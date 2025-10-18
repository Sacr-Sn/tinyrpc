#pragma once

/**
 * Timer 是 TinyRPC 中实现超时控制的核心组件。
 * 它通过继承 FdEvent 和使用 timerfd,巧妙地将定时器集成到 Reactor 的 epoll 事件循环中,实现了高效的定时事件管理。
 * multimap 的使用保证了事件按时间自动排序,而读写锁确保了并发安全。
 * Timer 在 RPC 超时、连接超时、日志刷新等多个场景中都有应用,是整个框架不可或缺的基础组件。
 * 
 * 设计优势：
 *  1.高效的事件管理:使用 multimap 自动排序,O(log n) 插入和删除
 *  2.统一的事件处理:通过 timerfd 将定时器集成到 epoll,与 I/O 事件统一处理
 *  3.线程安全:使用读写锁保护并发访问
 *  4.灵活的定时模式:支持一次性和重复定时
 *  5.精确的时间控制:使用单调时钟,不受系统时间调整影响
*/

#include <time.h>
#include <memory>
#include <map>
#include <functional>

#include "../comm/log.h"
#include "mutex.h"
#include "fd_event.h"
#include "reactor.h"

namespace tinyrpc {

// 获取当前时间的毫秒时间戳,这是定时器系统的时间基准。
int64_t getNowMs();

/**
 * 该类相当于一个事件容器类，每一个对象都封装了一个事件及其执行的时间（即一个回调函数及执行该回调的时间）
 * 该类的对象被Timer类管理
*/
class TimerEvent {

public:
    int64_t m_arrive_time;  // 到时时间（毫秒时间戳）
    int64_t m_interval;  // 定时间隔
    bool m_is_repeated {false};   // 是否重复执行
    bool m_is_cancled {false};  // 是否已取消
    std::function<void()> m_task;  // 回调函数

    typedef std::shared_ptr<TimerEvent> ptr;

    TimerEvent(int64_t interval, bool is_repeated, std::function<void()> task)
        : m_interval(interval), m_is_repeated(is_repeated), m_task(task) {
        m_arrive_time = getNowMs() + m_interval;
        // TOEDIT 暂时用cout代替log
        std::cout << "[TimerEvent] : timeevent will occur at " << m_arrive_time << std::endl;
    }

    // 重置定时器，在定时器到期后重新计算下一次到期时间
    void resetTime() {
        m_arrive_time = getNowMs() + m_interval;
        m_is_cancled = false;
    }

    // 唤醒被取消的定时器
    void wake() {
        m_is_cancled = false;
    }

    // 取消定时器
    void cancle() {
        m_is_cancled = true;
    }

    // 取消重复，将重复定时器转换为一次性定时器
    void cancleRepeated() {
        m_is_repeated = false;
    }
};

class FdEvent;

/**
 * 继承自FdEvent：
 *  1.允许Timer作为普通的文件描述符事件集成到epoll中
 *  2.复用FdEvent的事件管理机制
 *  3.使定时器事件和I/O事件统一处理
 * 
 * 本质上是一个事件文件描述符fd，
 * 其维护了系统关于定时的一个文件描述符timerfd，还有一个事件队列m_pending_events
 * timerfd被reactor即epoll维护，每当timerfd到时的时候，就处理m_pending_events中到时的事件，即出发它们的回调函数
*/
class Timer : public tinyrpc::FdEvent {

private:
    /**
     * 键：到期时间（int64_t毫秒时间戳）
     * 值：TimerEvent智能指针
     * multimap：
     *  按键自动排序，最早到期的事件在前,自动按时间排序,无需手动维护；
     *  支持重复键，允许多个事件有相同的到期时间
     *  O(log n) 的插入和删除复杂度
    */
    std::multimap<int64_t, TimerEvent::ptr> m_pending_events;

    RWMutex m_event_mutex;  // 读写锁

public:
    typedef std::shared_ptr<Timer> ptr;

    Timer(Reactor* reactor);

    ~Timer();

    /**
     * 添加定时事件
     * event : 要添加的事件
     * need_reset : 是否需要重置timerfd
    */
    void addTimerEvent(TimerEvent::ptr event, bool need_reset = true);

    /**
     * 删除定时事件
     * 从m_pending_events中删除指定的定时事件，并标记事件为已取消
    */
    void delTimerEvent(TimerEvent::ptr event);

    /**
     * 重置到期时间
     * 根据m_pending_events中最晚的事件重新设置timerfd的到期时间
     * 该方法在添加或删除事件后被调用
    */
    void resetArriveTime();

    /**
     * 定时器回调
     * 当timerfd到期时被Reactor调用。它负责：
     *  1.读取timerfd清空数据
     *  2.收集所有到期的事件
     *  3.处理重复定时器
     *  4.执行所有到期事件的回调函数
    */
    void onTimer();
};

}