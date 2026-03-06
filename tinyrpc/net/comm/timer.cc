#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <time.h>
#include <functional>
#include <iostream>
#include <map>
#include <vector>

#include <tinyrpc/comm/log.h>
#include <tinyrpc/coroutine/coroutine_hook.h>
#include <tinyrpc/net/comm/timer.h>

/**
 * coroutine_hook.h中定义的原始系统read函数指针，
 * 用于在onTimer()中直接调用系统read，避免hook
 */
extern read_fun_ptr_t g_sys_read_fun;

namespace tinyrpc {

int64_t getNowMs() {
    timeval val;
    gettimeofday(&val, nullptr);  // 获取当前时间
    // 将秒转换为毫秒并加上微秒转换的部分（在32位系统上要使用注释掉的写法，否则将溢出）
    // int64_t re = static_cast<int64_t>(val.tv_sec) * 1000 + val.tv_usec / 1000;
    int64_t re = val.tv_sec * 1000 + val.tv_usec / 1000;
    return re;  // 返回总毫秒数
}

/**
 * 将timerfd作为普通文件描述符，这样当定时器到期时，timerfd变为可读，epoll会触发读事件，从而调用onTimer()处理到期事件
 */
Timer::Timer(Reactor* reactor) : FdEvent(reactor) {
    /**
     * CLOCK_MONOTONIC 使用单调时钟，不受系统时间调整影响
     * TFD_NONBLOCK 设置为非阻塞模式
     * TFD_CLOEXEC 确保子进程不继承该文件描述符
     */
    fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    DebugLog << "m_timer fd = " << fd_;
    if (fd_ == -1) {
        DebugLog << "timerfd_create error";
    }
    // 使用std::bind将Timer::onTimer绑定为读事件的回调函数
    m_read_callback = std::bind(&Timer::onTimer, this);
    // 监听timerfd的可读事件
    addListenEvents(READ);
}

Timer::~Timer() {
    unregisterFromReactor();
    close(fd_);
}

void Timer::addTimerEvent(TimerEvent::ptr event, bool need_reset) {
    bool is_reset = false;
    {
        std::unique_lock<std::shared_mutex> write_lock(rw_mtx_);
        if (pending_events_.empty()) {
            is_reset = true;
        } else {
            auto it = pending_events_.begin();
            if (event->arrive_time < (*it).second->arrive_time) {
                is_reset = true;
            }
        }
        pending_events_.emplace(event->arrive_time, event);
    }

    if (is_reset && need_reset) {
        DebugLog << "need reset timer";
        resetArriveTime();
    }
}

void Timer::delTimerEvent(TimerEvent::ptr event) {
    // 标记为已取消，这样在onTimer()中也会跳过该事件，避免竞态条件
    event->is_cancled = true;
    {
        std::unique_lock<std::shared_mutex> write_lock(rw_mtx_);
        // 找到相同到期时间的事件范围
        auto begin = pending_events_.lower_bound(event->arrive_time);
        auto end = pending_events_.upper_bound(event->arrive_time);
        auto it = begin;
        // 遍历该范围，找到匹配的事件（通过指针比较）
        for (it = begin; it != end; it++) {
            if (it->second == event) {
                DebugLog << "find timer event, now delete it. src arrive time=" << event->arrive_time;
                break;
            }
        }
        // 从multimap中删除
        if (it != pending_events_.end()) {
            pending_events_.erase(it);
        }
    }
    DebugLog << "del timer event succ, origin arrive time=" << event->arrive_time;
}

void Timer::resetArriveTime() {
    int64_t max_expire_time = -1;  // 初始化为无效值

    {
        std::shared_lock<std::shared_mutex> read_lock(rw_mtx_);
        if (pending_events_.empty()) {
            DebugLog << "no timerevent pending, size=0";
            return;
        }
        // 直接取最大 key（multimap 有序，rbegin() 是 O(1)）
        max_expire_time = pending_events_.rbegin()->first;
    }  // 锁在此释放

    int64_t now = getNowMs();
    if (max_expire_time < now) {
        DebugLog << "[Timer::resetArriveTime] : all timer events has already expire";
        return;
    }

    int64_t interval = max_expire_time - now;

    itimerspec new_value{};
    timespec ts{};
    ts.tv_sec = interval / 1000;
    ts.tv_nsec = (interval % 1000) * 1000000;
    new_value.it_value = ts;

    int rt = timerfd_settime(fd_, 0, &new_value, nullptr);
    if (rt != 0) {
        DebugLog << "[Timer::resetArriveTime] : timerfd_settime error, interval=" << interval;
    }
}

// 当 timerfd 到期时被 Reactor 调用
void Timer::onTimer() {
    char buf[8];
    while (1) {
        /**
         * 读取timerfd
         * 使用g_sys_read_fun直接调用系统read，避免协程hook
         * 循环读取直到EAGAIN，确保完全清空timerfd数据
         * 这一步是必须的，否则timerfd会一直保持可读状态
         */
        if ((g_sys_read_fun(fd_, buf, 8) == -1) && errno == EAGAIN) {
            break;
        }
    }

    int64_t now = getNowMs();

    std::vector<TimerEvent::ptr> tmps;
    std::vector<std::pair<int64_t, std::function<void()>>> tasks;

    {
        std::unique_lock<std::shared_mutex> write_lock(rw_mtx_);
        auto it = pending_events_.begin();

        // 遍历，将到期且未取消的事件加入tmps和tasks向量
        for (it = pending_events_.begin(); it != pending_events_.end(); it++) {
            if ((*it).first <= now && !((*it).second->is_cancled)) {
                tmps.push_back((*it).second);
                tasks.push_back(std::make_pair((*it).second->arrive_time, (*it).second->task));
            } else {
                break;
            }
        }
        // 删除已处理的事件
        pending_events_.erase(pending_events_.begin(), it);
    }

    // 处理重复定时器
    for (auto i = tmps.begin(); i != tmps.end(); i++) {
        if ((*i)->is_repeated) {
            (*i)->resetTime();         // 重新计算到期时间
            addTimerEvent(*i, false);  // 重新加入队列
        }
    }

    // 根据剩余事件重新设置timerfd的到期时间
    resetArriveTime();

    // 执行回调，依次执行每个到期事件的回调函数
    for (auto i : tasks) {
        i.second();
    }
}

}  // namespace tinyrpc