#include "router.h"
#include <stdexcept>

Router::Router(MySQLPool* pool) : pool_(pool) {}

bool Router::loadFromMySQL() {
    MYSQL* conn = pool_->acquire();

    const char* sql = "SELECT id, path_pattern, target_host, target_port, "
                      "need_auth, rate_limit FROM api_routes";
    if (mysql_query(conn, sql) != 0) {
        pool_->release(conn);
        return false;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        pool_->release(conn);
        return false;
    }

    std::vector<RouteRule> newRules;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        RouteRule rule;
        // 安全处理每一个可能为 NULL 的字段
        rule.id          = row[0] ? std::stoi(row[0]) : 0;
        rule.pathPattern = row[1] ? std::string(row[1]) : "";
        rule.targetHost  = row[2] ? std::string(row[2]) : "";
        rule.targetPort  = row[3] ? std::stoi(row[3]) : 0;
        rule.needAuth    = row[4] ? (std::stoi(row[4]) != 0) : true;
        rule.rateLimit   = row[5] ? std::stoi(row[5]) : 0;
        newRules.push_back(rule);
    }

    mysql_free_result(result);
    pool_->release(conn);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        rules_ = std::move(newRules);
    }
    return true;
}

bool Router::reload() {
    return loadFromMySQL();
}

RouteRule* Router::match(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& rule : rules_) {
        if (rule.match(path)) {
            return &rule;
        }
    }
    return nullptr;
}
