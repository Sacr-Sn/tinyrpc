#include <assert.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <algorithm>
#include <iostream>

#include "coroutine_hook.h"
#include "log.h"
#include "reactor.h"
#include "timer.h"

extern read_fun_ptr_t g_sys_read_fun;    // 原始系统调用函数指针，避免协程hook
extern write_fun_ptr_t g_sys_write_fun;  // 原始系统调用函数指针，避免协程hook

namespace tinyrpc {

static thread_local Reactor* t_reactor_ptr = nullptr;  // 每个线程有独立的Reactor

static thread_local int t_max_epoll_timeout = 10000;  // epoll_wait的最大超时时间(单位为ms)

static CoroutineTaskQueue* t_coroutine_task_queue = nullptr;  // 全局协程任务队列

Reactor::Reactor() {
    // 确保每个线程只能创建一个Reactor实例
    if (t_reactor_ptr != nullptr) {
        DebugLog << "this thread has already create a reactor";
        Exit(0);
    }

    tid_ = gettid();  // 获取当前线程ID
    DebugLog << "thread[" << tid_ << "] succ create a reactor";
    t_reactor_ptr = this;

    // 创建epoll实例
    if ((epfd_ = epoll_create(1)) <= 0) {
        DebugLog << "epoll_create error, sys error=" << strerror(errno);
        Exit(0);
    } else {
        DebugLog << "epfd_ = " << epfd_;
    }

    // 创建唤醒文件描述符，用于跨线程唤醒Reactor
    if ((wake_fd_ = eventfd(0, EFD_NONBLOCK)) <= 0) {
        DebugLog << "event_fd error, sys error=" << strerror(errno);
        Exit(0);
    }
    DebugLog << "[Reactor::Reactor] : wakefd = " << wake_fd_;

    // 添加 wakeup fd 到 epoll
    addWakeupFd();
}

Reactor::~Reactor() {
    DebugLog << " ~Reactor";
    close(epfd_);
    if (timer_ != nullptr) {
        delete timer_;
        timer_ = nullptr;
    }
    t_reactor_ptr = nullptr;
}

Reactor* Reactor::GetReactor() {
    if (t_reactor_ptr == nullptr) {  // 懒加载
        DebugLog << "create new Reacctor";
        t_reactor_ptr = new Reactor();
    }
    return t_reactor_ptr;
}

void Reactor::addEvent(int fd, epoll_event event, bool is_wakeup) {
    if (fd == -1) {
        DebugLog << "add error. if invalid, fd = -1";
        return;
    }
    // 判断是否在Reactor线程
    if (isLoopThread()) {  // 在Reactor线程，直接调用
        addEventInLoopThread(fd, event);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        // 将时间加入待添加队列
        pending_add_fds_.insert(std::pair<int, epoll_event>(fd, event));
    }
    if (is_wakeup) {
        wakeup();  // 唤醒事件循环
    }
}

void Reactor::delEvent(int fd, bool is_wakeup) {
    if (fd == -1) {
        DebugLog << "add error. if invalid, fd = -1";
        return;
    }

    if (isLoopThread()) {  // 判断是否在Reactor线程
        delEventInLoopThread(fd);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        // 将fd加入待删除队列
        pending_del_fds_.push_back(fd);
    }

    if (is_wakeup) {
        wakeup();
    }
}

void Reactor::wakeup() {
    // 检查是否正在循环中，没有则直接返回
    if (!is_looping_) {
        return;
    }

    uint64_t tmp = 1;
    uint64_t* p = &tmp;
    /**
     * 向wakeup fd写入8字节数据
     * 使用原始系统调用g_sys_write_fun避免协程hook
     * 写入会触发epoll_wait返回，从而唤醒Reactor
     */
    if (g_sys_write_fun(wake_fd_, p, 8) != 8) {
        ErrorLog << "write wakeupfd[" << wake_fd_ << "] error";
    }
}

bool Reactor::isLoopThread() const {
    // 通过比较Reactor的线程ID和当前线程ID来判断
    return tid_ == gettid();  // 注意不是本类中的getTid()
}

// 添加唤醒fd到epoll，将wakeup fd注册到epoll，监听EPOLLIN事件
void Reactor::addWakeupFd() {
    int op = EPOLL_CTL_ADD;
    epoll_event event;
    event.data.fd = wake_fd_;
    event.events = EPOLLIN;
    if ((epoll_ctl(epfd_, op, wake_fd_, &event)) != 0) {
        ErrorLog << "epoll_ctl error, fd[" << wake_fd_ << "], errno=" << errno << ", err=" << strerror(errno);
    }
    fds_.push_back(wake_fd_);
}

// 在Reactor线程内添加事件
void Reactor::addEventInLoopThread(int fd, epoll_event event) {
    assert(isLoopThread());  // 确保在Reactor线程中调用

    int op = EPOLL_CTL_ADD;
    bool is_add = true;
    auto it = find(fds_.begin(), fds_.end(), fd);
    // 如果fd已存在，使用EPOLL_CTL_MOD修改
    if (it != fds_.end()) {
        is_add = false;
        op = EPOLL_CTL_MOD;
    }

    // 如果fd不存在，使用EPOLL_CTL_ADD添加
    // 调用epoll_ctl将事件注册到epoll
    if (epoll_ctl(epfd_, op, fd, &event) != 0) {
        ErrorLog << "epoo_ctl error, fd[" << fd << "], sys errinfo = " << strerror(errno);
        return;
    }
    // 如果是新添加的fd，加入fds_向量
    if (is_add) {
        fds_.push_back(fd);
    }
    DebugLog << "epoll_ctl add succ, fd[" << fd << "]";
}

// 在Reactor线程内删除事件
void Reactor::delEventInLoopThread(int fd) {
    assert(isLoopThread());

    // 查找要删除的fd在fds_中是否存在
    auto it = find(fds_.begin(), fds_.end(), fd);
    if (it == fds_.end()) {
        DebugLog << "fd[" << fd << "] not in this loop";
        return;
    }

    int op = EPOLL_CTL_DEL;
    // 调用epoll_ctl，从epoll中删除
    if ((epoll_ctl(epfd_, op, fd, nullptr)) != 0) {
        ErrorLog << "epoo_ctl error, fd[" << fd << "], sys errinfo = " << strerror(errno);
    }

    // 更新fd列表，从fds_中移除
    fds_.erase(it);
    DebugLog << "del succ, fd[" << fd << "]";
}

// 核心事件循环
void Reactor::loop() {
    assert(isLoopThread());  // 确保在Reactor线程中
    if (is_looping_) {       // 检查是否已经在循环中
        return;
    }

    is_looping_ = true;
    stop_flag_ = false;

    // 如果有待恢复的协程，直接Resume而不加入全局队列，减少锁竞争
    Coroutine* first_coroutine = nullptr;
    while (!stop_flag_) {
        const int MAX_EVENTS = 10;
        epoll_event re_events[MAX_EVENTS + 1];

        if (first_coroutine) {
            tinyrpc::Coroutine::Resume(first_coroutine);
            first_coroutine = NULL;
        }

        // 处理全局协程任务队列（仅SubReactor）
        if (reactor_type_ != MainReactor) {
            FdEvent* ptr = NULL;
            while (1) {
                // SubReactor从CoroutineTaskQueue中取出待执行的协程
                ptr = CoroutineTaskQueue::GetCoroutineTaskQueue()->pop();
                if (ptr) {
                    // 设置协程的Reactor并恢复执行
                    ptr->setReactor(this);
                    tinyrpc::Coroutine::Resume(ptr->getCoroutine());
                } else {
                    break;
                }
            }
        }

        // 执行待处理任务
        std::vector<std::function<void()>> tmp_tasks;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            tmp_tasks.swap(pending_tasks_);  // 减少锁持有时间
        }

        for (size_t i = 0; i < tmp_tasks.size(); i++) {
            if (tmp_tasks[i]) {
                tmp_tasks[i]();
            }
        }

        // epoll_wait事件，最多等待10秒，返回就绪的事件数量
        int rt = epoll_wait(epfd_, re_events, MAX_EVENTS, t_max_epoll_timeout);
        if (rt < 0) {
            ErrorLog << "epoll_wait error, skip, errno=" << strerror(errno);
        } else {  // 处理就绪事件
            for (int i = 0; i < rt; i++) {
                epoll_event one_event = re_events[i];
                if (one_event.data.fd == wake_fd_ && (one_event.events & READ)) {  // 处理wakeup事件
                    char buf[8];
                    // 清空eventfd，避免重复触发
                    while (1) {
                        if ((g_sys_read_fun(wake_fd_, buf, 8) == -1) && errno == EAGAIN) {
                            break;
                        }
                    }
                } else {  // 处理普通I/O事件
                    tinyrpc::FdEvent* ptr = (tinyrpc::FdEvent*)one_event.data.ptr;
                    if (ptr != nullptr) {
                        int fd = ptr->getFd();
                        if ((!(one_event.events & EPOLLIN)) && (!(one_event.events & EPOLLOUT))) {  // 未知事件
                            ErrorLog << "socket [" << fd << "] occur other unknow event:[" << one_event.events
                                     << "], need unregister this socket";
                            delEventInLoopThread(fd);  // 注销未知事件
                        } else {
                            if (ptr->getCoroutine()) {  // 如果FdEvent关联了协程
                                if (!first_coroutine) {
                                    // 第一个协程直接保存，后续在循环开始时恢复
                                    first_coroutine = ptr->getCoroutine();
                                    continue;
                                }
                                if (reactor_type_ == SubReactor) {
                                    // SubReactor将协程加入全局队列
                                    delEventInLoopThread(fd);
                                    ptr->setReactor(NULL);
                                    CoroutineTaskQueue::GetCoroutineTaskQueue()->push(ptr);
                                } else {
                                    // MainReactor直接恢复协程（用于accept）
                                    tinyrpc::Coroutine::Resume(ptr->getCoroutine());
                                    if (first_coroutine) {
                                        first_coroutine = NULL;
                                    }
                                }
                            } else {  // 如果没有关联协程
                                std::function<void()> read_cb;
                                std::function<void()> write_cb;
                                read_cb = ptr->getCallBack(READ);
                                write_cb = ptr->getCallBack(WRITE);
                                if (fd == timer_fd_) {  // Timer事件直接执行回调
                                    read_cb();
                                    continue;
                                }
                                // 其它事件将回调加入任务队列
                                if (one_event.events & EPOLLIN) {
                                    std::lock_guard<std::mutex> lock(mtx_);
                                    pending_tasks_.push_back(read_cb);
                                }
                                if (one_event.events & EPOLLOUT) {
                                    std::lock_guard<std::mutex> lock(mtx_);
                                    pending_tasks_.push_back(write_cb);
                                }
                            }
                        }
                    }
                }
            }

            // 处理待添加/删除的事件
            std::map<int, epoll_event> tmp_add;
            std::vector<int> tmp_del;
            {
                std::lock_guard<std::mutex> lock(mtx_);
                tmp_add.swap(pending_add_fds_);  // swap减少锁占用时间
                pending_add_fds_.clear();

                tmp_del.swap(pending_del_fds_);
                pending_del_fds_.clear();
            }
            for (auto i = tmp_add.begin(); i != tmp_add.end(); i++) {
                addEventInLoopThread((*i).first, (*i).second);
            }
            for (auto i = tmp_del.begin(); i != tmp_del.end(); i++) {
                delEventInLoopThread((*i));
            }
        }
    }
    DebugLog << "[Reactor::loop] : reactor loop end";
    is_looping_ = false;
}

void Reactor::stop() {
    if (!stop_flag_ && is_looping_) {
        stop_flag_ = true;
        wakeup();
    }
}

void Reactor::addTask(std::function<void()> task, bool is_wakeup) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        pending_tasks_.push_back(task);
    }
    if (is_wakeup) {
        wakeup();
    }
}

void Reactor::addTask(std::vector<std::function<void()>> task, bool is_wakeup) {
    if (task.size() == 0) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        pending_tasks_.insert(pending_tasks_.end(), task.begin(), task.end());
    }
    if (is_wakeup) {
        wakeup();
    }
}

// 添加协程，将协程的Resume操作包装成任务，调用addTask()
void Reactor::addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup) {
    auto func = [cor]() { tinyrpc::Coroutine::Resume(cor.get()); };
    addTask(func, is_wakeup);
}

Timer* Reactor::getTimer() {
    if (!timer_) {
        timer_ = new Timer(this);
        timer_fd_ = timer_->getFd();
    }
    return timer_;
}

pid_t Reactor::getTid() { return tid_; }

void Reactor::setReactorType(ReactorType type) { reactor_type_ = type; }

CoroutineTaskQueue* CoroutineTaskQueue::GetCoroutineTaskQueue() {
    if (t_coroutine_task_queue) {
        return t_coroutine_task_queue;
    }
    t_coroutine_task_queue = new CoroutineTaskQueue();
    return t_coroutine_task_queue;
}

void CoroutineTaskQueue::push(FdEvent* cor) {
    std::lock_guard<std::mutex> lock(mtx_);
    task_.push(cor);
}

FdEvent* CoroutineTaskQueue::pop() {
    FdEvent* re = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (task_.size() >= 1) {
            re = task_.front();
            task_.pop();
        }
    }
    return re;
}

}  // namespace tinyrpc