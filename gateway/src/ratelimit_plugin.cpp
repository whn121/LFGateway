#include "ratelimit_plugin.h"
#include <chrono>

bool RateLimitPlugin::isAllowed(const std::string& keyPrefix, int maxPerSecond) {
    if (maxPerSecond <= 0) return true;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();

    std::string key = keyPrefix + ":" + std::to_string(now);
    long long count = redis_->incr(key);
    if (count == 1) {
        redis_->expire(key, 2);
    }
    return count <= maxPerSecond;
}
