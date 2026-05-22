#ifndef STREAM_WRITER_H
#define STREAM_WRITER_H

#include "redis_client.h"
#include "mysql_pool.h"
#include <string>
#include <thread>
#include <atomic>

class StreamWriter {
public:
    StreamWriter(RedisClient* redis, MySQLPool* mysql);
    ~StreamWriter();

    void start();
    void stop();

private:
    void run();

    RedisClient* redis_;
    MySQLPool* mysql_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

#endif
