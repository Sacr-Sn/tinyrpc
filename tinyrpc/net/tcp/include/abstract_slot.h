#pragma once

#include <functional>
#include <memory>

/**
 * TCP 时间轮(Time Wheel)机制,用于高效管理和清理空闲的 TCP 连接。
 * 时间轮是一种经典的定时器数据结构,可以在 O(1) 时间复杂度内完成定时器的添加、删除和触发操作。
 *
 * abstract.h定义了时间轮中槽位(Slot)的抽象接口。
 * 每个槽位可以存储一个对象(如TcpConnection)，并在超时时执行回调函数
 */

namespace tinyrpc {

// 也是一种RAII设计
template <class T>
class AbstractSlot {
   public:
    typedef std::shared_ptr<AbstractSlot> ptr;

    // 模板化设计：可以管理任意类型的对象
    typedef std::weak_ptr<T> weakPtr;
    typedef std::shared_ptr<T> sharedPtr;

   private:
    weakPtr weak_ptr_;                   // 弱引用，避免循环引用
    std::function<void(sharedPtr)> cb_;  // 超时回调

   public:
    AbstractSlot(weakPtr ptr, std::function<void(sharedPtr)> cb) : weak_ptr_(ptr), cb_(cb) {}

    // 析构时触发回调：当槽位被销毁时，自动执行超时回调
    // 也是一种RAII设计
    ~AbstractSlot() {
        sharedPtr ptr = weak_ptr_.lock();
        if (ptr) {
            cb_(ptr);
        }
    }
};

}  // namespace tinyrpc