#include "net/eventloop.hpp"
#include "net/channel.hpp"
#include <cassert>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>

static int createEventfd() {
    int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        perror("eventfd");
        exit(EXIT_FAILURE);
    }
    return fd;
}

EventLoop::EventLoop()
    : looping_(false), quit_(false),
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      wakeupFd_(createEventfd()),
      wakeupChannel_(new Channel(this, wakeupFd_)),
      threadId_(std::this_thread::get_id()) {
    if (epollfd_ < 0) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleWakeup, this));
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
    wakeupChannel_->disableAll();
    ::close(wakeupFd_);
    ::close(epollfd_);
}

void EventLoop::loop() {
    assert(!looping_);
    assert(isInLoopThread());
    looping_ = true;
    quit_ = false;

    events_.resize(1024);

    while (!quit_) {
        int numEvents = ::epoll_wait(epollfd_, events_.data(), events_.size(), 10000);
        if (numEvents > 0) {
            for (int i = 0; i < numEvents; ++i) {
                Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
                channel->set_revents(events_[i].events);
                channel->handleEvent();
            }
        } else if (numEvents == 0) {
            // 超时，可以处理定时器等
        } else {
            if (errno != EINTR) {
                perror("epoll_wait");
            }
        }
        doPendingFunctors();
    }
    looping_ = false;
}

void EventLoop::quit() {
    quit_ = true;
    if (!isInLoopThread()) {
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingFunctors_.push_back(std::move(cb));
        }
        wakeup();
    }
}

void EventLoop::updateChannel(Channel* channel) {
    assert(isInLoopThread());
    const int index = channel->index();
    if (index < 0) {
        // 新 channel
        struct epoll_event ev;
        ev.events = channel->events();
        ev.data.ptr = channel;
        if (::epoll_ctl(epollfd_, EPOLL_CTL_ADD, channel->fd(), &ev) < 0) {
            perror("epoll_ctl add");
        }
        channel->set_index(1);
    } else {
        struct epoll_event ev;
        ev.events = channel->events();
        ev.data.ptr = channel;
        if (::epoll_ctl(epollfd_, EPOLL_CTL_MOD, channel->fd(), &ev) < 0) {
            perror("epoll_ctl mod");
        }
    }
}

void EventLoop::removeChannel(Channel* channel) {
    assert(isInLoopThread());
    if (channel->index() >= 0) {
        struct epoll_event ev;
        ev.events = 0;
        ev.data.ptr = channel;
        if (::epoll_ctl(epollfd_, EPOLL_CTL_DEL, channel->fd(), &ev) < 0) {
            perror("epoll_ctl del");
        }
        channel->set_index(-1);
    }
}

void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        perror("wakeup write");
    }
}

void EventLoop::handleWakeup() {
    uint64_t one;
    ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        perror("wakeup read");
    }
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }
    for (auto& func : functors) {
        func();
    }
}
