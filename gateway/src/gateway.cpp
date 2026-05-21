#include "gateway.h"
#include <sstream>
#include <chrono>
#include <netinet/in.h>
#include <cstring>

static sockaddr_in makeAddr(uint16_t port) {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    return addr;
}

Gateway::Gateway(EventLoop* loop, uint16_t apiPort, uint16_t mgmtPort,
                 size_t threadNum,
                 const std::string& mysqlHost, const std::string& mysqlUser,
                 const std::string& mysqlPass, const std::string& mysqlDb,
                 const std::string& redisHost, int redisPort)
    : loop_(loop), apiPort_(apiPort), mgmtPort_(mgmtPort), threadNum_(threadNum) {

    mysqlPool_ = std::make_unique<MySQLPool>(mysqlHost, mysqlUser, mysqlPass, mysqlDb, 4);
    redisPool_ = std::make_unique<RedisPool>(redisHost, redisPort, 4);
    redisClient_ = std::make_unique<RedisClient>(redisPool_.get());

    router_ = std::make_unique<Router>(mysqlPool_.get());
    router_->loadFromMySQL();

    auth_ = std::make_unique<AuthPlugin>(redisClient_.get());
    rateLimiter_ = std::make_unique<RateLimitPlugin>(redisClient_.get());
    logConsumer_ = std::make_unique<LogConsumer>(mysqlPool_.get(), 50);
    logConsumer_->start();

    mgmtHandler_ = std::make_unique<MgmtHandler>(this);
    threadPool_ = std::make_unique<ThreadPool>(threadNum);

    apiServer_ = std::make_unique<TcpServer>(loop, "ApiServer", makeAddr(apiPort));
    apiServer_->setMessageCallback(
        [this](const std::shared_ptr<TcpConnection>& conn, std::string& buf) {
            printf("[Gateway] API message received, size=%zu\n", buf.size());
            onApiRequest(conn, buf);
        });

    mgmtServer_ = std::make_unique<TcpServer>(loop, "MgmtServer", makeAddr(mgmtPort));
    mgmtServer_->setMessageCallback(
        [this](const std::shared_ptr<TcpConnection>& conn, std::string& buf) {
            printf("[Gateway] MGMT message received, size=%zu\n", buf.size());
            onMgmtRequest(conn, buf);
        });
}

Gateway::~Gateway() { stop(); }

void Gateway::start() {
    apiServer_->start();
    mgmtServer_->start();
    printf("[Gateway] API: %d, Mgmt: %d\n", apiPort_, mgmtPort_);
}

void Gateway::stop() {
    if (logConsumer_) logConsumer_->stop();
}

void Gateway::onApiRequest(const std::shared_ptr<TcpConnection>& conn, std::string& buf) {
    std::string raw = buf;
    threadPool_->enqueue([this, conn, raw]() { processApiRequest(conn, raw); });
}

void Gateway::processApiRequest(const std::shared_ptr<TcpConnection>& conn, std::string raw) {
    totalRequests_++;
    auto start = std::chrono::steady_clock::now();

    HttpParser parser;
    HttpRequest req;
    if (!parser.parse(raw.data(), raw.size(), req)) {
        sendResponse(conn, 400, "{\"error\": \"bad request\"}");
        return;
    }

    RouteRule* rule = router_->match(req.path);
    if (!rule) {
        sendResponse(conn, 404, "{\"error\": \"no route\"}");
        return;
    }

    if (rule->needAuth) {
        std::string token;
        auto it = req.headers.find("Authorization");
        if (it != req.headers.end() && it->second.find("Bearer ") == 0) {
            token = it->second.substr(7);
        }
        std::string userId;
        if (!auth_->validate(token, userId)) {
            rejectedAuth_++;
            sendResponse(conn, 401, "{\"error\": \"unauthorized\"}");
            return;
        }
    }

    if (rule->rateLimit > 0) {
        if (!rateLimiter_->isAllowed("rate:" + rule->pathPattern, rule->rateLimit)) {
            rejectedLimit_++;
            sendResponse(conn, 429, "{\"error\": \"rate limit exceeded\"}");
            return;
        }
    }

    std::string response = "{\"message\": \"OK\", \"path\": \"" + req.path + "\"}";
    sendResponse(conn, 200, response);

    auto end = std::chrono::steady_clock::now();
    double latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
    totalLatencyUs_ += static_cast<uint64_t>(latencyMs * 1000);

    LogEntry entry;
    entry.path = req.path;
    entry.userId = 0;
    entry.statusCode = 200;
    entry.latencyMs = latencyMs;
    logConsumer_->push(std::move(entry));
}

void Gateway::onMgmtRequest(const std::shared_ptr<TcpConnection>& conn, std::string& buf) {
    printf("[Gateway] onMgmtRequest called, conn=%p, buf size=%zu\n", conn.get(), buf.size());
    if (!conn) {
        printf("[Gateway] onMgmtRequest: conn is null!\n");
        return;
    }
    mgmtHandler_->onMessage(conn, buf);
}

void Gateway::sendResponse(const std::shared_ptr<TcpConnection>& conn, int code, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " OK\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: keep-alive\r\n"
        << "Server: LFGateway/1.0\r\n\r\n"
        << body;
    conn->send(oss.str());
}

std::string Gateway::getStatsJson() {
    uint64_t reqs = totalRequests_.load();
    uint64_t latUs = totalLatencyUs_.load();
    double avgMs = reqs > 0 ? (latUs / 1000.0 / reqs) : 0;
    std::ostringstream oss;
    oss << "{\"totalRequests\":" << reqs
        << ",\"avgLatencyMs\":" << avgMs
        << ",\"rejectedAuth\":" << rejectedAuth_.load()
        << ",\"rejectedLimit\":" << rejectedLimit_.load() << "}";
    return oss.str();
}

bool Gateway::addRoute(const std::string& json) {
    return router_->reload();
}
