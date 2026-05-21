#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

/**
 * MySQL 连接池
 * 功能：管理一组 MySQL 连接，线程安全地获取和归还连接
 */
class MySQLPool {
public:
    MySQLPool(const std::string& host, const std::string& user,
              const std::string& pass, const std::string& db,
              size_t pool_size = 4);
    ~MySQLPool();

    MYSQL* acquire();          // 获取连接，若池空则阻塞
    void release(MYSQL* conn); // 归还连接

private:
    void initConnections();    // 初始化连接

    std::string host_, user_, pass_, db_;
    size_t pool_size_;
    std::queue<MYSQL*> pool_;  // 可用连接队列
    std::mutex mutex_;
    std::condition_variable cv_;
};

#endif
