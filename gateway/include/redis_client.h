#ifndef REDIS_CLIENT_H
#define REDIS_CLIENT_H

#include <hiredis/hiredis.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>

class RedisPool {
public:
    RedisPool(const std::string& host, int port, size_t pool_size = 4);
    ~RedisPool();
    redisContext* acquire();
    void release(redisContext* ctx);
private:
    void initConnections();
    std::string host_;
    int port_;
    size_t pool_size_;
    std::queue<redisContext*> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

class RedisClient {
public:
    explicit RedisClient(RedisPool* pool) : pool_(pool) {}

    // 原有的通用方法
    bool setex(const std::string& key, int seconds, const std::string& value);
    std::string get(const std::string& key);
    long long incr(const std::string& key);
    bool expire(const std::string& key, int seconds);
    bool exists(const std::string& key);
    bool publish(const std::string& channel, const std::string& message);

    // 新增：暴露连接池，供DistributedRateLimiter等模块执行Lua脚本
    RedisPool* getPool() { return pool_; }

    // 新增：Redis Streams 相关方法
    bool xadd(const std::string& key, const std::string& id,
              const std::map<std::string, std::string>& fields);
    bool xgroup_create(const std::string& stream, const std::string& group,
                       const std::string& start_id = "$");
    std::vector<std::map<std::string, std::string>> xreadgroup(
        const std::string& group, const std::string& consumer,
        const std::string& stream, const std::string& id = ">",
        int count = 10);
    bool xack(const std::string& stream, const std::string& group,
              const std::string& id);

private:
    RedisPool* pool_;
    redisReply* execute(const char* format, ...);
};

#endif
