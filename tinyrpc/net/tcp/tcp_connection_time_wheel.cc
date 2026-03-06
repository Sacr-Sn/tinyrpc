#include <tinyrpc/net/tcp/tcp_connection.h>
#include <tinyrpc/net/tcp/tcp_connection_time_wheel.h>

namespace tinyrpc {

TcpTimeWheel::TcpTimeWheel(Reactor *reactor, int bucket_count, int inteval)
    : reactor_(reactor), bucket_count_(bucket_count), inteval_(inteval) {
    // 初始化时间轮队列
    for (int i = 0; i < bucket_count; i++) {
        std::vector<TcpConnectionSlot::ptr> tmp;
        wheel_.push(tmp);
    }

    // 创建定时器事件
    event_ = std::make_shared<TimerEvent>(inteval_ * 1000, true, std::bind(&TcpTimeWheel::loopFunc, this));
    reactor_->getTimer()->addTimerEvent(event_);
}

TcpTimeWheel::~TcpTimeWheel() { reactor_->getTimer()->delTimerEvent(event_); }

/**
 * 刷新活跃连接的超时时间
 * 将连接槽位添加到队列末尾的桶中
 * 这样每次有数据活动时，连接就被“刷新”到最新的桶
 */
void TcpTimeWheel::fresh(TcpConnectionSlot::ptr slot) {
    DebugLog << "fresh connection";
    wheel_.back().emplace_back(slot);  // 将槽位添加到最后一个桶中
}

// 定时器回调，由定时器定期调用，负责清理超时连接
// 每次丢弃最旧的桶，添加一个新的空桶；被丢掉的桶中的槽中对象执行回调，新活跃的连接将被添加到新桶
void TcpTimeWheel::loopFunc() {
    wheel_.pop();  // 取出最早的桶，其中的连接已超时
    std::vector<TcpConnectionSlot::ptr> tmp;
    wheel_.push(tmp);  // 添加新空桶到队列尾部
    // 利用RAII，旧桶中的操作会自动析构，槽位析构时自动触发超时回调
}

}  // namespace tinyrpc