#ifndef GATEWAY_H
#define GATEWAY_H

#include "../../include/net/tcp_server.hpp"
#include "../../include/pool/thread_pool.hpp"
#include "../../include/http/http_parser.hpp"
#include "mysql_pool.h"
#include "redis_client.h"
#include "router.h"
#include "auth_plugin.h"
#include "ratelimit_plugin.h"
#include "log_consumer.h"
#include "mgmt_handler.h"
#include "stream_writer.h"
#include "distributed_rate_limiter.h"
#include <memory>
#include <atomic>

class Gateway {
public:
    Gateway(EventLoop* loop, uint16_t apiPort, uint16_t mgmtPort,
            size_t threadNum,
            const std::string& mysqlHost, const std::string& mysqlUser,
            const std::string& mysqlPass, const std::string& mysqlDb,
            const std::string& redisHost, int redisPort);
    ~Gateway();

    void start();
    void stop();

    std::string getStatsJson();
    bool addRoute(const std::string& json);
     std::string getHealthJson();
     std::string getRouteListJson();
     bool deleteRoute(const std::string& json);
     std::string getLogsJson(const std::string& params);

private:
    void onApiRequest(const std::shared_ptr<TcpConnection>& conn, std::string& buf);
    void processApiRequest(const std::shared_ptr<TcpConnection>& conn, std::string raw);
    void onMgmtRequest(const std::shared_ptr<TcpConnection>& conn, std::string& buf);
    void sendResponse(const std::shared_ptr<TcpConnection>& conn, int code, const std::string& body);

    EventLoop* loop_;
    uint16_t apiPort_, mgmtPort_;
    size_t threadNum_;

    std::unique_ptr<StreamWriter> streamWriter_;
    std::unique_ptr<TcpServer> apiServer_;
    std::unique_ptr<TcpServer> mgmtServer_;
    std::unique_ptr<ThreadPool> threadPool_;
    std::unique_ptr<MySQLPool> mysqlPool_;
    std::unique_ptr<RedisPool> redisPool_;
    std::unique_ptr<RedisClient> redisClient_;
    std::unique_ptr<Router> router_;
    std::unique_ptr<AuthPlugin> auth_;
    std::unique_ptr<RateLimitPlugin> rateLimiter_;
    std::unique_ptr<LogConsumer> logConsumer_;
    std::unique_ptr<MgmtHandler> mgmtHandler_;
    std::unique_ptr<DistributedRateLimiter> distributedLimiter_;

    std::atomic<uint64_t> totalRequests_{0};
    std::atomic<uint64_t> totalLatencyUs_{0};
    std::atomic<uint64_t> rejectedAuth_{0};
    std::atomic<uint64_t> rejectedLimit_{0};
};

#endif
