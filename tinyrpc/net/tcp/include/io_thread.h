#pragma once

#include <semaphore.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>

#include "coroutine.h"
#include "reactor.h"
#include "tcp_connection_time_wheel.h"

/**
 * 实现了TinyRPC中的IO线程和IO线程池，是网络层的核心组件。
 * 它们负责管理多个IO线程，每个线程运行独立的Reactor事件循环，用于处理TCP连接的IO操作
 */

namespace tinyrpc {

class TcpServer;

class IOThread {
   private:
    Reactor* reactor_{nullptr};  // 该线程持有的Reactor对象，用于事件循环
    std::thread thread_;
    pid_t tid_{-1};                         // 系统线程ID（通过gettid()获取）
    TimerEvent::ptr timer_event_{nullptr};  // 定时事件
    int index_{-1};                         // 线程在线程池中的索引

    sem_t init_semaphore_;  // 初始化信号量，用于同步线程创建

    sem_t start_semaphore_;  // 启动信号量，用于控制线程开始执行

    static void* main(void* arg);

   public:
    typedef std::shared_ptr<IOThread> ptr;

    static IOThread* GetCurrentIOThread();  // 获取当前线程的IOThread对象

    IOThread();

    ~IOThread();

    Reactor* getReactor();  // 获取该线程的Reactor对象

    void addClient(TcpConnection* tcp_conn);  // 将TcpConnection添加到该线程处理

    std::thread::id getThreadId();

    void setThreadIndex(const int index);  // 设置线程索引

    int getThreadIndex();  // 获取线程索引

    sem_t* getStartSemaphore();  // 获取启动信号量
};

class IOThreadPool {
   private:
    int size_{0};  // 线程池大小

    std::atomic<int> index_{-1};  // 原子变量，用于轮询分配线程

    std::vector<IOThread::ptr> io_threads_;  // IOThread只能指针的vec

   public:
    typedef std::shared_ptr<IOThreadPool> ptr;

    IOThreadPool(int size);

    void start();  // 启动所有IO线程

    IOThread* getIOThread();  // 轮询获取下一个可用的IO线程

    int getIOThreadPoolSize();  // 获取线程池大小

    void broadcastTask(std::function<void()> cb);  // 向所有线程广播任务

    void addTaskByIndex(int index, std::function<void()> cb);  // 向指定索引的线程添加任务

    void addCoroutineToRandomThread(Coroutine::ptr cor, bool self = false);  // 将协程添加到随机线程

    void addCoroutineToEachThread(std::function<void()> cb);  // 向每个线程添加协程

    Coroutine::ptr addCoroutineToRandomThread(std::function<void()> cb, bool self = false);

    Coroutine::ptr addCoroutineToThreadByIndex(int index, std::function<void()> cb,
                                               bool self = false);  // 将协程添加到指定线程
};

}  // namespace tinyrpc