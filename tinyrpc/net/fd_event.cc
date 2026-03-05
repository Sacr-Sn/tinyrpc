#include <fcntl.h>
#include <unistd.h>

#include "fd_event.h"

namespace tinyrpc {

static FdEventContainer* g_FdContainer = nullptr;

FdEvent::FdEvent(tinyrpc::Reactor* reactor, int fd) : fd_(fd), reactor_(reactor) {
    if (reactor == nullptr) {
        DebugLog << "reactor is nullptr, create it first";
    }
}

FdEvent::FdEvent(int fd) : fd_(fd) {}

FdEvent::~FdEvent() {}

void FdEvent::handleEvent(int flag) {
    if (flag == READ) {
        m_read_callback();
    } else if (flag == WRITE) {
        m_write_callback();
    } else {
        ErrorLog << "error flag";
    }
}

int FdEvent::getFd() const { return fd_; }

void FdEvent::setFd(const int fd) { fd_ = fd; }

void FdEvent::setCallBack(IOEvent flag, std::function<void()> cb) {
    if (flag == READ) {
        m_read_callback = cb;
    } else if (flag == WRITE) {
        m_write_callback = cb;
    } else {
        DebugLog << "[FdEvent::setCallBack] : error flag";
    }
}

std::function<void()> FdEvent::getCallBack(IOEvent flag) const {
    if (flag == READ) {
        return m_read_callback;
    } else if (flag == WRITE) {
        return m_write_callback;
    }
    return nullptr;
}

void FdEvent::addListenEvents(IOEvent event) {
    if (m_listen_events & event) {
        DebugLog << "already has this event, skip";
        return;
    }
    // |= 对两个操作数的每个二进制位执行按位或操作
    m_listen_events |= event;
    updateToReactor();
}

void FdEvent::delListenEvents(IOEvent event) {
    if (m_listen_events & event) {
        m_listen_events &= ~event;
        updateToReactor();
        DebugLog << "[FdEvent::delListenEvents] : delete succ";
        return;
    }
    DebugLog << "[FdEvent::delListenEvents] : this event not exists, skip";
}

int FdEvent::getListenEvents() const { return m_listen_events; }

void FdEvent::updateToReactor() {
    epoll_event event;
    event.events = m_listen_events;
    event.data.ptr = this;
    if (!reactor_) {
        reactor_ = tinyrpc::Reactor::GetReactor();
    }

    reactor_->addEvent(fd_, event);
}

void FdEvent::unregisterFromReactor() {
    if (!reactor_) {
        reactor_ = tinyrpc::Reactor::GetReactor();
    }
    reactor_->delEvent(fd_);
    m_listen_events = 0;
    m_read_callback = nullptr;
    m_write_callback = nullptr;
}

Reactor* FdEvent::getReactor() const { return reactor_; }

void FdEvent::setReactor(Reactor* r) { reactor_ = r; }

void FdEvent::setNonBlock() {
    if (fd_ == -1) {
        ErrorLog << "error, fd=-1";
        return;
    }

    int flag = fcntl(fd_, F_GETFL, 0);
    if (flag & O_NONBLOCK) {
        DebugLog << "fd already set o_nonblock";
        return;
    }

    fcntl(fd_, F_SETFL, flag | O_NONBLOCK);
    flag = fcntl(fd_, F_GETFL, 0);
    if (flag & O_NONBLOCK) {
        DebugLog << "succ set o_nonblock";
    } else {
        ErrorLog << "set o_nonblock error";
    }
}

bool FdEvent::isNonBlock() {
    if (fd_ == -1) {
        ErrorLog << "error, fd=-1";
        return false;
    }
    int flag = fcntl(fd_, F_GETFL, 0);
    return (flag & O_NONBLOCK);
}

void FdEvent::setCoroutine(Coroutine* cor) { m_coroutine = cor; }

void FdEvent::clearCoroutine() { m_coroutine = nullptr; }

Coroutine* FdEvent::getCoroutine() { return m_coroutine; }

FdEvent::ptr FdEventContainer::getFdEvent(int fd) {
    // 两阶段检查，处理读锁与写锁的切换问题
    // 第一阶段：用读锁快速检查是否需要写
    {
        std::shared_lock<std::shared_mutex> read_lock(rw_mtx_);
        if (fd < static_cast<int>(fds_.size())) {
            // 不需写操作（数组扩容），仅进行读操作
            tinyrpc::FdEvent::ptr re = fds_[fd];
            return re;
        }
    }  // 读锁在此释放
    // 第二阶段：重新上写锁，并再次检查（因为状态可能已变）
    std::unique_lock<std::shared_mutex> write_lock(rw_mtx_);
    if (!(fd < static_cast<int>(fds_.size()))) {
        // 需要进行写操作
        int n = (int)(fd * 1.5);  // 容量不足，fds扩容
        for (int i = fds_.size(); i < n; i++) {
            fds_.push_back(std::make_shared<FdEvent>(i));
        }
        tinyrpc::FdEvent::ptr re = fds_[fd];
        return re;
    } else {
        // 可能中途被其它线程扩容后，不再需要扩容，仅读即可
        tinyrpc::FdEvent::ptr re = fds_[fd];
        return re;
    }
}

FdEventContainer::FdEventContainer(int size) {
    for (int i = 0; i < size; i++) {
        fds_.push_back(std::make_shared<FdEvent>(i));
    }
}

FdEventContainer* FdEventContainer::GetFdContainer() {
    if (g_FdContainer == nullptr) {
        g_FdContainer = new FdEventContainer(1000);
    }
    return g_FdContainer;
}

}  // namespace tinyrpc