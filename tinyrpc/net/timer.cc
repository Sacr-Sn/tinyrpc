#include <iostream>
#include <sys/timerfd.h>
#include <sys/time.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <functional>
#include <vector>
#include <map>

#include "../comm/log.h"
#include "timer.h"
#include "../coroutine/coroutine_hook.h"

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
    m_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);
    // TOEDIT 暂时用cout代替log
    std::cout << "m_timer fd = " << m_fd << std::endl;
    if (m_fd == -1) {
        // TOEDIT 暂时用cout代替log
        std::cout << "timerfd_create error" << std::endl;
    }
    // 使用std::bind将Timer::onTimer绑定为读事件的回调函数
    m_read_callback = std::bind(&Timer::onTimer, this);
    // 监听timerfd的可读事件
    addListenEvents(READ);
}

Timer::~Timer() {
    unregisterFromReactor();
    close(m_fd);
}

void Timer::addTimerEvent(TimerEvent::ptr event, bool need_reset) {
    RWMutex::WriteLock lock(m_event_mutex);
    bool is_reset = false;
    if (m_pending_events.empty()) {
        is_reset = true;
    } else {
        auto it = m_pending_events.begin();
        if (event->m_arrive_time < (*it).second->m_arrive_time) {
            is_reset = true;
        }
    }
    m_pending_events.emplace(event->m_arrive_time, event);
    lock.unlock();

    if (is_reset && need_reset) {
        // TOEDIT 暂时用cout代替log
        std::cout << "[Timer::addTimerEvent] : need reset timer" << std::endl;
        resetArriveTime();
    }
}

void Timer::delTimerEvent(TimerEvent::ptr event) {
    // 标记为已取消，这样在onTimer()中也会跳过该事件，避免竞态条件
    event->m_is_cancled = true;
    RWMutex::WriteLock lock(m_event_mutex);
    // 找到相同到期时间的事件范围
    auto begin = m_pending_events.lower_bound(event->m_arrive_time);
    auto end = m_pending_events.upper_bound(event->m_arrive_time);
    auto it = begin;
    // 遍历该范围，找到匹配的事件（通过指针比较）
    for (it = begin; it != end; it++) {
        if (it->second == event) {
            // TOEDIT
            std::cout << "[Timer::delTimerEvent] : find timer event, now delete it. src arrive time=" << event->m_arrive_time << std::endl;
            break;
        }
    }
    // 从multimap中删除
    if (it != m_pending_events.end()) {
        m_pending_events.erase(it);
    }
    lock.unlock();
    // TOEDIT
    std::cout << "[Timer::delTimerEvent] : del timer event succ, origin arrive time=" << event->m_arrive_time << std::endl;
}

void Timer::resetArriveTime() {
    RWMutex::ReadLock lock(m_event_mutex);
    // 复制临界资源到临时变量，可以在不持有锁
    std::multimap<int64_t, TimerEvent::ptr> tmp = m_pending_events;
    lock.unlock();

    if (tmp.size() == 0) {  // 没有待处理事件，直接返回
        // TOEDIT
        std::cout << "[Timer::resetArriveTime] : no timerevent pending, size=0" << std::endl;
        return;
    }

    int64_t now = getNowMs();
    // 指向最晚到期的事件
    // 因为timerfd只能设置一个到期时间，设置为最晚的事件可以确保在这之前的所有事件都能被处理
    auto it = tmp.rbegin();  
    if ((*it).first < now) {  // 如果最晚的事件都已经过期，说明所有事件都过期了
        // TOEDIT
        std::cout << "[Timer::resetArriveTime] : all timer events has already expire" << std::endl;
        return;
    }

    int64_t interval = (*it).first - now;

    itimerspec new_value;
    memset(&new_value, 0, sizeof(new_value));

    timespec ts;
    memset(&ts, 0, sizeof(new_value));
    // 将时间转换为秒和纳秒
    ts.tv_sec = interval / 1000;
    ts.tv_nsec = (interval % 1000) * 1000000;
    new_value.it_value = ts;
    // 设置timerfd的到期时间,it_value设置首次到期时间，in_interval为0表示不重复
    int rt = timerfd_settime(m_fd, 0, &new_value, nullptr);
    if (rt != 0) {
        // TOEDIT
        std::cout << "[Timer::resetArriveTime] : timer_settime error, interval=" << interval << std::endl;
    } else {

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
        if ((g_sys_read_fun(m_fd, buf, 8) == -1) && errno == EAGAIN) {
            break;
        }
    }

    int64_t now = getNowMs();
    RWMutex::WriteLock lock(m_event_mutex);
    auto it = m_pending_events.begin();
    std::vector<TimerEvent::ptr> tmps;
    std::vector<std::pair<int64_t, std::function<void()>>> tasks;
    // 遍历，将到期且未取消的事件加入tmps和tasks向量
    for (it = m_pending_events.begin(); it != m_pending_events.end(); it++) {
        if ((*it).first <= now && !((*it).second->m_is_cancled)) {
            tmps.push_back((*it).second);
            tasks.push_back(std::make_pair((*it).second->m_arrive_time, (*it).second->m_task));
        } else {
            break;
        }
    }

    // 删除已处理的事件
    m_pending_events.erase(m_pending_events.begin(), it);
    lock.unlock();

    // 处理重复定时器
    for (auto i = tmps.begin(); i != tmps.end(); i++) {
        if ((*i)->m_is_repeated) {
            (*i)->resetTime();  // 重新计算到期时间
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

}