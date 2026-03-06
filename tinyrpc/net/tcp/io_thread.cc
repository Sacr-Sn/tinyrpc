#include <semaphore.h>
#include <stdlib.h>
#include <time.h>

#include <tinyrpc/comm/config.h>
#include <tinyrpc/coroutine/coroutine_pool.h>
#include <tinyrpc/net/tcp/io_thread.h>
#include <tinyrpc/net/tcp/tcp_connection.h>
#include <tinyrpc/net/tcp/tcp_server.h>

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

static thread_local Reactor* t_reactor_ptr = nullptr;  // 当前线程的Reactor指针

static thread_local IOThread* t_cur_io_thread = nullptr;  // 当前线程的IOThread对象指针

IOThread::IOThread() {
    int rt = sem_init(&init_semaphore_, 0, 0);  // 初始化信号量
    assert(rt == 0);

    rt = sem_init(&start_semaphore_, 0, 0);  // 初始化信号量
    assert(rt == 0);

    // 创建新线程，入口函数为main
    thread_ = std::thread(&IOThread::main, this);

    DebugLog << "semaphore begin to wait until new thread finish IOThread::main() to init";
    // 等待新线程完成初始化
    rt = sem_wait(&init_semaphore_);
    assert(rt == 0);
    DebugLog << "semaphore wait end, finish create io thread";

    // 销毁初始化信号量,确保了在构造函数返回时,新线程已经完成了基本初始化。
    sem_destroy(&init_semaphore_);
}

IOThread::~IOThread() {
    reactor_->stop();
    if (thread_.joinable()) {
        thread_.join();
    }

    if (reactor_ != nullptr) {
        delete reactor_;
        reactor_ = nullptr;
    }
}

IOThread* IOThread::GetCurrentIOThread() { return t_cur_io_thread; }

sem_t* IOThread::getStartSemaphore() { return &start_semaphore_; }

Reactor* IOThread::getReactor() { return reactor_; }

std::thread::id IOThread::getThreadId() { return thread_.get_id(); }

void IOThread::setThreadIndex(const int index) { index_ = index; }

int IOThread::getThreadIndex() { return index_; }

// 线程入口函数
void* IOThread::main(void* arg) {
    // 为该线程创建新的Reactor对象
    t_reactor_ptr = new Reactor();
    assert(t_reactor_ptr != NULL);

    // 设置当前线程的IOThread和Reactor指针
    IOThread* thread = static_cast<IOThread*>(arg);
    t_cur_io_thread = thread;
    thread->reactor_ = t_reactor_ptr;
    thread->reactor_->setReactorType(SubReactor);
    thread->tid_ = gettid();  // 获取系统线程ID

    Coroutine::GetCurrentCoroutine();

    DebugLog << "finish iothread init, now post semaphore";
    sem_post(&thread->init_semaphore_);  // 通知主线程

    // 等待主线程发出启动信号
    sem_wait(&thread->start_semaphore_);

    sem_destroy(&thread->start_semaphore_);

    DebugLog << "IOThread " << thread->tid_ << " begin to loop";
    t_reactor_ptr->loop();  // 进入事件循环，开始处理事件

    return nullptr;
}

// 添加客户端连接 - 将TcpConnection添加到该IO线程处理
void IOThread::addClient(TcpConnection* tcp_conn) {
    tcp_conn->registerToTimeWheel();  // 将连接注册到时间轮
    tcp_conn->setUpServer();          // 设置服务端连接
    return;
}

IOThreadPool::IOThreadPool(int size) : size_(size) {
    io_threads_.resize(size);
    for (int i = 0; i < size; i++) {
        io_threads_[i] = std::make_shared<IOThread>();
        io_threads_[i]->setThreadIndex(i);
    }
}

// 启动所有线程
void IOThreadPool::start() {
    for (int i = 0; i < size_; i++) {
        int rt = sem_post(io_threads_[i]->getStartSemaphore());
        assert(rt == 0);
    }
}

// 轮询获取线程
IOThread* IOThreadPool::getIOThread() {
    if (index_ == size_ || index_ == -1) {
        index_ = 0;
    }
    return io_threads_[index_++].get();
}

int IOThreadPool::getIOThreadPoolSize() { return size_; }

// 广播任务到所有线程
void IOThreadPool::broadcastTask(std::function<void()> cb) {
    for (auto i : io_threads_) {
        i->getReactor()->addTask(cb, true);
    }
}

// 向指定线程添加任务
void IOThreadPool::addTaskByIndex(int index, std::function<void()> cb) {
    if (index >= 0 && index < size_) {
        io_threads_[index]->getReactor()->addTask(cb, true);
    }
}

// 允许协程在不同的IO线程间调度，实现m:n协程模型
void IOThreadPool::addCoroutineToRandomThread(Coroutine::ptr cor, bool self) {
    if (size_ == 1) {
        io_threads_[0]->getReactor()->addCoroutine(cor, true);
        return;
    }
    srand(time(0));
    int i = 0;
    while (1) {
        i = rand() % (size_);  // 生成随机索引
        // 确保不选择当前线程
        if (!self && io_threads_[i]->getThreadId() == t_cur_io_thread->getThreadId()) {
            i++;
            if (i == size_) {
                i -= 2;
            }
        }
        break;
    }
    // 调用选中线程的Reactor的addCoroutine()方法
    io_threads_[i]->getReactor()->addCoroutine(cor, true);
}

// 从协程池中获取协程，设置回调函数，然后添加到随机线程
Coroutine::ptr IOThreadPool::addCoroutineToRandomThread(std::function<void()> cb, bool self) {
    Coroutine::ptr cor = GetCoroutinePool()->getCoroutineInstance();
    cor->setCallBack(cb);
    addCoroutineToRandomThread(cor, self);
    return cor;
}

// 从协程池获取协程、设置回调，并添加到指定线程
Coroutine::ptr IOThreadPool::addCoroutineToThreadByIndex(int index, std::function<void()> cb, bool self) {
    if (index >= (int)io_threads_.size() || index < 0) {
        ErrorLog << "addCoroutineToThreadByIndex error, invalid iothread index[" << index << "]";
        return nullptr;
    }
    Coroutine::ptr cor = GetCoroutinePool()->getCoroutineInstance();
    cor->setCallBack(cb);
    io_threads_[index]->getReactor()->addCoroutine(cor, true);
    return cor;
}

// 遍历所有线程，为每个线程创建新的协程并添加
void IOThreadPool::addCoroutineToEachThread(std::function<void()> cb) {
    for (auto i : io_threads_) {
        Coroutine::ptr cor = GetCoroutinePool()->getCoroutineInstance();
        cor->setCallBack(cb);
        i->getReactor()->addCoroutine(cor, true);
    }
}

}  // namespace tinyrpc