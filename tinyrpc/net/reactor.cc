#include <iostream>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <assert.h>
#include <string.h>
#include <algorithm>

#include "../comm/log.h"
#include "reactor.h"
#include "timer.h"
#include "../coroutine/coroutine_hook.h"

extern read_fun_ptr_t g_sys_read_fun;  // 原始系统调用函数指针，避免协程hook
extern write_fun_ptr_t g_sys_write_fun;  // 原始系统调用函数指针，避免协程hook

namespace tinyrpc {

static thread_local Reactor* t_reactor_ptr = nullptr;  // 每个线程有独立的Reactor

static thread_local int t_max_epoll_timeout = 10000;  // epoll_wait的最大超时时间(单位为ms)

static CoroutineTaskQueue* t_coroutine_task_queue = nullptr;  // 全局协程任务队列

Reactor::Reactor() {
    // 确保每个线程只能创建一个Reactor实例
    if (t_reactor_ptr != nullptr) {
        // TOEDIT
        std::cout << "[Reactor::Reactor] : this thread has already create a reactor" << std::endl;
        // TODO 在log.cc中实现该方法，然后放开注释
        // Exit(0);  
    }

    m_tid = gettid();  // 获取当前线程ID
    // TOEDIT
    std::cout << "[Reactor::Reactor] : thread[" << m_tid << "] succ create a reactor" << std::endl;
    t_reactor_ptr = this;

    // 创建epoll实例
    if ((m_epfd = epoll_create(1)) <= 0) {
        // TOEDIT
        std::cout << "[Reactor::Reactor] : epoll_create error, sys error=" << strerror(errno) << std::endl;
        // TODO 在log.cc中实现该方法，然后放开注释
        // Exit(0);
    } else {
        // TOEDIT
        std::cout << "[Reactor::Reactor] : m_epfd = " << m_epfd << std::endl;
    }

    // 创建唤醒文件描述符，用于跨线程唤醒Reactor
    if ((m_wake_fd = eventfd(0, EFD_NONBLOCK)) <= 0) {
        // TOEDIT
        std::cout << "[Reactor::Reactor] : event_fd error, sys error=" << strerror(errno) << std::endl;
        // TODO 在log.cc中实现该方法，然后放开注释
        // Exit(0);
    }
    // TOEDIT
    std::cout << "[Reactor::Reactor] : wakefd = " << m_wake_fd << std::endl;

    // 添加 wakeup fd 到 epoll
    addWakeupFd();
}

Reactor::~Reactor() {
    // TOEDIT
    std::cout << "[Reactor::~Reactor] : ~Reactor" << std::endl;
    close(m_epfd);
    if (m_timer != nullptr) {
        delete m_timer;
        m_timer = nullptr;
    }
    t_reactor_ptr = nullptr;
}

Reactor* Reactor::GetReactor() {
    if (t_reactor_ptr == nullptr) {  // 懒加载
        // TOEDIT
        std::cout << "[Reactor::GetReactor] : create new Reacctor" << std::endl;
        t_reactor_ptr = new Reactor();
    }
    return t_reactor_ptr;
}

void Reactor::addEvent(int fd, epoll_event event, bool is_wakeup) {
    if (fd = -1) {
        // TOEDIT
        std::cout << "[Reactor::addEvent] : add error. if invalid, fd = -1" << std::endl;
        return;
    }
    // 判断是否在Reactor线程
    if (isLoopThread()) {  // 在Reactor线程，直接调用
        addEventInLoopThread(fd, event);
        return;
    }
    {
        Mutex::Lock lock(m_mutex);
        // 将时间加入待添加队列
        m_pending_add_fds.insert(std::pair<int, epoll_event>(fd, event));
    }
    if (is_wakeup) {
        wakeup();  // 唤醒事件循环
    }
} 

void Reactor::delEvent(int fd, bool is_wakeup) {
    if (fd = -1) {
        // TOEDIT
        std::cout << "[Reactor::addEvent] : add error. if invalid, fd = -1" << std::endl;
        return;
    }

    if (isLoopThread()) {  // 判断是否在Reactor线程
        delEventInLoopThread(fd);
        return;
    }

    {
        Mutex::Lock lock(m_mutex);
        // 将fd加入待删除队列
        m_pending_del_fds.push_back(fd);
    }

    if (is_wakeup) {
        wakeup();
    }
}

void Reactor::wakeup() {
    // 检查是否正在循环中，没有则直接返回
    if (!m_is_looping) {
        return;
    }

    uint64_t tmp = 1;
    uint64_t* p = &tmp;
    /**
     * 向wakeup fd写入8字节数据
     * 使用原始系统调用g_sys_write_fun避免协程hook
     * 写入会触发epoll_wait返回，从而唤醒Reactor
    */
    if (g_sys_write_fun(m_wake_fd, p, 8) != 8) {
        // TOEDIT
        std::cerr << "[Reactor::wakeup] : write wakeupfd[" << m_wake_fd << "] error" << std::endl;
    }
}

bool Reactor::isLoopThread() const {
    // 通过比较Reactor的线程ID和当前线程ID来判断
    return m_tid == gettid();  // 注意不是本类中的getTid()
}

// 添加唤醒fd到epoll，将wakeup fd注册到epoll，监听EPOLLIN事件
void Reactor::addWakeupFd() {
    int op = EPOLL_CTL_ADD;
    epoll_event event;
    event.data.fd = m_wake_fd;
    event.events = EPOLLIN;
    if ((epoll_ctl(m_epfd, op, m_wake_fd, &event)) != 0) {
        // TOEDIT
        std::cerr << "[Reactor::addWakeupFd] : epoll_ctl error, fd[" << m_wake_fd << "], errno=" << errno << ", err=" << strerror(errno) << std::endl;
    }
    m_fds.push_back(m_wake_fd);
}

// 在Reactor线程内添加事件
void Reactor::addEventInLoopThread(int fd, epoll_event event) {
    assert(isLoopThread());  // 确保在Reactor线程中调用

    int op = EPOLL_CTL_ADD;
    bool is_add = true;
    auto it = find(m_fds.begin(), m_fds.end(), fd);
    // 如果fd已存在，使用EPOLL_CTL_MOD修改
    if (it != m_fds.end()) {
        is_add = false;
        op = EPOLL_CTL_MOD;
    }

    // 如果fd不存在，使用EPOLL_CTL_ADD添加
    // 调用epoll_ctl将事件注册到epoll
    if (epoll_ctl(m_epfd, op, fd, &event) != 0) {
        // TOEDIT
        std::cerr << "[Reactor::addEventInLoopThread] : epoo_ctl error, fd[" << fd << "], sys errinfo = " << strerror(errno) << std::endl;
        return;
    }
    // 如果是新添加的fd，加入m_fds向量
    if (is_add) {
        m_fds.push_back(fd);
    }
    // TOEDIT
    std::cout << "[Reactor::addEventInLoopThread] : epoll_ctl add succ, fd[" << fd << "]" << std::endl;
}

// 在Reactor线程内删除事件
void Reactor::delEventInLoopThread(int fd) {
    assert(isLoopThread());

    // 查找要删除的fd在m_fds中是否存在
    auto it = find(m_fds.begin(), m_fds.end(), fd);
    if (it == m_fds.end()) {
        // TOEDIT
        std::cout << "[Reactor::delEventInLoopThread] : fd[" << fd << "] not in this loop" << std::endl;
        return;
    }
    
    int op = EPOLL_CTL_DEL;
    // 调用epoll_ctl，从epoll中删除
    if ((epoll_ctl(m_epfd, op, fd, nullptr)) != 0) {
        // TOEDIT
        std::cerr << "[Reactor::delEventInLoopThread] : epoo_ctl error, fd[" << fd << "], sys errinfo = " << strerror(errno) << std::endl;
    }

    // 更新fd列表，从m_fds中移除 
    m_fds.erase(it);
    // TOEDIT
    std::cout << "[Reactor::delEventInLoopThread] : del succ, fd[" << fd << "]" << std::endl;
}

// 核心事件循环
void Reactor::loop() {
    assert(isLoopThread());  // 确保在Reactor线程中
    if (m_is_looping) {  // 检查是否已经在循环中
        return;
    }

    m_is_looping = true;
    m_stop_flag = false;

    // 如果有待恢复的协程，直接Resume而不加入全局队列，减少锁竞争
    Coroutine* first_coroutine = nullptr;
    while (!m_stop_flag) {
        const int MAX_EVENTS = 10;
        epoll_event re_events[MAX_EVENTS + 1];

        if (first_coroutine) {
            tinyrpc::Coroutine::Resume(first_coroutine);
            first_coroutine = NULL;
        }

        // 处理全局协程任务队列（仅SubReactor）
        if (m_reactor_type != MainReactor) {
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
        Mutex::Lock lock(m_mutex);
        std::vector<std::function<void()>> tmp_tasks;
        tmp_tasks.swap(m_pending_tasks);  // 减少锁持有时间
        lock.unlock();
        for (size_t i=0; i<tmp_tasks.size(); i++) {
            if (tmp_tasks[i]) {
                tmp_tasks[i]();
            }
        }

        // epoll_wait事件，最多等待10秒，返回就绪的事件数量
        int rt = epoll_wait(m_epfd, re_events, MAX_EVENTS, t_max_epoll_timeout);
        if (rt < 0) {
            // TOEDIT
            std::cerr << "[Reactor::loop] : epoll_wait error, skip, errno=" << strerror(errno) << std::endl;
        } else {  // 处理就绪事件
            for (int i=0; i<rt; i++) {
                epoll_event one_event = re_events[i];
                if (one_event.data.fd == m_wake_fd && (one_event.events & READ)) {  // 处理wakeup事件
                    char buf[8];
                    // 清空eventfd，避免重复触发
                    while (1) {
                        if ((g_sys_read_fun(m_wake_fd, buf, 8) == -1) && errno == EAGAIN) {
                            break;
                        }
                    }
                } else {  // 处理普通I/O事件
                    tinyrpc::FdEvent* ptr =(tinyrpc::FdEvent*)one_event.data.ptr;
                    if (ptr != nullptr) {
                        int fd = ptr->getFd();
                        if ((!(one_event.events & EPOLLIN)) && (!(one_event.events & EPOLLOUT))) {  // 未知事件
                            // TOEDIT
                            std::cerr << "socket [" << fd << "] occur other unknow event:[" << one_event.events << "], need unregister this socket" << std::endl;
                            delEventInLoopThread(fd);  // 注销未知事件
                        } else {
                            if (ptr->getCoroutine()) {  // 如果FdEvent关联了协程
                                if (!first_coroutine) {
                                    // 第一个协程直接保存，后续在循环开始时恢复
                                    first_coroutine = ptr->getCoroutine();
                                    continue;
                                }
                                if (m_reactor_type == SubReactor) {
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
                                if (fd == m_timer_fd) {  // Timer事件直接执行回调
                                    read_cb();
                                    continue;
                                }
                                // 其它事件将回调加入任务队列
                                if (one_event.events & EPOLLIN) {
                                    Mutex::Lock lock(m_mutex);
                                    m_pending_tasks.push_back(read_cb);
                                }
                                if (one_event.events & EPOLLOUT) {
                                    Mutex::Lock lock(m_mutex);
                                    m_pending_tasks.push_back(write_cb);
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
                Mutex::Lock lock(m_mutex);
                tmp_add.swap(m_pending_add_fds);  // swap减少锁占用时间
                m_pending_add_fds.clear();

                tmp_del.swap(m_pending_del_fds);
                m_pending_del_fds.clear();
            }
            for (auto i = tmp_add.begin(); i != tmp_add.end(); i++) {
                addEventInLoopThread((*i).first, (*i).second);
            }
            for (auto i = tmp_del.begin(); i != tmp_del.end(); i++) {
                delEventInLoopThread((*i));
            }
        }
    }
    // TOEDIT
    std::cout << "[Reactor::loop] : reactor loop end" << std::endl;
    m_is_looping = false;
}

void Reactor::stop() {
    if (!m_stop_flag && m_is_looping) {
        m_stop_flag = true;
        wakeup();
    }
}

void Reactor::addTask(std::function<void()> task, bool is_wakeup) {
    {
        Mutex::Lock lock(m_mutex);
        m_pending_tasks.push_back(task);
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
        Mutex::Lock lock(m_mutex);
        m_pending_tasks.insert(m_pending_tasks.end(), task.begin(), task.end());
    }
    if (is_wakeup) {
        wakeup();
    }
}

// 添加协程，将协程的Resume操作包装成任务，调用addTask()
void Reactor::addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup) {
    auto func = [cor]() {
        tinyrpc::Coroutine::Resume(cor.get());
    };
    addTask(func, is_wakeup);
}

Timer* Reactor::getTimer() {
    if (!m_timer) {
        m_timer = new Timer(this);
        m_timer_fd = m_timer->getFd();
    }
    return m_timer;
}

pid_t Reactor::getTid() {
    return m_tid;
}

void Reactor::setReactorType(ReactorType type) {
    m_reactor_type = type;
}

CoroutineTaskQueue* CoroutineTaskQueue::GetCoroutineTaskQueue() {
    if (t_coroutine_task_queue) {
        return t_coroutine_task_queue;
    }
    t_coroutine_task_queue = new CoroutineTaskQueue();
    return t_coroutine_task_queue;
}

void CoroutineTaskQueue::push(FdEvent* cor) {
    Mutex::Lock lock(m_mutex);
    m_task.push(cor);
    lock.unlock();
}

FdEvent* CoroutineTaskQueue::pop() {
    FdEvent* re = nullptr;
    Mutex::Lock lock(m_mutex);
    if (m_task.size() >= 1) {
        re = m_task.front();
        m_task.pop();
    }
    lock.unlock();

    return re;
}



}