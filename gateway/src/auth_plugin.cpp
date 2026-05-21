#include "auth_plugin.h"

bool AuthPlugin::validate(const std::string& token, std::string& userId) {
    std::string key = "token:" + token;
    std::string val = redis_->get(key);
    if (val.empty()) return false;
    userId = val;
    return true;
}
