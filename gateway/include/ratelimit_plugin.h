#ifndef RATELIMIT_PLUGIN_H
#define RATELIMIT_PLUGIN_H

#include "redis_client.h"
#include <string>

class RateLimitPlugin {
public:
    explicit RateLimitPlugin(RedisClient* redis) : redis_(redis) {}

    // 返回 true 表示未超限，允许通过
    bool isAllowed(const std::string& keyPrefix, int maxPerSecond);

private:
    RedisClient* redis_;
};

#endif
