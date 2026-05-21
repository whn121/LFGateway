#include "net/tcp_connection.hpp"
#include "log/async_logger.hpp"
#include <unistd.h>
#include <errno.h>

TcpConnection::TcpConnection(EventLoop* loop, int sockfd, const std::string& name)
    : loop_(loop), sockfd_(sockfd), name_(name), channel_(loop, sockfd) {
    channel_.setReadCallback(std::bind(&TcpConnection::handleRead, this));
    channel_.setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_.setCloseCallback(std::bind(&TcpConnection::handleClose, this));
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
    channel_.enableReading();
    if (connectionCallback_) {
        connectionCallback_(shared_from_this());
    }
}

void TcpConnection::connectDestroyed() {
    if (channel_.index() >= 0) {
        channel_.disableAll();
    }
}

void TcpConnection::send(const std::string& message) {
    if (loop_->isInLoopThread()) {
        sendInLoop(message);
    } else {
        loop_->runInLoop([this, message] { sendInLoop(message); });
    }
}

void TcpConnection::sendInLoop(const std::string& message) {
    if (sockfd_ < 0) {
        // 连接已关闭，不操作
        AsyncLogger::instance().log("[WARN] sendInLoop on closed connection: " + name_);
        return;
    }
    outputBuffer_ += message;
    if (!channel_.isWriting()) {
        channel_.enableWriting();
    }
}

void TcpConnection::handleRead() {
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
    if (sockfd_ < 0) return;
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
                    handleClose();
                } else {
                    AsyncLogger::instance().log("[ERROR] write error on " + name_);
                    handleClose();
                }
            }
        }
    }
}

void TcpConnection::handleClose() {
    AsyncLogger::instance().log("[INFO] Connection closed: " + name_);
    if (sockfd_ >= 0) {
        channel_.disableAll();
        if (closeCallback_) {
            closeCallback_(shared_from_this());
        }
        ::close(sockfd_);
        sockfd_ = -1;
    }
}
