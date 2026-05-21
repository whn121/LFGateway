#ifndef LOG_CONSUMER_H
#define LOG_CONSUMER_H

#include "mysql_pool.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

struct LogEntry {
    std::string path;
    int userId;
    int statusCode;
    double latencyMs;
};

class LogConsumer {
public:
    LogConsumer(MySQLPool* pool, size_t batchSize = 50);
    ~LogConsumer();

    void start();
    void stop();
    void push(LogEntry entry);

private:
    void run();

    MySQLPool* pool_;
    size_t batchSize_;
    std::vector<LogEntry> buffer_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

#endif
