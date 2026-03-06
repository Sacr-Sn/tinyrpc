#pragma once

#include <google/protobuf/service.h>
#include <memory>

#include <tinyrpc/coroutine/coroutine.h>
#include <tinyrpc/coroutine/coroutine_hook.h>
#include <tinyrpc/net/comm/abstract_codec.h>
#include <tinyrpc/net/comm/net_address.h>
#include <tinyrpc/net/comm/reactor.h>
#include <tinyrpc/net/tcp/tcp_connection.h>
#include <tinyrpc/net/tinypb/tinypb_data.h>

/**
 * 是TinyRPC中的TCP客户端类，用于发起TCP连接并进行RPC调用。
 * 它是RPC客户端的底层网络通信组件，负责连接管理、数据收发和超时控制
 *
 * 应该在工作协程中使用，而不是在主协程中使用
 */

namespace tinyrpc {

class TcpClient {
   private:
    int family_{0};           // 协议族
    int fd_{-1};              // 文件描述符
    int try_counts_{3};       // 最大重建次数，默认为3
    int max_timeout_{10000};  // 最大超时时间，默认10000ms(10s)
    bool is_stop_{false};     // 是否停止
    std::string err_info_;    // 错误信息字符串

    NetAddress::ptr local_addr_{nullptr};     // 本地地址
    NetAddress::ptr peer_addr_{nullptr};      // 对端地址（服务器地址）
    Reactor* reactor_{nullptr};               // 关联的Reactor对象
    TcpConnection::ptr connection_{nullptr};  // 管理连接

    AbstractCodeC::ptr codec_{nullptr};  // 协议编解码器(HTTP或TinyPB)

    bool connect_succ_{false};  //  是否连接成功

   public:
    typedef std::shared_ptr<TcpClient> ptr;

    TcpClient(NetAddress::ptr addr, ProtocolType type = TinyPb_Protocol);

    ~TcpClient();

    void init();

    void resetFd();  // 重置文件描述符，用于重连

    int sendAndRecvTinyPb(const std::string& msg_no, TinyPbStruct::pb_ptr& res);  // 发送并接收TinyPB协议数据的核心方法

    void stop();  // 停止客户端

    TcpConnection* getConnection();

    void setTimeout(const int v) {  // 设置超时时间
        max_timeout_ = v;
    }

    void setTryCounts(const int v) {  // 设置重试次数
        try_counts_ = v;
    }

    const std::string& getErrInfo() {  // 获取错误信息
        return err_info_;
    }

    NetAddress::ptr getPeerAddr() const { return peer_addr_; }

    NetAddress::ptr getLocalAddr() const { return local_addr_; }

    AbstractCodeC::ptr getCodeC() { return codec_; }
};

}  // namespace tinyrpc