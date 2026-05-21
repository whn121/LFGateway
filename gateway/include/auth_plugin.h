#ifndef AUTH_PLUGIN_H
#define AUTH_PLUGIN_H

#include "redis_client.h"
#include <string>

class AuthPlugin {
public:
    explicit AuthPlugin(RedisClient* redis) : redis_(redis) {}

    // 返回 true 表示 token 有效，同时输出 userId
    bool validate(const std::string& token, std::string& userId);

private:
    RedisClient* redis_;
};

#endif
