#pragma once

#include <google/protobuf/service.h>
#include <future>

#include "coroutine.h"
#include "io_thread.h"
#include "net_address.h"
#include "tcp_client.h"
#include "tinypb_data.h"
#include "tinypb_rpc_channel.h"
#include "tinypb_rpc_controller.h"

/**
 * 是TinyRPC中实现的非阻塞协程式异步RPC调用的核心类，
 * 它继承自google::protobuf::RpcChannel 和 std::enable_shared_from_this<TinyPbRpcAsyncChannel>
 * 这个类使得开发者可以发起RPC调用后立即继续执行其他代码，而不需要等待RPC完成
 */

namespace tinyrpc {

/**
 * 继承 google::protobuf::RpcChannel:与 Protobuf 生成的 Service Stub 无缝集成
 * 继承 std::enable_shared_from_this:支持在成员函数中安全地获取自身的 shared_ptr
 */
class TinyPbRpcAsyncChannel : public google::protobuf::RpcChannel,
                              public std::enable_shared_from_this<TinyPbRpcAsyncChannel> {
   public:
    typedef std::shared_ptr<TinyPbRpcAsyncChannel> ptr;
    typedef std::shared_ptr<google::protobuf::RpcController> con_ptr;
    typedef std::shared_ptr<google::protobuf::Message> msg_ptr;
    typedef std::shared_ptr<google::protobuf::Closure> clo_ptr;

    TinyPbRpcAsyncChannel(NetAddress::ptr addr);
    ~TinyPbRpcAsyncChannel();

    // 实现RPC调用，创建新协程执行
    void CallMethod(const google::protobuf::MethodDescriptor* method, google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request, google::protobuf::Message* response,
                    google::protobuf::Closure* done);

    // 保存调用参数的引用计数,必须在CallMethod()前调用
    void saveCallee(con_ptr controller, msg_ptr req, msg_ptr res, clo_ptr closure);

    // 可选调用，阻塞当前协程直到RPC完成
    void wait();

    // 获取内部的TinyPbRpcChannel
    TinyPbRpcChannel* getRpcChannel();

    void setFinished(bool value);

    bool getNeedResume();

    IOThread* getIOThread();

    Coroutine* getCurrentCoroutine();

    google::protobuf::RpcController* getControllerPtr();

    google::protobuf::Message* getRequestPtr();

    google::protobuf::Message* getResponsePtr();

    google::protobuf::Closure* getClosurePtr();

   private:
    /*------------------- 内部RPC通道 -------------------*/
    TinyPbRpcChannel::ptr rpc_channel_;  // 内部使用的TinyPbRpcChannel对象，实际执行RPC调用

    /*------------------- 协程管理 -------------------*/
    Coroutine::ptr pending_cor_;        // 新创建的协程，用于执行RPC调用
    Coroutine* current_cor_{NULL};      // 当前协程（调用者协程）
    IOThread* current_iothread_{NULL};  // 当前IO线程

    /*------------------- 状态标志 -------------------*/
    bool is_finished_{false};  // RPC是否完成
    bool need_resume_{false};  // 是否需要恢复调用者协程
    bool is_pre_set_{false};   // 是否已调用saveCallee()

    /*------------------- 保存的调用参数 -------------------*/
    con_ptr controller_;
    msg_ptr req_;
    msg_ptr res_;
    clo_ptr closure_;
};

}  // namespace tinyrpc