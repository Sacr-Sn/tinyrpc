#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#include "coroutine_hook.h"
#include "../comm/config.h"
#include "../net/fd_event.h"
#include "../net/timer.h"

/**
 * 核心机制：函数指针获取
 * 这个宏使用dlsym(RTLD_NEXT, ...)获取 原始系统调用 的函数指针。
 * RTLD_NEXT会查找下一个符号定义，从而获取真正的系统调用而不是hook函数本身。
 * 通过宏定义，获取了5个系统调用的原始函数指针：
 *  g_sys_accept_fun - 原始 accept
 *  g_sys_read_fun - 原始 read
 *  g_sys_write_fun - 原始 write
 *  g_sys_connect_fun - 原始 connect
 *  g_sys_sleep_fun - 原始 sleep
*/
#define HOOK_SYS_FUNC(name) name##_fun_ptr_t g_sys_##name##_fun = (name##_fun_ptr_t)dlsym(RTLD_NEXT, #name);


HOOK_SYS_FUNC(read);
HOOK_SYS_FUNC(write);
HOOK_SYS_FUNC(connect);
HOOK_SYS_FUNC(accept);
HOOK_SYS_FUNC(sleep);

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

static bool g_hook = true;  // 全局hook开关

// 用于开启或关闭hook机制
void SetHook(bool value) {
    g_hook = value;
}

/**
 * 将文件描述符事件注册到epoll
*/
void toEpoll(tinyrpc::FdEvent::ptr fd_event, int events) {
    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();  // 获取当前协程指针
    if (events & tinyrpc::IOEvent::READ) {
        // 将协程设置到FdEvent中
        // 根据事件类型(READ/WRITE)添加监听事件
        // FdEvent会自动调用updateToReactor()注册到epoll
        // TOEDIT
        std::cout << "[toEpoll] : fd[" << fd_event->getFd() << "], register read event to epoll" << std::endl;
        fd_event->setCoroutine(cur_cor);
        fd_event->addListenEvents(tinyrpc::IOEvent::READ);
    }
    if (events & tinyrpc::IOEvent::WRITE) {
        // TOEDIT
        std::cout << "[toEpoll] : fd[" << fd_event->getFd() << "], register write event to epoll" << std::endl;
        fd_event->setCoroutine(cur_cor);
        fd_event->addListenEvents(tinyrpc::IOEvent::WRITE);
    }
}

ssize_t read_hook(int fd, void *buf, size_t count) {
    // TOEDIT
    std::cout << "[read_hook] : thisis hook read" << std::endl;
    // 如果在主协程，直接调用原始系统调用
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        // TOEDIT
        std::cout << "[read_hook] : hook disable, call sys read func" << std::endl;
        return g_sys_read_fun(fd, buf, count);
    }

    /**
     * 获取当前线程的Reactor
     * 从FdEventContainer中获取对应的FdEvent
     * 如果FdEvent没有关联Reactor，设置当前Reactor
    */
    tinyrpc::Reactor::GetReactor();
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(fd);
    if (fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());
    }

    // 设置非阻塞模式
    fd_event->setNonBlock();

    // 尝试立即读取，如果读取成功，直接返回，这样可以避免不必要的协程切换
    ssize_t n = g_sys_read_fun(fd, buf, count);
    if (n > 0) {
        return;
    }

    // 不能立即读取，需要阻塞，则注册事件并yield让出协程
    toEpoll(fd_event, tinyrpc::IOEvent::READ);
    // TOEDIT
    std::cout << "[read_hook] : read func to yield" << std::endl;
    tinyrpc::Coroutine::Yield();

    // 恢复后清理：删除监听事件；清除协程关联
    fd_event->delListenEvents(tinyrpc::IOEvent::READ);
    fd_event->clearCoroutine();

    // 再次读取：协程恢复后，再次调用原始read
    // TOEDIT
    std::cout << "[read_hook] : read func yield back, now to call sys read" << std::endl;
    return g_sys_read_fun(fd, buf, count);
}

ssize_t write_hook(int fd, const void *buf, size_t count) {
    // TOEDIT
    std::cout << "[write_hook] : this is hook write" << std::endl;
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        std::cout << "[write_hook] : hook disable, call sys write func" << std::endl;
        return g_sys_write_fun(fd, buf, count);
    }

    tinyrpc::Reactor::GetReactor();
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(fd);
    if (fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());
    }

    fd_event->setNonBlock();

    ssize_t n = g_sys_write_fun(fd, buf, count);
    if (n > 0) {
        return n;
    }

    toEpoll(fd_event, tinyrpc::IOEvent::WRITE);
    // TOEDIT
    std::cout << "[write_hook] : write func to yield" << std::endl;
    tinyrpc::Coroutine::Yield();

    fd_event->delListenEvents(tinyrpc::IOEvent::WRITE);
    fd_event->clearCoroutine();

    // TOEDIT
    std::cout << "[write_hook] : write func yield back, now to call sys write" << std::endl;
    return g_sys_write_fun(fd, buf, count);
}

int connect_hook(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    // TOEDIT
    std::cout << "[connect_hook] : this is hook connect" << std::endl;
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        // TOEDIT
        std::cout << "[connect_hook] : hook disable, call sys connect func" << std::endl;
        return g_sys_connect_fun(sockfd, addr, addrlen);
    }

    tinyrpc::Reactor* reactor = tinyrpc::Reactor::GetReactor();
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(sockfd);
    if (fd_event->getReactor() == nullptr) {
        fd_event->setReactor(reactor);
    }
    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();

    fd_event->setNonBlock();

    int n = g_sys_connect_fun(sockfd, addr, addrlen);
    if (n == 0) {  // connect成功，直接返回
        // TOEDIT
        std::cout << "[connect_hook] : direct connect succ, return" << std::endl;
        return 0;
    } else if (errno != EINPROGRESS) {  // 连接失败，且不是EINPROGRESS，返回错误
        // TOEDIT
        std::cout << "[connect_hook] : connect error and errno is't EINPROGRESS, errno=" << errno << ", error=" << strerror(errno) << std::endl;
        return n;
    }

    // TOEDIT
    std::cout << "[connect_hook] : errno == EINPROGRESS" << std::endl;

    // 注册WRITE事件，connect完成时，socket会变为可写
    toEpoll(fd_event, tinyrpc::IOEvent::WRITE);

    // 设置超时定时器
    bool is_timeout = false;  // 是否超时
    // 超时函数句柄 -- 创建超时回调，设置超时标志并恢复协程
    auto timeout_cb = [&is_timeout, cur_cor]() {
        // 设置超时标志，然后唤醒协程
        is_timeout = true;
        tinyrpc::Coroutine::Resume(cur_cor);
    }; 
    // 创建TimerEvent，超时时间从配置读取
    tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(gRpcConfig->m_max_connect_timeout, false, timeout_cb);
    tinyrpc::Timer* timer = reactor->getTimer();
    // 添加到Reactor的Timer中
    timer->addTimerEvent(event);

    // 协程会在连接完成或超时时被恢复
    tinyrpc::Coroutine::Yield();

    // write事件需要删除，因为连接成功后后面会重新监听该fd的写事件
    fd_event->delListenEvents(tinyrpc::IOEvent::WRITE);
    fd_event->clearCoroutine();

    // 定时器也需要删除
    timer->delTimerEvent(event);

    n = g_sys_connect_fun(sockfd, addr, addrlen);
    if ((n < 0 && errno == EISCONN) || n == 0) {
        // TOEDIT
        std::cout << "[connect_hook] : connect succ" << std::endl;
        return 0;
    }

    if (is_timeout) {
        // TOEDIT
        std::cout << "[connect_hook] : connect error, timeout[" << gRpcConfig->m_max_connect_timeout << "ms]" << std::endl;
        errno = ETIMEDOUT;
    }
    // TOEDIT
    std::cout << "[connect_hook] : connect error and errno=" << errno << ", error=" << strerror(errno) << std::endl;
    return -1;
}

int accept_hook(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    // TOEDIT
    std::cout << "[accept_hook] : this is hook accept" << std::endl;
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        // TOEDIT
        std::cout << "[accept_hook] : hook disable, call sys accept func" << std::endl;
        return g_sys_accept_fun(sockfd, addr, addrlen);
    }
    // 获取Reactor和FdEvent
    tinyrpc::Reactor::GetReactor();
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(sockfd);
    if (fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());
    }

    // 设置非阻塞
    fd_event->setNonBlock();

    // 尝试accept，避免非必要的协程切换
    int n = g_sys_accept_fun(sockfd, addr, addrlen);

    if (n > 0) {
        return n;
    }

    // 执行到此说明会阻塞，注册READ事件并yield
    toEpoll(fd_event, tinyrpc::IOEvent::READ);

    // TOEDIT
    std::cout << "[accept_hook] : accept func to yield" << std::endl;
    tinyrpc::Coroutine::Yield();

    // 被唤醒恢复后清理并再次accept
    fd_event->delListenEvents(tinyrpc::IOEvent::READ);
    fd_event->clearCoroutine();
    // TOEDIT
    std::cout << "[accept_hook] : accept func yield back, now to call sys accept" << std::endl;
    return g_sys_accept_fun(sockfd, addr, addrlen);
} 

unsigned int sleep_hook(unsigned int seconds) {
    // TOEDIT
    std::cout << "[sleep_hook] : this is hook sleep" << std::endl;
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        // TOEDIT
        std::cout << "[sleep_hook] : hook disable, call sys sleep func" << std::endl;
        return g_sys_sleep_fun(seconds);
    }

    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();

    bool is_timeout = false;
    // 设置超时回调 = 设置超时标志并恢复协程
    auto timeout_cb = [cur_cor, &is_timeout]() {
        // TOEDIT
        std::cout << "[sleep_hook] : onTime, now resume sleep cor" << std::endl;
        is_timeout = true;
        tinyrpc::Coroutine::Resume(cur_cor);
    };

    // 创建定时器事件，单位毫秒，添加到Reactor的Timer
    tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(1000 * seconds, false, timeout_cb);
    tinyrpc::Reactor::GetReactor()->getTimer()->addTimerEvent(event);

    // TOEDIT
    std::cout << "[sleep_hook] : now to yield sleep" << std::endl;
    // 使用yield直到超时，使用while循环确保只有超时才返回
    while (!is_timeout) {
        tinyrpc::Coroutine::Yield();
    }

    return 0;
}


}

extern "C" {

ssize_t read(int fd, void *buf, size_t count) {
    if (!tinyrpc::g_hook) {
        return g_sys_read_fun(fd, buf, count);
    } else {
        return tinyrpc::read_hook(fd, buf, count);
    }
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (!tinyrpc::g_hook) {
        return g_sys_write_fun(fd, buf, count);
    } else {
        return tinyrpc::write_hook(fd, buf, count);
    }
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (!tinyrpc::g_hook) {
        return g_sys_connect_fun(sockfd, addr, addrlen);
    } else {
        return tinyrpc::connect_hook(sockfd, addr, addrlen);
    }
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    if (!tinyrpc::g_hook) {
        return g_sys_accept_fun(sockfd, addr, addrlen);
    } else {
        return tinyrpc::accept_hook(sockfd, addr, addrlen);
    }
}

unsigned int sleep(unsigned int seconds) {
    if (!tinyrpc::g_hook) {
        return g_sys_sleep_fun(seconds);
    } else {
        return tinyrpc::sleep_hook(seconds);
    }
}

}