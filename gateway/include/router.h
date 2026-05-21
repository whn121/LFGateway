#ifndef ROUTER_H
#define ROUTER_H

#include "mysql_pool.h"
#include <string>
#include <vector>
#include <mutex>

struct RouteRule {
    int id;
    std::string pathPattern;
    std::string targetHost;
    int targetPort;
    bool needAuth;
    int rateLimit;

    bool match(const std::string& path) const {
        return path.compare(0, pathPattern.size(), pathPattern) == 0;
    }
};

class Router {
public:
    explicit Router(MySQLPool* pool);

    bool loadFromMySQL();               // 从数据库加载路由
    bool reload();                      // 重新加载
    RouteRule* match(const std::string& path); // 匹配路径，返回规则指针

private:
    MySQLPool* pool_;
    std::vector<RouteRule> rules_;
    std::mutex mutex_;
};

#endif
