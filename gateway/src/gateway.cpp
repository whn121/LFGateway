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
    redisPool_ = std::make_unique<RedisPool>(redisHost, redisPort, 50);
    redisClient_ = std::make_unique<RedisClient>(redisPool_.get());

    router_ = std::make_unique<Router>(mysqlPool_.get());
    router_->loadFromMySQL();

    auth_ = std::make_unique<AuthPlugin>(redisClient_.get());
    rateLimiter_ = std::make_unique<RateLimitPlugin>(redisClient_.get());

    logConsumer_ = std::make_unique<LogConsumer>(redisClient_.get(), 50);
    logConsumer_->start();

    mgmtHandler_ = std::make_unique<MgmtHandler>(this);
    threadPool_ = std::make_unique<ThreadPool>(threadNum);

    // 创建 StreamWriter（用于从 Redis Stream 消费日志并写入 MySQL）
    streamWriter_ = std::make_unique<StreamWriter>(redisClient_.get(), mysqlPool_.get());
    streamWriter_->start();

    // 创建两个服务器
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
    rateLimiter_ = std::make_unique<RateLimitPlugin>(redisClient_.get());

     //初始化分布式限流器
    distributedLimiter_ = std::make_unique<DistributedRateLimiter>(
        redisClient_.get(), "gateway:rate_limit");
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
        if (it != req.headers.end() && !it->second.empty() && it->second.find("Bearer ") == 0) {
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
    bool allowed = false;
    
    // 优先使用分布式限流
    if (distributedLimiter_) {
        allowed = distributedLimiter_->isAllowed(
            rule->pathPattern,           // API 路径作为 key
            rule->rateLimit,             // 每秒允许的请求数
            rule->rateLimit * 2          // 令牌桶容量 = 速率的 2 倍，允许一定突发
        );
    }
    
    // 如果分布式限流器不存在或不可用，降级到本地限流
    if (!distributedLimiter_ || !allowed) {
        allowed = rateLimiter_->isAllowed("rate:" + rule->pathPattern, rule->rateLimit);
    }
    
    if (!allowed) {
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

std::string Gateway::getHealthJson() {
    std::ostringstream oss;
    oss << "{"
        << "\"status\":\"running\","
        << "\"totalRequests\":" << totalRequests_.load() << ","
        << "\"activeConnections\":" << (apiServer_ ? "running" : "stopped") << ","
        << "\"mysqlPool\":{"
        << "\"active\":" << mysqlPool_->active_count() << ","
        << "\"idle\":" << mysqlPool_->idle_count() << ","
        << "\"totalAcquires\":" << mysqlPool_->total_acquires() << ","
        << "\"totalTimeouts\":" << mysqlPool_->total_timeouts()
        << "},"
        << "\"redisPool\":\"connected\""
        << "}";
    return oss.str();
}

bool Gateway::addRoute(const std::string& json) {
    return router_->reload();
}

bool Gateway::deleteRoute(const std::string& json) {
    // 尝试从 json 里提取 id 字段，格式简单：{"id": 数字}
    size_t idPos = json.find("\"id\"");
    if (idPos == std::string::npos) return false;
    size_t colon = json.find(':', idPos);
    if (colon == std::string::npos) return false;
    size_t start = json.find_first_of("0123456789", colon);
    if (start == std::string::npos) return false;
    size_t end = json.find_first_not_of("0123456789", start);
    std::string idStr = json.substr(start, end - start);
    int routeId = std::stoi(idStr);

    MYSQL* conn = mysqlPool_->acquire();
    std::string sql = "DELETE FROM api_routes WHERE id = " + std::to_string(routeId);
    int ret = mysql_query(conn, sql.c_str());
    mysqlPool_->release(conn);

    if (ret == 0) {
        router_->reload();   // 重新加载路由表
        return true;
    }
    return false;
}

std::string Gateway::getLogsJson(const std::string& params) {
    std::ostringstream oss;
    oss << "[";
    MYSQL* conn = mysqlPool_->acquire();
    if (mysql_query(conn, "SELECT path, user_id, status_code, latency_ms, created_at "
                          "FROM api_call_logs ORDER BY id DESC LIMIT 20") == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row;
        bool first = true;
        while ((row = mysql_fetch_row(res))) {
            if (!first) oss << ",";
            first = false;
            oss << "{"
                << "\"path\":\"" << (row[0] ? row[0] : "") << "\","
                << "\"userId\":" << (row[1] ? row[1] : "0") << ","
                << "\"status\":" << (row[2] ? row[2] : "0") << ","
                << "\"latency\":" << (row[3] ? row[3] : "0") << ","
                << "\"time\":\"" << (row[4] ? row[4] : "") << "\""
                << "}";
        }
        mysql_free_result(res);
    }
    mysqlPool_->release(conn);
    oss << "]";
    return oss.str();
}

std::string Gateway::getRouteListJson() {
    std::ostringstream oss;
    oss << "[";
    MYSQL* conn = mysqlPool_->acquire();
    if (mysql_query(conn, "SELECT id, path_pattern, target_host, target_port, "
                          "need_auth, rate_limit FROM api_routes") == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row;
        bool first = true;
        while ((row = mysql_fetch_row(res))) {
            if (!first) oss << ",";
            first = false;
            oss << "{"
                << "\"id\":" << (row[0] ? row[0] : "0") << ","
                << "\"path\":\"" << (row[1] ? row[1] : "") << "\","
                << "\"target\":\"" << (row[2] ? row[2] : "") << ":" << (row[3] ? row[3] : "0") << "\","
                << "\"auth\":" << (row[4] ? (strcmp(row[4], "1") == 0 ? "true" : "false") : "true") << ","
                << "\"rateLimit\":" << (row[5] ? row[5] : "0")
                << "}";
        }
        mysql_free_result(res);
    }
    mysqlPool_->release(conn);
    oss << "]";
    return oss.str();
}
