#pragma once

/**
 * 文件描述符事件，是最基础的组件,封装了 epoll 事件
 * 设计目标：
 *  1.封装epoll事件：将底层的epoll事件抽象为易用的接口；
 *  2.支持回调机制：为读写事件提供回调函数
 *  3.协程集成：支持将协程与I/O事件关联；
 *  4.与Reactor集成：自动将事件注册到Reactor的epoll中
*/

#include <iostream>
#include <functional>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <assert.h>
#include <memory>

#include "../comm/log.h"  // 待实现，暂时用cout代替日志
#include "../coroutine/coroutine.h"
#include "reactor.h"  // 待实现
#include "mutex.h"  // 待实现

namespace tinyrpc{

class Reactor;

enum IOEvent {
    READ = EPOLLIN,
    WRITE = EPOLLOUT,
    ETModel = EPOLLET
};

/**
 * 继承 std::enable_shared_from_this 的意义就是让你能够在类的成员函数内部使用 shared_from_this() 获取当前对象的共享指针，
 * 确保对象生命周期的正确管理。这样可以防止由于手动管理原始指针或直接使用 this 指针而导致的生命周期问题
 * 
 * 该类本质上是对事件描述符fd的封装
*/
class FdEvent : public std::enable_shared_from_this<FdEvent> {

protected:
    int m_fd {-1};
    std::function<void()> m_read_callback;  // 读事件的回调函数
    std::function<void()> m_write_callback;  // 写事件的回调函数

    int m_listen_events {0};  // 当前监听的事件类型（READ/WRITE）

    Reactor* m_reactor {nullptr};  // 关联的Reactor对象

    Coroutine* m_coroutine {nullptr};  // 关联的协程指针

public:

    typedef std::shared_ptr<FdEvent> ptr;

    Mutex m_mutex;

    // 接收Reactor和文件描述符
    FdEvent(tinyrpc::Reactor* reactor, int fd = -1);

    // 只接受文件描述符，Reactor可后续设置
    FdEvent(int fd);

    virtual ~FdEvent();

    int getFd() const;

    void setFd(const int fd);

    /*** 事件回调机制 ***/

    // 允许为READ或WRITE事件设置回调函数，这些回调在事件就绪时被调用
    void setCallBack(IOEvent flag, std::function<void()> cb);

    // 获取回调函数
    std::function<void()> getCallBack(IOEvent flag) const;

    // 根据事件类型执行对应的回调函数
    void handleEvent(int flag);

    /*** 事件监听管理 ***/

    // 添加监听事件，将新事件添加到监听列表
    void addListenEvents(IOEvent event);

    // 移除监听事件，同样会更新到Reactor
    void delListenEvents(IOEvent event);

    // 获取监听事件
    int getListenEvents() const;

    /*** 与 Reactor 的集成 ***/

    // 更新到 Reactor ，即注册到epoll
    void updateToReactor();

    // 从 Reactor 注销，即从epoll中移除文件描述符，并清理所有状态
    void unregisterFromReactor();

    Reactor* getReactor() const;

    void setReactor(Reactor* r);

    /*** 非阻塞模式设置 ***/
    void setNonBlock();

    bool isNonBlock();

    /**
     * 协程支持
     * 这些方法允许将协程与 FdEvent 关联
     * 当事件就绪时，Reactor可以通过getCoroutine()获取协程并恢复执行
    */

    void setCoroutine(Coroutine* cor);

    Coroutine* getCoroutine();

    void clearCoroutine();

};


/**
 * 全局容器，管理所有的FdEvent对象
 * 设计特点：
 *  1.预分配：初始化时预分配一定数量的FdEvent对象
 *  2.动态扩容：当请求的fd超出当前容量时，自动扩容1.5倍
 *  3.线程安全：使用读写锁保护并发访问
 *  4.单例模式
*/
class FdEventContainer {

private:
    RWMutex m_mutex;  // 读写锁
    std::vector<FdEvent::ptr> m_fds;  // 存储shared_ptr<FdEvent>对象，即FdEvent对象的智能指针

public:
    FdEventContainer(int size);

    FdEvent::ptr getFdEvent(int fd);

    static FdEventContainer* GetFdContainer();

};

}