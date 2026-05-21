#ifndef REDIS_CLIENT_H
#define REDIS_CLIENT_H

#include <hiredis/hiredis.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

/**
 * Redis 连接池
 */
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

/**
 * Redis 客户端封装
 */
class RedisClient {
public:
    explicit RedisClient(RedisPool* pool) : pool_(pool) {}

    bool setex(const std::string& key, int seconds, const std::string& value);
    std::string get(const std::string& key);
    long long incr(const std::string& key);
    bool expire(const std::string& key, int seconds);
    bool exists(const std::string& key);
    bool publish(const std::string& channel, const std::string& message);

private:
    RedisPool* pool_;
    redisReply* execute(const char* format, ...);
};

#endif
