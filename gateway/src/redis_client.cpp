#include "redis_client.h"
#include <stdexcept>
#include <cstdarg>
#include <cstring>

// -------------------- RedisPool --------------------

RedisPool::RedisPool(const std::string& host, int port, size_t pool_size)
    : host_(host), port_(port), pool_size_(pool_size) {
    initConnections();
}

RedisPool::~RedisPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        redisFree(pool_.front());
        pool_.pop();
    }
}

void RedisPool::initConnections() {
    for (size_t i = 0; i < pool_size_; ++i) {
        redisContext* ctx = redisConnect(host_.c_str(), port_);
        if (!ctx || ctx->err) {
            std::string err = ctx ? ctx->errstr : "connection failed";
            if (ctx) redisFree(ctx);
            throw std::runtime_error("Redis connection failed: " + err);
        }
        struct timeval timeout = {2, 0};
        redisSetTimeout(ctx, timeout);
        pool_.push(ctx);
    }
}

redisContext* RedisPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !pool_.empty(); });
    redisContext* ctx = pool_.front();
    pool_.pop();
    return ctx;
}

void RedisPool::release(redisContext* ctx) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(ctx);
    cv_.notify_one();
}

// -------------------- RedisClient --------------------

redisReply* RedisClient::execute(const char* format, ...) {
    redisContext* ctx = pool_->acquire();
    va_list args;
    va_start(args, format);
    redisReply* reply = (redisReply*)redisvCommand(ctx, format, args);
    va_end(args);
    pool_->release(ctx);
    if (!reply) {
        throw std::runtime_error("Redis command failed: " + std::string(ctx->errstr));
    }
    return reply;
}

bool RedisClient::setex(const std::string& key, int seconds, const std::string& value) {
    redisReply* reply = execute("SETEX %s %d %s", key.c_str(), seconds, value.c_str());
    bool ok = (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    return ok;
}

std::string RedisClient::get(const std::string& key) {
    redisReply* reply = execute("GET %s", key.c_str());
    std::string result;
    if (reply->type == REDIS_REPLY_STRING) {
        result.assign(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}

long long RedisClient::incr(const std::string& key) {
    redisReply* reply = execute("INCR %s", key.c_str());
    long long val = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : -1;
    freeReplyObject(reply);
    return val;
}

bool RedisClient::expire(const std::string& key, int seconds) {
    redisReply* reply = execute("EXPIRE %s %d", key.c_str(), seconds);
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}

bool RedisClient::exists(const std::string& key) {
    redisReply* reply = execute("EXISTS %s", key.c_str());
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}

bool RedisClient::publish(const std::string& channel, const std::string& message) {
    redisReply* reply = execute("PUBLISH %s %s", channel.c_str(), message.c_str());
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return ok;
}
