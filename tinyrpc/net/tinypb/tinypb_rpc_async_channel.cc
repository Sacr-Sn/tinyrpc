#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>

#include <tinyrpc/comm/error_code.h>
#include <tinyrpc/comm/msg_req.h>
#include <tinyrpc/comm/run_time.h>
#include <tinyrpc/comm/start.h>
#include <tinyrpc/coroutine/coroutine.h>
#include <tinyrpc/coroutine/coroutine_pool.h>
#include <tinyrpc/net/tinypb/tinypb_rpc_async_channel.h>

namespace tinyrpc {

TinyPbRpcAsyncChannel::TinyPbRpcAsyncChannel(NetAddress::ptr addr) {
    DebugLog << "constructing TinyPbRpcAsyncChannel, addr: " << addr->toString();
    rpc_channel_ = std::make_shared<TinyPbRpcChannel>(addr);
    current_iothread_ = IOThread::GetCurrentIOThread();
    current_cor_ = Coroutine::GetCurrentCoroutine();
}

TinyPbRpcAsyncChannel::~TinyPbRpcAsyncChannel() { GetCoroutinePool()->returnCoroutine(pending_cor_); }

TinyPbRpcChannel* TinyPbRpcAsyncChannel::getRpcChannel() { return rpc_channel_.get(); }

/**
 * 保存所有RPC调用相关对象的智能指针，增加引用计数，确保在异步执行期间对象不会被销毁。
 * 因为RPC调用会在新协程中执行，可能在不同的线程上，如果不保存引用计数，原始对象可能在RPC完成前就被销毁
 */
void TinyPbRpcAsyncChannel::saveCallee(con_ptr controller, msg_ptr req, msg_ptr res, clo_ptr closure) {
    controller_ = controller;
    req_ = req;
    res_ = res;
    closure_ = closure;
    is_pre_set_ = true;
}

void TinyPbRpcAsyncChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                                       google::protobuf::RpcController* controller,
                                       const google::protobuf::Message* request, google::protobuf::Message* response,
                                       google::protobuf::Closure* done) {
    TinyPbRpcController* rpc_controller = dynamic_cast<TinyPbRpcController*>(controller);
    if (!is_pre_set_) {  // 检查saveCallee()是否已调用
        ErrorLog << "Error! must call [saveCallee()] function before [CallMethod()]";
        TinyPbRpcController* rpc_controller = dynamic_cast<TinyPbRpcController*>(controller);
        rpc_controller->SetError(ERROR_NOT_SET_ASYNC_PRE_CALL,
                                 "Error! must call [saveCallee()] function before [CallMethod()]");
        is_finished_ = true;
        return;
    }

    // 设置消息请求编号
    RunTime* run_time = getCurrentRunTime();
    if (run_time) {
        rpc_controller->SetMsgReq(run_time->msg_no);
        DebugLog << "get from RunTime succ, msgno = " << run_time->msg_no;
    } else {
        rpc_controller->SetMsgReq(MsgReqUtil::genMsgNumber());
        DebugLog << "get from RunTime error, generate new msgno = " << rpc_controller->MsgSeq();
    }

    // 创建新协程执行RPC调用 - 创建了一个lambda函数作为新协程的任务
    std::shared_ptr<TinyPbRpcAsyncChannel> s_ptr = shared_from_this();  // 保存自身引用
    auto cb = [s_ptr, method]() mutable {
        DebugLog << "now excute rpc call method by this thread";
        // 调用内部TinyPbRpcChannel::CallMethod()执行实际的RPC调用
        s_ptr->getRpcChannel()->CallMethod(method, s_ptr->getControllerPtr(), s_ptr->getRequestPtr(),
                                           s_ptr->getResponsePtr(), NULL);

        DebugLog << "excute rpc call method bu this thread finish";
        // 创建内层lambda作为回调
        auto call_back = [s_ptr]() mutable {
            DebugLog << "async excute rpc call method back old thread";
            // 如果有closure，执行用户定义的回调函数
            if (s_ptr->getClosurePtr() != nullptr) {
                s_ptr->getClosurePtr()->Run();
            }
            s_ptr->setFinished(true);
            if (s_ptr->getNeedResume()) {
                DebugLog << "async excute rpc call method back old thread, need resume";
                Coroutine::Resume(s_ptr->getCurrentCoroutine());
            }
            s_ptr.reset();  // 释放引用
        };
        // 内层回调通过addTask()添加到原IO线程的Reactor中执行，确保回调在正确的线程上下文中运行。
        s_ptr->getIOThread()->getReactor()->addTask(call_back, true);
        s_ptr.reset();
    };
    // 将新协程添加到随机的 IO 线程中执行,实现负载均衡。
    pending_cor_ = GetServer()->getIOThreadPool()->addCoroutineToRandomThread(cb, false);
}

// 等待RPC完成
void TinyPbRpcAsyncChannel::wait() {
    need_resume_ = true;  // 表示需要恢复当前协程
    if (is_finished_) {   // RPC已完成，直接返回
        return;
    }
    Coroutine::Yield();  // RPC未完成，让出当前协程
}

void TinyPbRpcAsyncChannel::setFinished(bool value) { is_finished_ = true; }

IOThread* TinyPbRpcAsyncChannel::getIOThread() { return current_iothread_; }

Coroutine* TinyPbRpcAsyncChannel::getCurrentCoroutine() { return current_cor_; }

bool TinyPbRpcAsyncChannel::getNeedResume() { return need_resume_; }

google::protobuf::RpcController* TinyPbRpcAsyncChannel::getControllerPtr() { return controller_.get(); }

google::protobuf::Message* TinyPbRpcAsyncChannel::getRequestPtr() { return req_.get(); }

google::protobuf::Message* TinyPbRpcAsyncChannel::getResponsePtr() { return res_.get(); }

google::protobuf::Closure* TinyPbRpcAsyncChannel::getClosurePtr() { return closure_.get(); }

}  // namespace tinyrpc