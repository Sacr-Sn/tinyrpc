#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>

#include <tinyrpc/comm/config.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/coroutine/coroutine.h>
#include <tinyrpc/coroutine/coroutine_hook.h>
#include <tinyrpc/coroutine/coroutine_pool.h>
#include <tinyrpc/net/http/http_codec.h>
#include <tinyrpc/net/http/http_dispatcher.h>
#include <tinyrpc/net/tcp/io_thread.h>
#include <tinyrpc/net/tcp/tcp_connection.h>
#include <tinyrpc/net/tcp/tcp_connection_time_wheel.h>
#include <tinyrpc/net/tcp/tcp_server.h>
#include <tinyrpc/net/tinypb/tinypb_codec.h>
#include <tinyrpc/net/tinypb/tinypb_rpc_dispatcher.h>

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

TcpAcceptor::TcpAcceptor(NetAddress::ptr net_addr) : local_addr_(net_addr) { family_ = local_addr_->getFamily(); }

void TcpAcceptor::init() {
    fd_ = socket(local_addr_->getFamily(), SOCK_STREAM, 0);
    if (fd_ < 0) {
        ErrorLog << "start server error. socket error, sys error = " << strerror(errno);
        Exit(0);
    }
    DebugLog << "create listenfd succ, listenfd = " << fd_;

    int val = 1;
    // 允许地址重用，避免TIME_WAIT状态导致的绑定失败
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        ErrorLog << "set REUSEADDR error";
    }

    // 将socket绑定到指定地址和端口
    socklen_t len = local_addr_->getSockLen();
    int rt = bind(fd_, local_addr_->getSockAddr(), len);
    if (rt != 0) {
        ErrorLog << "start server error. bind error, errno = " << errno << ", error = " << strerror(errno);
        Exit(0);
    }

    DebugLog << "set REUSEADDR succ";
    rt = listen(fd_, 10);
    if (rt != 0) {
        ErrorLog << "start server error. listen error, fd = " << fd_ << ", errno = " << errno
                 << ", error = " << strerror(errno);
        Exit(0);
    }
}

TcpAcceptor::~TcpAcceptor() {
    FdEvent::ptr fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd_);
    fd_event->unregisterFromReactor();
    if (fd_ != -1) {
        close(fd_);
    }
}

int TcpAcceptor::toAccept() {
    socklen_t len = 0;
    int rt = 0;

    // 根据地址族选择处理方式
    if (family_ == AF_INET) {  // 对应IPv4
        sockaddr_in cli_addr;
        memset(&cli_addr, 0, sizeof(cli_addr));
        len = sizeof(cli_addr);
        // 使用协程友好的accept，如果没有新连接会yield协程
        rt = accept_hook(fd_, reinterpret_cast<sockaddr *>(&cli_addr), &len);
        if (rt == -1) {
            DebugLog << "error, no new client coming, errno = " << errno << "error = " << strerror(errno);
            return -1;
        }
        InfoLog << "New client accepted succ! port:[" << cli_addr.sin_port;
        // 创建对端地址对象 (ipv4对应IPAddress)
        peer_addr_ = std::make_shared<IPAddress>(cli_addr);
    } else if (family_ == AF_UNIX) {
        sockaddr_un cli_addr;
        len = sizeof(cli_addr);
        memset(&cli_addr, 0, sizeof(cli_addr));
        rt = accept_hook(fd_, reinterpret_cast<sockaddr *>(&cli_addr), &len);
        if (rt == -1) {
            DebugLog << "error, no new client coming, errno = " << errno << ", error = " << strerror(errno);
            return -1;
        }
        peer_addr_ = std::make_shared<UnixDomainAddress>(cli_addr);
    } else {
        ErrorLog << "unknown type protocol !";
        close(rt);
        return -1;
    }

    InfoLog << "New client accepted succ! fd:[" << rt << "], addr:[" << peer_addr_->toString() << "]";
    // 返回新连接的客户端文件描述符
    return rt;
}

// 初始化服务器组件
TcpServer::TcpServer(NetAddress::ptr addr, ProtocolType type) : addr_(addr) {
    // 根据配置创建指定数量的IO线程
    io_pool_ = std::make_shared<IOThreadPool>(gRpcConfig->iothread_num_);
    // 根据协议类型创建dispatcher和codec
    if (type == Http_Protocol) {  // Http协议
        dispatcher_ = std::make_shared<HttpDispatcher>();
        codec_ = std::make_shared<HttpCodeC>();
        protocol_type_ = Http_Protocol;
    } else {  // TinyPB协议
        dispatcher_ = std::make_shared<TinyPbRpcDispatcher>();
        codec_ = std::make_shared<TinyPbCodeC>();
        protocol_type_ = TinyPb_Protocol;
    }

    // 获取主 Reactor
    main_reactor_ = tinyrpc::Reactor::GetReactor();
    main_reactor_->setReactorType(MainReactor);

    // 创建时间轮，用于管理连接超时，参数从配置读取
    time_wheel_ = std::make_shared<TcpTimeWheel>(main_reactor_, gRpcConfig->timewheel_bucket_num_,
                                                 gRpcConfig->timewheel_interval_);

    // 创建清理定时器 - 每10秒清理一次已关闭的连接
    clear_client_timer_event_ =
        std::make_shared<TimerEvent>(10000, true, std::bind(&TcpServer::ClearClientTimerFunc, this));
    main_reactor_->getTimer()->addTimerEvent(clear_client_timer_event_);

    InfoLog << "TcpServer setup on [" << addr_->toString() << "]";
}

void TcpServer::start() {
    // 创建并初始化TcpAccept
    acceptor_.reset(new TcpAcceptor(addr_));
    acceptor_->init();

    // 创建接受连接的协程
    accept_cor_ = GetCoroutinePool()->getCoroutineInstance();
    // 设置回调
    accept_cor_->setCallBack(std::bind(&TcpServer::MainAcceptCorFunc, this));

    InfoLog << "resume accept coroutine";
    // 启动接受连接的协程
    tinyrpc::Coroutine::Resume(accept_cor_.get());

    // 启动线程池 - 所有IO线程开始运行Reactor事件循环
    io_pool_->start();
    main_reactor_->loop();  // 主线程进入时间循环，处理连接接受
}

TcpServer::~TcpServer() {
    GetCoroutinePool()->returnCoroutine(accept_cor_);
    DebugLog << "~TcpServer";
}

// 接受连接的协程函数
void TcpServer::MainAcceptCorFunc() {
    while (!is_stop_accept_) {
        // 接受新连接，如果没有连接会yield
        int fd = acceptor_->toAccept();
        if (fd == -1) {
            ErrorLog << "accept ret -1 error, return, to yield";
            Coroutine::Yield();
            continue;
        }
        // 从IO线程池中轮询获取一个IO线程
        IOThread *io_thread = io_pool_->getIOThread();
        // 创建连接对象
        TcpConnection::ptr conn = addClient(io_thread, fd);
        conn->initServer();  // 设置服务端相关参数
        DebugLog << "tcp_connection address is " << conn.get() << ", and fd is " << fd;
        // 将连接的协程添加到IO线程的Reactor中
        io_thread->getReactor()->addCoroutine(conn->getCoroutine());
        tcp_counts_++;  // 更新连接计数
        DebugLog << "current tcp connection count is [" << tcp_counts_ << "]";
    }
}

void TcpServer::addCoroutine(Coroutine::ptr cor) { main_reactor_->addCoroutine(cor); }

/**
 * 注册Protobuf Service
 * 只有TinyPB协议才能注册Service
 * 通过dynamic_cast将dispatcher转换为TinyPbRpcDispatcher并调用其registerService方法
 */
bool TcpServer::registerService(std::shared_ptr<google::protobuf::Service> service) {
    if (protocol_type_ == TinyPb_Protocol) {
        if (service) {
            dynamic_cast<TinyPbRpcDispatcher *>(dispatcher_.get())->registerService(service);
        } else {
            ErrorLog << "register service error, service ptr is nullptr";
            return false;
        }
    } else {
        ErrorLog << "register service error. Just TinyPB protocol server need "
                    "to register Service";
        return false;
    }
    return true;
}

/**
 * 注册HTTP Servlet
 * 只有HTTP协议才能注册Servlet，
 * 通过dynamic_cast将dispatcher转换为HttpDispatcher并调用其registerServlet()方法
 */
bool TcpServer::registerHttpServlet(const std::string &url_path, HttpServlet::ptr servlet) {
    if (protocol_type_ == Http_Protocol) {
        if (servlet) {
            dynamic_cast<HttpDispatcher *>(dispatcher_.get())->registerServlet(url_path, servlet);
        } else {
            ErrorLog << "register http servlet error, servlet ptr is nullptr";
            return false;
        }
    } else {
        ErrorLog << "register http servlet error. Just Http protocol server need to register HttpServlet";
        return false;
    }
    return true;
}

TcpConnection::ptr TcpServer::addClient(IOThread *io_thread, int fd) {
    auto it = clients_.find(fd);
    if (it != clients_.end()) {  // 该fd已存在
        it->second.reset();      // 重置，创建新的TcpConnection对象
        DebugLog << "fd " << fd << " have exist, reset it";
        it->second = std::make_shared<TcpConnection>(this, io_thread, fd, 128, getPeerAddr());
        return it->second;  // 返回连接对象
    } else {
        DebugLog << "fd " << fd << " did't exist, new it";
        TcpConnection::ptr conn = std::make_shared<TcpConnection>(this, io_thread, fd, 128, getPeerAddr());
        clients_.insert(std::make_pair(fd, conn));
        return conn;
    }
}

/**
 * 刷新连接
 * 当连接有数据活动时，需要刷新其在时间轮中的位置，避免被误以为超时。
 * 这个方法通过addTask()将刷新操作添加到主Reactor的任务队列中，因为时间轮运行在主线程
 */
void TcpServer::freshTcpConnection(TcpTimeWheel::TcpConnectionSlot::ptr slot) {
    auto cb = [slot, this]() mutable {
        this->getTimeWheel()->fresh(slot);
        slot.reset();
    };
    main_reactor_->addTask(cb);
}

/**
 * 定期遍clients_s map，检查每个连接的状态。
 * 如果连接状态为Closed且引用计数大于0，则重置智能指针，释放连接对象。
 * 这样可以及时回收内存，避免内存泄漏
 */
void TcpServer::ClearClientTimerFunc() {
    for (auto &i : clients_) {
        if (i.second && i.second.use_count() > 0 && i.second->getState() == Closed) {
            DebugLog << "TcpConnection [fd:" << i.first << "] will delete, state = " << i.second->getState();
            (i.second).reset();
        }
    }
}

// 获取对端地址（客户端）
NetAddress::ptr TcpServer::getPeerAddr() { return acceptor_->getPeerAddr(); }

// 获取本地地址（服务端）
NetAddress::ptr TcpServer::getLocalAddr() { return addr_; }

TcpTimeWheel::ptr TcpServer::getTimeWheel() { return time_wheel_; }

IOThreadPool::ptr TcpServer::getIOThreadPool() { return io_pool_; }

AbstractDispatcher::ptr TcpServer::getDispatcher() { return dispatcher_; }

AbstractCodeC::ptr TcpServer::getCodec() { return codec_; }

}  // namespace tinyrpc