#include "mysql_pool.h"
#include <stdexcept>
#include <thread>
#include <iostream>

MySQLPool::MySQLPool(const std::string& host, const std::string& user,
                     const std::string& pass, const std::string& db,
                     size_t pool_size)
    : host_(host), user_(user), pass_(pass), db_(db), pool_size_(pool_size) {
    mysql_library_init(0, nullptr, nullptr);
    initConnections();
}

MySQLPool::~MySQLPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        MYSQL* conn = pool_.front();
        pool_.pop();
        mysql_close(conn);
    }
    mysql_library_end();
}

void MySQLPool::initConnections() {
    for (size_t i = 0; i < pool_size_; ++i) {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) throw std::runtime_error("mysql_init failed");

        if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                                pass_.c_str(), db_.c_str(), 0, nullptr, 0)) {
            std::string err = mysql_error(conn);
            mysql_close(conn);
            throw std::runtime_error("MySQL connection failed: " + err);
        }

        mysql_set_character_set(conn, "utf8mb4");

        // 设置自动重连（MySQL 8.0已弃用但仍有效）
        bool reconnect = true;
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

        // 设置连接超时
        unsigned int timeout = 5;
        mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

        pool_.push(conn);
    }
}

bool MySQLPool::healthCheck(MYSQL* conn) {
    // 通过执行简单查询验证连接是否有效
    if (mysql_query(conn, "SELECT 1") != 0) {
        return false;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (res) mysql_free_result(res);
    return true;
}

MYSQL* MySQLPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    total_acquires_++;

    // 等待直到有空闲连接，最多等3秒
    if (!cv_.wait_for(lock, std::chrono::seconds(3), 
                      [this] { return !pool_.empty(); })) {
        total_timeouts_++;
        throw std::runtime_error("MySQL connection pool timeout: no available connection");
    }

    MYSQL* conn = pool_.front();
    pool_.pop();
    active_count_++;

    // 健康检查：如果连接失效，重新创建
    if (!healthCheck(conn)) {
        mysql_close(conn);
        conn = mysql_init(nullptr);
        if (!conn || !mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                                         pass_.c_str(), db_.c_str(), 
                                         0, nullptr, 0)) {
            active_count_--;
            throw std::runtime_error("MySQL reconnect failed");
        }
        mysql_set_character_set(conn, "utf8mb4");
    }

    return conn;
}

void MySQLPool::release(MYSQL* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (conn) {
        pool_.push(conn);
        active_count_--;
        cv_.notify_one();
    }
}

size_t MySQLPool::active_count() const { return active_count_.load(); }
size_t MySQLPool::idle_count() const { 
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.size(); 
}
size_t MySQLPool::total_acquires() const { return total_acquires_.load(); }
size_t MySQLPool::total_timeouts() const { return total_timeouts_.load(); }
