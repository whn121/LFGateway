#include "log_consumer.h"

LogConsumer::LogConsumer(RedisClient* redis, size_t batchSize)
    : redis_(redis), batchSize_(batchSize) {}

LogConsumer::~LogConsumer() { stop(); }

void LogConsumer::start() {
    running_ = true;
    // 不再需要后台刷盘线程，由StreamWriter消费
}

void LogConsumer::stop() {
    running_ = false;
}

void LogConsumer::push(LogEntry entry) {
    std::map<std::string, std::string> fields;
    fields["path"] = entry.path;
    fields["user_id"] = std::to_string(entry.userId);
    fields["status_code"] = std::to_string(entry.statusCode);
    fields["latency_ms"] = std::to_string(entry.latencyMs);
    redis_->xadd("log_stream", "*", fields);
}
