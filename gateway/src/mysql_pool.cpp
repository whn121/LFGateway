#include "mysql_pool.h"
#include <stdexcept>
#include <thread>

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

        bool reconnect = true;
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

        pool_.push(conn);
    }
}

MYSQL* MySQLPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !pool_.empty(); });
    MYSQL* conn = pool_.front();
    pool_.pop();
    return conn;
}

void MySQLPool::release(MYSQL* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cv_.notify_one();
}
