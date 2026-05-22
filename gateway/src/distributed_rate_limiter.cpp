#include "distributed_rate_limiter.h"
#include <chrono>
#include <sstream>
#include <iostream>

static const char* TOKEN_BUCKET_LUA_SCRIPT = R"SCRIPT(
    local key = KEYS[1]
    local capacity = tonumber(ARGV[1])
    local rate = tonumber(ARGV[2])
    local now = tonumber(ARGV[3])

    local bucket = redis.call('hmget', key, 'tokens', 'last_time')
    local tokens = tonumber(bucket[1])
    local last_time = tonumber(bucket[2])

    if tokens == nil then
        tokens = capacity
        last_time = now
    else
        local elapsed_ms = math.max(0, now - last_time)
        local refill = elapsed_ms * (rate / 1000.0)
        tokens = math.min(capacity, tokens + refill)
    end

    if tokens >= 1 then
        tokens = tokens - 1
        redis.call('hmset', key, 'tokens', tokens, 'last_time', now)
        redis.call('expire', key, 3)
        return 1
    else
        redis.call('hmset', key, 'tokens', tokens, 'last_time', last_time)
        redis.call('expire', key, 3)
        return 0
    end
)SCRIPT";

DistributedRateLimiter::DistributedRateLimiter(RedisClient* redis, const std::string& key_prefix)
    : redis_(redis), key_prefix_(key_prefix) {}

bool DistributedRateLimiter::isAllowed(const std::string& api_path, int rate, int capacity) {

    if (capacity <= 0) {
        capacity = rate;
    }

    std::string key = key_prefix_ + ":" + api_path;

    try {
        bool result = acquireToken(key, rate, capacity);
        redis_available_ = true;
        return result;
    } catch (const std::exception& e) {
        last_error_ = std::string("Redis error, fallback to allow: ") + e.what();
        std::cerr << "[DistributedRateLimiter] " << last_error_ << std::endl;
        printf("[DistributedRateLimiter] Exception: %s\n", e.what());
        redis_available_ = false;
        return true;  // 降级放行
    }
}

bool DistributedRateLimiter::acquireToken(const std::string& key, int rate, int capacity) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();

    // 获取 Redis 连接
    RedisPool* pool = redis_->getPool();
    if (!pool) {
        throw std::runtime_error("getPool() returned null");
    }

    redisContext* ctx = pool->acquire();
    if (!ctx) {
        throw std::runtime_error("Failed to acquire Redis connection from pool");
    }

    // 执行 Lua 脚本
    redisReply* reply = (redisReply*)redisCommand(ctx,
        "EVAL %s 1 %s %d %d %lld",
        TOKEN_BUCKET_LUA_SCRIPT,
        key.c_str(),
        capacity,
        rate,
        (long long)now);

    pool->release(ctx);

    if (!reply) {
        throw std::runtime_error("Redis command returned null reply");
    }

    bool allowed = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return allowed;
}
