#pragma once

#include <google/protobuf/stubs/callback.h>
#include <functional>
#include <memory>

/**
 * 定义了TinyRPC中TinyPB RPC回调的封装类，用于在RPC调用完成后执行回调函数
 */

namespace tinyrpc {

/**
 * 继承自google::protobuf::Closure，这是Protobuf框架定义的回调接口。
 * 通过这个继承，TinyPbRpcClosure可以与Protobuf的RPC机制无缝集成
 */
class TinyPbRpcClosure : public google::protobuf::Closure {
   private:
    std::function<void()> cb_{nullptr};

   public:
    typedef std::shared_ptr<TinyPbRpcClosure> ptr;

    // 使用explicit关键字防止隐式类型转换
    explicit TinyPbRpcClosure(std::function<void()> cb) : cb_(cb) {}

    ~TinyPbRpcClosure() = default;

    /**
     * 执行回调，这是google::protobuf::Closure中要求实现的核心方法
     */
    void Run() {
        if (cb_) {
            cb_();
        }
    }
};

}  // namespace tinyrpc