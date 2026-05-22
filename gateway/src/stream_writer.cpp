#include "stream_writer.h"
#include <sstream>
#include <chrono>
#include <iostream>

StreamWriter::StreamWriter(RedisClient* redis, MySQLPool* mysql)
    : redis_(redis), mysql_(mysql) {}

StreamWriter::~StreamWriter() { stop(); }

void StreamWriter::start() {
    running_ = true;
    worker_ = std::thread(&StreamWriter::run, this);
}

void StreamWriter::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void StreamWriter::run() {
    const std::string stream = "log_stream";
    const std::string group = "log_writers";
    const std::string consumer = "writer_1";

    // 尝试创建消费者组（忽略已存在错误）
    redis_->xgroup_create(stream, group, "0");

    while (running_) {
        try {
            auto messages = redis_->xreadgroup(group, consumer, stream, ">", 10);

            if (messages.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            MYSQL* conn = mysql_->acquire();
            std::ostringstream sql;
            sql << "INSERT INTO api_call_logs (path, user_id, status_code, latency_ms) VALUES ";

            bool first = true;
            for (size_t i = 0; i < messages.size(); ++i) {
                auto& msg = messages[i];
                if (!first) sql << ", ";
                first = false;
                sql << "('" << msg["path"] << "', "
                    << msg["user_id"] << ", "
                    << msg["status_code"] << ", "
                    << msg["latency_ms"] << ")";
            }

            if (mysql_query(conn, sql.str().c_str()) == 0) {
                // 插入成功后 ACK 所有消息
                for (auto& msg : messages) {
                    redis_->xack(stream, group, msg["id"]);
                }
            }
            mysql_->release(conn);
        } catch (const std::exception& e) {
            std::cerr << "[StreamWriter] Error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}
