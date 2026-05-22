#ifndef DISTRIBUTED_RATE_LIMITER_H
#define DISTRIBUTED_RATE_LIMITER_H

#include "redis_client.h"
#include <string>
#include <vector>

/**
 * 分布式令牌桶限流器
 * 
 * 使用 Redis Lua 脚本原子化执行令牌桶算法，多个网关实例共享同一个 Redis key，
 * 实现真正的分布式限流。
 * 
 * 容错设计：当 Redis 不可用时，自动降级为本地限流（Fail-Open 策略），
 * 保证网关核心链路不被限流组件阻塞。
 */
class DistributedRateLimiter {
public:
    /**
     * @param redis      Redis 客户端
     * @param key_prefix 限流 key 前缀，如 "gateway:rate_limit"
     */
    DistributedRateLimiter(RedisClient* redis, const std::string& key_prefix);
    ~DistributedRateLimiter() = default;

    /**
     * 检查请求是否被允许
     * @param api_path   API 路径，如 "/api/test"
     * @param rate       每秒允许的请求数（令牌填充速率）
     * @param capacity   令牌桶容量（允许的最大突发流量）
     * @return true 表示允许通过，false 表示被限流
     */
    bool isAllowed(const std::string& api_path, int rate, int capacity = -1);

    /**
     * 获取最近一次操作的错误信息
     */
    std::string getLastError() const { return last_error_; }

private:
    /**
     * 执行 Redis Lua 脚本，原子化执行令牌桶算法
     * @return true 表示获取令牌成功
     */
    bool acquireToken(const std::string& key, int rate, int capacity);

    RedisClient* redis_;
    std::string key_prefix_;
    std::string last_error_;
    bool redis_available_{true};  // Redis 可用性标记
};

#endif
