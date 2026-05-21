#include "log_consumer.h"
#include <sstream>
#include <algorithm>

LogConsumer::LogConsumer(MySQLPool* pool, size_t batchSize)
    : pool_(pool), batchSize_(batchSize) {}

LogConsumer::~LogConsumer() { stop(); }

void LogConsumer::start() {
    running_ = true;
    worker_ = std::thread(&LogConsumer::run, this);
}

void LogConsumer::stop() {
    running_ = false;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void LogConsumer::push(LogEntry entry) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(std::move(entry));
    }
    if (buffer_.size() >= batchSize_) cv_.notify_one();
}

void LogConsumer::run() {
    while (running_) {
        std::vector<LogEntry> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !running_ || buffer_.size() >= batchSize_;
            });
            if (!running_ && buffer_.empty()) break;
            batch.swap(buffer_);
        }
        if (batch.empty()) continue;

        MYSQL* conn = pool_->acquire();
        std::ostringstream sql;
        sql << "INSERT INTO api_call_logs (path, user_id, status_code, latency_ms) VALUES ";
        for (size_t i = 0; i < batch.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << "('" << batch[i].path << "', " << batch[i].userId << ", "
                << batch[i].statusCode << ", " << batch[i].latencyMs << ")";
        }
        mysql_query(conn, sql.str().c_str());
        pool_->release(conn);
    }
}
