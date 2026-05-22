#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

class MySQLPool {
public:
    MySQLPool(const std::string& host, const std::string& user,
              const std::string& pass, const std::string& db,
              size_t pool_size = 4);
    ~MySQLPool();

    MYSQL* acquire();
    void release(MYSQL* conn);

    // 连接池统计接口
    size_t active_count() const;      // 当前正在使用的连接数
    size_t idle_count() const;        // 当前空闲连接数
    size_t total_acquires() const;    // 累计获取连接次数
    size_t total_timeouts() const;    // 累计超时等待次数

private:
    void initConnections();
    bool healthCheck(MYSQL* conn);    // 连接健康检查

    std::string host_, user_, pass_, db_;
    size_t pool_size_;
    std::queue<MYSQL*> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    // 连接池统计
    std::atomic<size_t> active_count_{0};
    std::atomic<size_t> total_acquires_{0};
    std::atomic<size_t> total_timeouts_{0};
};

#endif
