#pragma once

#include <google/protobuf/service.h>
#include <memory>

#include <tinyrpc/net/comm/net_address.h>
#include <tinyrpc/net/tcp/tcp_client.h>

/**
 * TinyPbRpcChannel是TinyRPC中实现阻塞协程式异步RPC调用的核心类，
 * 它继承自google::protobuf::RpcChannel，是Protobuf RPC框架的标准接口实现。
 * 这个类使得开发者可以用同步的方式编写代码，而底层实际上是完全异步的。
 * 用于需要立即获取结果的场景。
 */

namespace tinyrpc {

class TinyPbRpcChannel : public google::protobuf::RpcChannel {
   private:
    NetAddress::ptr addr_;  // 服务器地址

   public:
    typedef std::shared_ptr<TinyPbRpcChannel> ptr;

    TinyPbRpcChannel(NetAddress::ptr addr);

    ~TinyPbRpcChannel() = default;

    void CallMethod(const google::protobuf::MethodDescriptor* method, google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request, google::protobuf::Message* response,
                    google::protobuf::Closure* done);
};

}  // namespace tinyrpc