#pragma once

#include <iostream>
#include <memory>
#include <queue>
#include <shared_mutex>
#include <vector>

#include "abstract_slot.h"
#include "coroutine.h"
#include "fd_event.h"
#include "http_request.h"
#include "io_thread.h"
#include "log.h"
#include "net_address.h"
#include "reactor.h"
#include "tcp_buffer.h"
#include "tcp_connection_time_wheel.h"
#include "tinypb_codec.h"

/**
 * 是TinyRPC中的TCP连接管理类，代表一个单独的TCP连接，负责该连接的数据读取、处理和写入。
 * 每个客户端连接都对应一个TcpConnection对象
 */

namespace tinyrpc {

class TcpServer;
class TcpClient;
class IOThread;

enum TcpConnectionState {
    NotConnected = 1,  // 未连接状态，客户端初始状态
    Connected = 2,     // 已连接状态，可以进行I/O操作
    HalfClosing = 3,   // 半关闭状态，服务端调用shutdown后，只能读不能写
    Closed = 4,        // 已关闭状态，不能进行任何I/O操作
};

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
   public:
    // 两种连接类型
    enum ConnectionType {
        ServerConnection = 1,  // 服务端连接，由TcpServer拥有
        ClientConnection = 2,  // 客户端连接，由TcpClient拥有
    };

    typedef std::shared_ptr<TcpConnection> ptr;

    // 服务端构造函数
    TcpConnection(tinyrpc::TcpServer* tcp_svr, tinyrpc::IOThread* io_thread, int fd, int buff_size,
                  NetAddress::ptr peer_addr);

    // 客户端构造函数
    TcpConnection(tinyrpc::TcpClient* tcp_cli, tinyrpc::Reactor* reactor, int fd, int buff_size,
                  NetAddress::ptr peer_addr);

    ~TcpConnection();

    void setUpClient();  // 客户端连接初始化

    void setUpServer();  // 服务端连接初始化

    void initBuffer(int size);  // 初始化读写缓冲区

    void initServer();  // 服务端连接注册到时间轮和设置协程回调

    void shutdownConnection();  // 关闭连接

    TcpConnectionState getState();  // 获取连接状态

    void setState(const TcpConnectionState& state);  // 设置连接状态

    TcpBuffer* getInBuffer();  // 获取读缓冲区

    TcpBuffer* getOutBuffer();  // 获取写缓冲区

    AbstractCodeC::ptr getCodec() const;  // 获取编解码器

    // 客户端获取响应数据
    bool getResPackageData(const std::string& msg_req, TinyPbStruct::pb_ptr& pb_struct);

    void registerToTimeWheel();  // 注册到时间轮

    Coroutine::ptr getCoroutine();  // 获取协程对象

    void MainServerLoopCorFunc();  // 服务端主循环协程函数

    void input();  // 从socket读取数据到输入缓冲区

    void execute();  // 处理输入缓冲区的数据

    void output();  // 将输出缓冲区的数据写入socket

    void setOverTimeFlag(bool value);  // 设置超时标志

    bool getOverTimeFlag();  // 获取超时标志

   private:
    TcpServer* tcp_server_{nullptr};  // 所属的服务器对象
    TcpClient* tcp_client_{nullptr};  // 所属的客户端对象
    IOThread* io_thread_{nullptr};    // 所属的IO线程
    Reactor* reactor_{nullptr};       // 关联的Reactor对象

    int fd_{-1};                                               // socket文件描述符
    TcpConnectionState state_{TcpConnectionState::Connected};  // 连接状态
    ConnectionType connection_type_{ServerConnection};         // 连接类型（服务端/客户端）

    NetAddress::ptr peer_addr_;  // 对端地址

    TcpBuffer::ptr read_buffer_;   // 读缓冲区
    TcpBuffer::ptr write_buffer_;  // 写缓冲区

    Coroutine::ptr loop_cor_;  // 处理连接的协程

    AbstractCodeC::ptr codec_;  // 协议编解码器

    FdEvent::ptr fd_event_;  // 文件描述符事件对象

    bool stop_{false};  // 停止标志

    bool is_over_time_{false};  // 超时标志

    std::map<std::string, std::shared_ptr<TinyPbStruct>> reply_datas_;  // 客户端用于存储响应数据的map

    std::weak_ptr<AbstractSlot<TcpConnection>> weak_slot_;  // 时间轮槽位的弱引用

    std::shared_mutex rw_mtx_;  // 读写锁

    void clearClient();
};

}  // namespace tinyrpc