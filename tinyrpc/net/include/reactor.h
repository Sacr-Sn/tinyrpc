#pragma once

#include <sys/socket.h>
#include <sys/types.h>
#include <atomic>
#include <functional>
#include <map>
#include <queue>
#include <vector>

#include "coroutine.h"
#include "fd_event.h"

namespace tinyrpc {

/**
 * 两种Reactor类型
 * 实现了连接接收和数据处理的解耦
 */
enum ReactorType {
    MainReactor = 1,  // 主线程使用，主要负责接收新连接
    SubReactor = 2    // IO线程使用，负责处理已建立连接的数据读写
};

class FdEvent;
class Timer;

/**
 * Reactor 是 TinyRPC 的事件驱动核心,它将 epoll、定时器、协程、任务队列统一管理在一个事件循环中。
 * 通过 MainReactor 和 SubReactor 的分离,实现了连接接受和数据处理的解耦。
 * Reactor 与 FdEvent、Timer、Coroutine 深度集成,形成了高效的异步 I/O 处理架构。
 * 每个线程有独立的 Reactor 实例,通过 eventfd 实现线程间通信,这种设计使得 TinyRPC 能够高效处理大量并发连接。
 */
class Reactor {
   private:
    int epfd_{-1};      // epoll文件描述符，用于监听所有注册的事件
    int wake_fd_{-1};   // 唤醒文件描述符(eventfd)，用于跨线程唤醒Reactor
    int timer_fd_{-1};  // 定时器文件描述符，由Timer管理

    bool stop_flag_{false};   // 控制事件循环的状态标志
    bool is_looping_{false};  // 控制事件循环的状态标志
    bool is_init_timer_{false};

    pid_t tid_{0};  // Reactor所属线程的ID

    std::mutex mtx_;  // 保护并发访问的互斥锁

    std::vector<int> fds_;
    std::atomic<int> fd_size_;

    // 1 -- to add to loop
    // 2 -- to del from loop
    std::map<int, epoll_event> pending_add_fds_;        // 待添加的文件描述符队列
    std::vector<int> pending_del_fds_;                  // 待删除的文件描述符队列
    std::vector<std::function<void()>> pending_tasks_;  // 待执行的任务队列

    Timer* timer_{nullptr};  // 定时器对象指针

    ReactorType reactor_type_{SubReactor};  // Reactor类型

    // 将wakeup fd添加到epoll
    void addWakeupFd();

    // 判断当前线程是否是Reactor所属线程
    bool isLoopThread() const;

    // 添加事件到epoll
    void addEventInLoopThread(int fd, epoll_event event);

    // 从epoll删除事件
    void delEventInLoopThread(int fd);

   public:
    typedef std::shared_ptr<Reactor> ptr;

    // explicit 修饰构造函数时，防止了编译器进行隐式类型转换
    explicit Reactor();

    ~Reactor();

    /**
     * 向epoll添加文件描述符事件
     * is_wakeup参数控制是否立即唤醒Reactor
     * 支持跨线程调用，会自动加锁保护
     */
    void addEvent(int fd, epoll_event event, bool is_wakeup = true);

    // 从epoll删除文件描述符事件
    void delEvent(int fd, bool is_wakeup = true);

    // 添加单个或多个任务到任务队列
    void addTask(std::function<void()> task, bool is_wakeup = true);
    void addTask(std::vector<std::function<void()>> task, bool is_wakeup = true);

    /**
     * 添加协程到执行队列
     * 实际上是将协程的Resume操作包装成任务
     */
    void addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup = true);

    // 唤醒正在epoll_wait的Reactor
    void wakeup();

    // 启动事件循环（这是Reactor的核心）
    void loop();

    // 停止事件循环
    void stop();

    // 获取Timer实例（懒初始化）
    Timer* getTimer();

    pid_t getTid();  // 获取Reactor所属线程ID

    // 设置Reactor类型
    void setReactorType(ReactorType type);

    /**
     * 获取当前线程的Reactor实例
     *  使用thread_local存储，每个线程有独立的Reactor
     *  如果不存在会自动创建
     */
    static Reactor* GetReactor();
};

/**
 * 全局的协程任务队列
 * 用于在不同IO线程间调度协程
 * 使用互斥锁保护并发访问
 * SubReactor会定期从中取出协程执行
 */
class CoroutineTaskQueue {
   private:
    std::queue<FdEvent*> task_;
    std::mutex mtx_;

   public:
    static CoroutineTaskQueue* GetCoroutineTaskQueue();

    void push(FdEvent* fd);

    FdEvent* pop();
};

}  // namespace tinyrpc