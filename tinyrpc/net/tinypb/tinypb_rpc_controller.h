#pragma once

#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include <stdio.h>
#include <memory>

#include <tinyrpc/net/comm/net_address.h>

/**
 * 是TinyRPC中RPC调用的控制器类，用于管理RPC调用的状态、超时、错误信息等。
 */

namespace tinyrpc {

/**
 * 继承自google::protobuf::RpcController，这是Protobuf定义的RPC控制器接口。
 * 通过实现这个接口，TinyPbRpcController可以与Protobuf生成的Service和Stub代码无缝集成。
 */
class TinyPbRpcController : public google::protobuf::RpcController {
   private:
    /**
     * 核心状态
     */
    int error_code_{0};       // 框架级错误码，0表示成功
    std::string error_info_;  // 详细错误描述
    std::string msg_req_;     // 消息请求编号，用于日志追踪
    bool is_failed_{false};   // 是否失败标志
    bool is_cancled_{false};  // 是否取消标志

    /**
     * 网络信息
     */
    NetAddress::ptr peer_addr_;   // 对端网络地址
    NetAddress::ptr local_addr_;  // 本地网络地址

    /**
     * 调用配置
     */
    int timeout_{5000};        // RPC超时时间，默认5000毫秒
    std::string method_name_;  // 方法名
    std::string full_name_;    // 完整方法名

   public:
    typedef std::shared_ptr<TinyPbRpcController> ptr;

    TinyPbRpcController() = default;

    ~TinyPbRpcController() = default;

    /**
     * Protobuf RpcController 接口实现
     */

    /* ----------- 客户端方法 -----------*/

    void Reset() override;  // 重置控制器状态，用于复用控制器对象

    bool Failed() const override;  // 检查RPC调用是否失败

    /*----------- 服务端方法 -----------*/

    std::string ErrorText() const override;  // 获取错误描述信息

    void StartCancel() override;  // 启动取消操作

    void SetFailed(const std::string& reason) override;  // 设置失败状态和原因

    bool IsCanceled() const override;  // 检查是否已取消

    void NotifyOnCancel(google::protobuf::Closure* callback) override;  // 注册取消时的回调

    /**
     * TinyRPC 扩展方法
     */

    /*----------- 错误管理 -----------*/

    int ErrorCode() const;  // 获取框架级错误码

    void SetErrorCode(const int error_code);  // 设置框架级错误码

    void SetError(const int err_code, const std::string& err_info);  // 同时设置错误码和错误信息

    /*----------- 消息追踪 -----------*/

    const std::string& MsgSeq() const;  // 获取消息请求编号，用于追踪RPC调用

    void SetMsgReq(const std::string& msg_req);  // 设置消息请求编号，用于追踪RPC调用

    /*----------- 网络地址 -----------*/

    void SetPeerAddr(NetAddress::ptr addr);  // 设置对端地址

    NetAddress::ptr PeerAddr();  // 获取对端地址

    void SetLocalAddr(NetAddress::ptr addr);  // 设置本地地址

    NetAddress::ptr LocalAddr();  // 获取本地地址

    /*----------- 超时控制 -----------*/

    void SetTimeout(const int timeout);  // 设置RPC超时时间

    int Timeout() const;  // 获取RPC超时时间

    /*----------- 方法信息 -----------*/

    void SetMethodName(const std::string& name);  // 设置方法名

    std::string GetMethodName();  // 获取方法名

    void SetMethodFullName(const std::string& name);  // 设置完整方法名

    std::string GetMethodFullName();  // 获取完整方法名
};

}  // namespace tinyrpc