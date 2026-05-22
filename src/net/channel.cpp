#include "net/channel.hpp"
#include "net/eventloop.hpp"
#include <unistd.h>
#include <iostream>

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), removed_(false) {}

Channel::~Channel() {
    disableAll();
}

void Channel::enableReading() {
    if (removed_) return;  // 已移除则不操作
    events_ |= EPOLLIN | EPOLLPRI;
    update();
}

void Channel::enableWriting() {
    if (removed_) return;  
    events_ |= EPOLLOUT;
    update();
}

void Channel::disableWriting() {
    if (removed_) return;  
    events_ &= ~EPOLLOUT;
    update();
}

void Channel::disableAll() {
    if (removed_) return; 
    events_ = 0;
    update();
}

void Channel::update() {
    if (removed_) {
        std::cerr << "[Channel] update() called on removed channel, fd=" 
                  << fd_ << std::endl;
        return;  // 防止对已关闭fd执行epoll_ctl
    }
    loop_->updateChannel(this);
}

void Channel::remove() {
    removed_ = true;
    loop_->removeChannel(this);
}

void Channel::handleEvent() {
    if (removed_) return;
    
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & (EPOLLERR | EPOLLRDHUP)) {
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (readCallback_) readCallback_();
    }
    if (revents_ & EPOLLOUT) {
        if (writeCallback_) writeCallback_();
    }
}
