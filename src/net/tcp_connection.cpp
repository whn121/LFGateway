#include "net/tcp_connection.hpp"
#include "log/async_logger.hpp"
#include <unistd.h>
#include <errno.h>

TcpConnection::TcpConnection(EventLoop* loop, int sockfd, const std::string& name)
    : loop_(loop), sockfd_(sockfd), name_(name), channel_(loop, sockfd)
{
    AsyncLogger::instance().log("[INFO] TcpConnection created: " + name_);
}

TcpConnection::~TcpConnection() {
    AsyncLogger::instance().log("[INFO] TcpConnection destroyed: " + name_);
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

void TcpConnection::connectEstablished() {
    auto self = shared_from_this();
    channel_.setReadCallback([self] { self->handleRead(); });
    channel_.setWriteCallback([self] { self->handleWrite(); });
    channel_.setCloseCallback([self] { self->handleClose(); });
    channel_.enableReading();
    if (connectionCallback_) {
        connectionCallback_(self);
    }
}

void TcpConnection::connectDestroyed() {
    if (channel_.index() >= 0) {
        channel_.disableAll();
        channel_.markAsRemoved();
    }
}

void TcpConnection::send(const std::string& message) {
    if (loop_->isInLoopThread()) {
        sendInLoop(message);
    } else {
        auto self = shared_from_this();
        loop_->runInLoop([self, message] { self->sendInLoop(message); });
    }
}

void TcpConnection::sendInLoop(const std::string& message) {
    // 核心保护：如果连接已关闭或 Channel 已被移除，直接丢弃数据
    if (sockfd_ < 0 || channel_.isRemoved()) return;
    
    outputBuffer_ += message;
    if (!channel_.isWriting()) {
        channel_.enableWriting();
    }
}

void TcpConnection::handleRead() {
    if (sockfd_ < 0 || channel_.isRemoved()) return;
    
    char buf[65536];
    ssize_t n = ::read(sockfd_, buf, sizeof(buf));
    if (n > 0) {
        inputBuffer_.append(buf, n);
        if (messageCallback_) {
            messageCallback_(shared_from_this(), inputBuffer_);
            inputBuffer_.clear();
        }
    } else if (n == 0) {
        handleClose();
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            AsyncLogger::instance().log("[ERROR] read error on " + name_);
            handleClose();
        }
    }
}

void TcpConnection::handleWrite() {
    if (sockfd_ < 0 || channel_.isRemoved()) return;
    
    if (channel_.isWriting()) {
        ssize_t n = ::write(sockfd_, outputBuffer_.data(), outputBuffer_.size());
        if (n > 0) {
            outputBuffer_.erase(0, n);
            if (outputBuffer_.empty()) {
                channel_.disableWriting();
            }
        } else if (n == 0) {
            handleClose();
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                if (errno == EBADF || errno == EPIPE) {
                    AsyncLogger::instance().log("[WARN] write failed, fd closed: " + name_);
                } else {
                    AsyncLogger::instance().log("[ERROR] write error on " + name_);
                }
                handleClose();
            }
        }
    }
}

void TcpConnection::handleClose() {
    if (sockfd_ < 0) return;
    
    AsyncLogger::instance().log("[INFO] Connection closed: " + name_);
    
    // 第一步：从 epoll 移除并标记为已删除，阻止任何新操作
    channel_.remove();
    
    // 第二步：安全地通知上层
    if (closeCallback_) {
        closeCallback_(shared_from_this());
    }
    
    // 第三步：关闭 fd 并标记为无效
    ::close(sockfd_);
    sockfd_ = -1;
}
