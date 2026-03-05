#pragma once

#include <google/protobuf/service.h>
#include <map>

#include "abstract_codec.h"
#include "abstract_dispatcher.h"
#include "fd_event.h"
#include "http_dispatcher.h"
#include "http_servlet.h"
#include "io_thread.h"
#include "net_address.h"
#include "reactor.h"
#include "tcp_connection.h"
#include "tcp_connection_time_wheel.h"
#include "timer.h"

/**
 * 是TinyRPC中的TCP服务器核心类，负责管理整个服务端的网络通信。
 * 它协调连接接受、IO线程池、协议编解码、请求分发等所有服务端组件，是服务端架构的中轴。
 */

namespace tinyrpc {

// 负责监听和接受新连接的组件
class TcpAcceptor {
   private:
    int family_{-1};  // 地址族
    int fd_{-1};      // 监听socket的文件描述符

    NetAddress::ptr local_addr_{nullptr};  // 服务器监听地址
    NetAddress::ptr peer_addr_{nullptr};   // 客户端地址（accept后填充）

   public:
    typedef std::shared_ptr<TcpAcceptor> ptr;

    TcpAcceptor(NetAddress::ptr net_addr);

    ~TcpAcceptor();

    void init();  // 初始化监听socket，包括socket创建、bind、listen

    int toAccept();  // 接受新连接，返回客户端socket fd

    NetAddress::ptr getPeerAddr() { return peer_addr_; }

    NetAddress::ptr getLocalAddr() { return local_addr_; }
};

class TcpServer {
   private:
    NetAddress::ptr addr_;  // 服务器监听地址

    TcpAcceptor::ptr acceptor_;  // 负责接受连接

    int tcp_counts_{0};  // 当前TCP连接数统计

    Reactor* main_reactor_{nullptr};  // 主Reactor，运行在主线程，处理连接接受

    bool is_stop_accept_{false};  // 停止接受连接的标志

    Coroutine::ptr accept_cor_;  // 接受连接的协程

    AbstractDispatcher::ptr dispatcher_;  // 请求分发器(HTTP或TinyPB)

    AbstractCodeC::ptr codec_;  // 协议编解码器

    IOThreadPool::ptr io_pool_;  // IO线程池，管理多个IO线程

    ProtocolType protocol_type_{TinyPb_Protocol};  // 协议类型

    TcpTimeWheel::ptr time_wheel_;  // 时间轮，管理连接超时

    std::map<int, std::shared_ptr<TcpConnection>> clients_;  // 连接映射表

    TimerEvent::ptr clear_client_timer_event_{nullptr};  // 定期清理已关闭连接的定时器

    void MainAcceptCorFunc();

    void ClearClientTimerFunc();

   public:
    typedef std::shared_ptr<TcpServer> ptr;

    TcpServer(NetAddress::ptr addr, ProtocolType type = TinyPb_Protocol);

    ~TcpServer();

    void start();  // 启动服务器

    void addCoroutine(tinyrpc::Coroutine::ptr cor);

    bool registerService(std::shared_ptr<google::protobuf::Service> service);  // 注册Protobuf Service

    bool registerHttpServlet(const std::string& utl_path, HttpServlet::ptr servlet);  // 注册HTTP Servlet

    TcpConnection::ptr addClient(IOThread* io_thread, int fd);  // 添加新的客户端连接

    void freshTcpConnection(TcpTimeWheel::TcpConnectionSlot::ptr slot);  // 刷新连接在时间轮中的位置

    AbstractDispatcher::ptr getDispatcher();

    AbstractCodeC::ptr getCodec();

    NetAddress::ptr getPeerAddr();

    NetAddress::ptr getLocalAddr();

    IOThreadPool::ptr getIOThreadPool();

    TcpTimeWheel::ptr getTimeWheel();
};

}  // namespace tinyrpc
