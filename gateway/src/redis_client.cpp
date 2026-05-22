#include "redis_client.h"
#include <stdexcept>
#include <cstdarg>
#include <cstring>

// ==================== RedisPool ====================

RedisPool::RedisPool(const std::string& host, int port, size_t pool_size)
    : host_(host), port_(port), pool_size_(pool_size) {
    initConnections();
}

RedisPool::~RedisPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        redisFree(pool_.front());
        pool_.pop();
    }
}

void RedisPool::initConnections() {
    for (size_t i = 0; i < pool_size_; ++i) {
        redisContext* ctx = redisConnect(host_.c_str(), port_);
        if (!ctx || ctx->err) {
            std::string err = ctx ? (ctx->errstr ? ctx->errstr : "connection failed") : "connection failed";
            if (ctx) redisFree(ctx);
            throw std::runtime_error("Redis connection failed: " + err);
        }
        struct timeval timeout = {2, 0};
        redisSetTimeout(ctx, timeout);
        pool_.push(ctx);
    }
}

redisContext* RedisPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !pool_.empty(); });
    redisContext* ctx = pool_.front();
    pool_.pop();
    return ctx;
}

void RedisPool::release(redisContext* ctx) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(ctx);
    cv_.notify_one();
}

// ==================== RedisClient 基础命令 ====================

redisReply* RedisClient::execute(const char* format, ...) {
    redisContext* ctx = pool_->acquire();
    if (!ctx) return nullptr;

    va_list args;
    va_start(args, format);
    redisReply* reply = (redisReply*)redisvCommand(ctx, format, args);
    va_end(args);

    pool_->release(ctx);
    return reply;      // 可能为 nullptr，由上层调用者检查
}

bool RedisClient::setex(const std::string& key, int seconds, const std::string& value) {
    redisReply* reply = execute("SETEX %s %d %s", key.c_str(), seconds, value.c_str());
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    return ok;
}

std::string RedisClient::get(const std::string& key) {
    redisReply* reply = execute("GET %s", key.c_str());
    if (!reply) return "";
    std::string result;
    if (reply->type == REDIS_REPLY_STRING) {
        result.assign(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}

long long RedisClient::incr(const std::string& key) {
    redisReply* reply = execute("INCR %s", key.c_str());
    if (!reply) return -1;
    long long val = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : -1;
    freeReplyObject(reply);
    return val;
}

bool RedisClient::expire(const std::string& key, int seconds) {
    redisReply* reply = execute("EXPIRE %s %d", key.c_str(), seconds);
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}

bool RedisClient::exists(const std::string& key) {
    redisReply* reply = execute("EXISTS %s", key.c_str());
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}

bool RedisClient::publish(const std::string& channel, const std::string& message) {
    redisReply* reply = execute("PUBLISH %s %s", channel.c_str(), message.c_str());
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return ok;
}

// ==================== Redis Streams 命令（新增） ====================

bool RedisClient::xadd(const std::string& key, const std::string& id,
                       const std::map<std::string, std::string>& fields) {
    std::string cmd = "XADD " + key + " MAXLEN ~ 10000 * ";
    for (auto& kv : fields) {
        cmd += kv.first + " " + kv.second + " ";
    }
    redisReply* reply = execute(cmd.c_str());
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_STRING);
    freeReplyObject(reply);
    return ok;
}

bool RedisClient::xgroup_create(const std::string& stream, const std::string& group,
                                const std::string& start_id) {
    std::string cmd = "XGROUP CREATE " + stream + " " + group + " " + start_id;
    redisReply* reply = execute(cmd.c_str());
    // 忽略组已存在的错误
    if (reply) freeReplyObject(reply);
    return true;  // 我们总是认为创建成功（或已存在）
}

std::vector<std::map<std::string, std::string>> RedisClient::xreadgroup(
    const std::string& group, const std::string& consumer,
    const std::string& stream, const std::string& id, int count) {
    std::string cmd = "XREADGROUP GROUP " + group + " " + consumer +
                      " COUNT " + std::to_string(count) +
                      " STREAMS " + stream + " " + id;
    redisReply* reply = execute(cmd.c_str());
    std::vector<std::map<std::string, std::string>> result;
    if (!reply) return result;

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            auto& streamReply = reply->element[i];
            if (streamReply->type != REDIS_REPLY_ARRAY) continue;
            for (size_t j = 0; j < streamReply->elements; ++j) {
                auto& msgArray = streamReply->element[j];
                if (msgArray->elements >= 2) {
                    std::map<std::string, std::string> msg;
                    msg["id"] = std::string(msgArray->element[0]->str, msgArray->element[0]->len);
                    auto& fields = msgArray->element[1];
                    for (size_t k = 0; k < fields->elements; k += 2) {
                        msg[std::string(fields->element[k]->str, fields->element[k]->len)] =
                            std::string(fields->element[k+1]->str, fields->element[k+1]->len);
                    }
                    result.push_back(msg);
                }
            }
        }
    }
    freeReplyObject(reply);
    return result;
}

bool RedisClient::xack(const std::string& stream, const std::string& group,
                       const std::string& id) {
    std::string cmd = "XACK " + stream + " " + group + " " + id;
    redisReply* reply = execute(cmd.c_str());
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}
