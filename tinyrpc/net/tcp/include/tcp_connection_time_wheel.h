#pragma once

#include <queue>
#include <vector>

#include "abstract_slot.h"
#include "reactor.h"
#include "timer.h"

/**
 * 时间轮实现
 */

namespace tinyrpc {

class TcpConnection;

class TcpTimeWheel {
   public:
    typedef std::shared_ptr<TcpTimeWheel> ptr;

    typedef AbstractSlot<TcpConnection> TcpConnectionSlot;

   private:
    Reactor* reactor_{nullptr};
    int bucket_count_{0};  // 桶的数量
    int inteval_{0};       // 时间间隔(s)

    TimerEvent::ptr event_;
    std::queue<std::vector<TcpConnectionSlot::ptr>> wheel_;  // 时间轮队列

   public:
    TcpTimeWheel(Reactor* reactor, int bucket_count, int inteval = 10);

    ~TcpTimeWheel();

    void fresh(TcpConnectionSlot::ptr slot);  // 刷新连接

    void loopFunc();  // 定时器回调
};

}  // namespace tinyrpc