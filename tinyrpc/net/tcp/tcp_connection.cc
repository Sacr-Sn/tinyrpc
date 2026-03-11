#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <tinyrpc/coroutine/coroutine.h>
#include <tinyrpc/coroutine/coroutine_hook.h>
#include <tinyrpc/coroutine/coroutine_pool.h>
#include <tinyrpc/net/comm/timer.h>
#include <tinyrpc/net/tcp/abstract_slot.h>
#include <tinyrpc/net/tcp/tcp_client.h>
#include <tinyrpc/net/tcp/tcp_connection.h>
#include <tinyrpc/net/tcp/tcp_connection_time_wheel.h>
#include <tinyrpc/net/tcp/tcp_server.h>
#include <tinyrpc/net/tinypb/tinypb_codec.h>
#include <tinyrpc/net/tinypb/tinypb_data.h>

namespace tinyrpc {

TcpConnection::TcpConnection(tinyrpc::TcpServer *tcp_svr, tinyrpc::IOThread *io_thread, int fd, int buff_size,
                             NetAddress::ptr peer_addr)
    : io_thread_(io_thread), fd_(fd), connection_type_(ServerConnection), peer_addr_(peer_addr) {
    reactor_ = io_thread_->getReactor();

    tcp_server_ = tcp_svr;
    codec_ = tcp_server_->getCodec();

    fd_event_ = FdEventContainer::GetFdContainer()->getFdEvent(fd);
    fd_event_->setReactor(reactor_);
    initBuffer(buff_size);
    loop_cor_ = GetCoroutinePool()->getCoroutineInstance();
    state_ = TcpConnectionState::Connected;
    DebugLog << "succ create tcp connection[" << state_ << "], fd = " << fd;
}

TcpConnection::TcpConnection(tinyrpc::TcpClient *tcp_cli, tinyrpc::Reactor *reactor, int fd, int buff_size,
                             NetAddress::ptr peer_addr)
    : fd_(fd), state_(TcpConnectionState::NotConnected), connection_type_(ClientConnection), peer_addr_(peer_addr) {
    reactor_ = reactor;

    tcp_client_ = tcp_cli;

    codec_ = tcp_client_->getCodeC();

    fd_event_ = FdEventContainer::GetFdContainer()->getFdEvent(fd);
    fd_event_->setReactor(reactor_);
    initBuffer(buff_size);
    DebugLog << "succ create tcp connection[NotConnected]";
}

TcpConnection::~TcpConnection() {
    if (connection_type_ == ServerConnection) {
        GetCoroutinePool()->returnCoroutine(loop_cor_);
    }

    DebugLog << "~TcpConnection, fd = " << fd_;
}

void TcpConnection::initBuffer(int size) {
    // 初始化缓冲区大小
    write_buffer_ = std::make_shared<TcpBuffer>(size);
    read_buffer_ = std::make_shared<TcpBuffer>(size);
}

void TcpConnection::initServer() {
    registerToTimeWheel();  // 注册到时间轮
    // 设置协程回调函数
    loop_cor_->setCallBack(std::bind(&TcpConnection::MainServerLoopCorFunc, this));
}

void TcpConnection::setUpServer() { reactor_->addCoroutine(loop_cor_); }

void TcpConnection::registerToTimeWheel() {
    // 超时回调函数，调用shutdownConnection()
    auto cb = [](TcpConnection::ptr conn) { conn->shutdownConnection(); };
    // 创建slot对象，传入自身的shared_ptr和回调函数
    TcpTimeWheel::TcpConnectionSlot::ptr tmp = std::make_shared<AbstractSlot<TcpConnection>>(shared_from_this(), cb);
    weak_slot_ = tmp;
    // 调用TcpServer的freshTcpConnection()将槽位加入时间轮
    tcp_server_->freshTcpConnection(tmp);
}

void TcpConnection::setUpClient() { setState(TcpConnectionState::Connected); }

void TcpConnection::MainServerLoopCorFunc() {
    // 服务端连接处理的核心循环
    while (!stop_) {
        input();  // 读取数据

        execute();  // 处理数据

        output();  // 写入数据
    }
    InfoLog << "this connection has already end loop";
}

// 读取数据
void TcpConnection::input() {
    if (is_over_time_) {  // 如果超时，直接返回
        InfoLog << "over timer, skip input progress";
    }
    TcpConnectionState state = getState();
    if (state == Closed || state == NotConnected) {  // 如果连接已关闭，直接返回
        return;
    }

    bool read_all = false;  // 是否已读完
    bool close_flag = false;
    int count = 0;
    while (!read_all) {                        // 循环读取数据，直到读完
        if (read_buffer_->writeAble() == 0) {  // 检查写缓冲区是否还有空间
            // 没有空间，扩容为2倍
            read_buffer_->resizeBuffer(2 * read_buffer_->getSize());
        }

        int read_count = read_buffer_->writeAble();
        int write_index = read_buffer_->writeIndex();
        DebugLog << "read_buffer_ size = " << read_buffer_->getBufferVector().size()
                 << ", rd = " << read_buffer_->readIndex() << ", wd = " << read_buffer_->writeIndex();
        // 调用read_hook()从socket读取数据到缓冲区
        DebugLog << "read event triggered, fd=" << fd_;
        int rt = read_hook(fd_, &(read_buffer_->buffer[write_index]), read_count);
        DebugLog << "read result, rt=" << rt << ", errno=" << errno;

        if (rt > 0) {
            read_buffer_->recycleWrite(rt);  // 更新写索引
        }
        DebugLog << "read_buffer_ size = " << read_buffer_->getBufferVector().size()
                 << ", rd = " << read_buffer_->readIndex() << ", wd = " << read_buffer_->writeIndex();

        DebugLog << "read data back, fd = " << fd_;
        count += rt;  // 累加已读数据长度
        if (is_over_time_) {
            InfoLog << "over time, now break read function";
            break;
        }
        if (rt == 0) {
            // 对端正常关闭连接
            InfoLog << "peer close connection (EOF), fd=" << fd_;
            close_flag = true;
            break;
        } else if (rt == -1) {
            if (errno == EAGAIN) {
                // 非阻塞，暂无数据，应该继续等待
                DebugLog << "read would block, wait next event";
                break;
            } else {
                // 真正的错误
                ErrorLog << "read error, fd=" << fd_ << ", errno=" << strerror(errno);
                close_flag = true;
                break;
            }
        } else {
            if (rt == read_count) {  // 说明可能还有更多数据，继续读取
                DebugLog << "read_count == rt";
                continue;
            } else if (rt < read_count) {  // 已读完所有数据，跳出循环
                DebugLog << "read_count > rt";
                read_all = true;
                break;
            }
        }
        // if (rt <= 0) {  // 对端关闭
        //     ErrorLog << "read empty while occur read event, because of peer "
        //                 "close, fd = "
        //              << fd_ << ", sys error = [" << strerror(errno) << "], now to clear tcp connection";
        //     close_flag = true;
        //     break;
        // } else {
        //     if (rt == read_count) {  // 说明可能还有更多数据，继续读取
        //         DebugLog << "read_count == rt";
        //         continue;
        //     } else if (rt < read_count) {  // 已读完所有数据，跳出循环
        //         DebugLog << "read_count > rt";
        //         read_all = true;
        //         break;
        //     }
        // }
    }
    if (close_flag) {
        clearClient();  // 清理连接
        DebugLog << "peer close, now yield current coroutine, wait main thread "
                    "clear this TcpConnection";
        // 设置协程不可恢复并Yield
        Coroutine::GetCurrentCoroutine()->setCanResume(false);
        Coroutine::Yield();
    }

    if (is_over_time_) {  // 因超时而退出循环
        return;
    }

    if (!read_all) {  // 没有读完却退出循环
        ErrorLog << "not read all data in socket buffer";
    }
    InfoLog << "recv [" << count << "] bytes data from [" << peer_addr_->toString() << "], fd [" << fd_ << "]";
    if (connection_type_ == ServerConnection) {  // 如果是服务端连接
        TcpTimeWheel::TcpConnectionSlot::ptr tmp = weak_slot_.lock();
        if (tmp) {
            // 刷新时间轮，重置超时时间
            tcp_server_->freshTcpConnection(tmp);
        }
    }
}

// 处理数据
void TcpConnection::execute() {
    while (read_buffer_->readAble() > 0) {
        // 创建协议数据对象
        std::shared_ptr<AbstractData> data;
        if (codec_->getProtocolType() == TinyPb_Protocol) {
            data = std::make_shared<TinyPbStruct>();
        } else {
            data = std::make_shared<HttpRequest>();
        }

        // 解码数据
        codec_->decode(read_buffer_.get(), data.get());
        if (!data->decode_succ) {
            ErrorLog << "it parse request error of fd " << fd_;
            break;
        }

        if (connection_type_ == ServerConnection) {  // 服务端连接
            // 调用dispatcher的dispatch()方法分发到对应的处理器
            tcp_server_->getDispatcher()->dispatch(data.get(), this);
        } else if (connection_type_ == ClientConnection) {  // 客户端连接
            // 将响应数据reply_datas_as map中，供客户端获取
            std::shared_ptr<TinyPbStruct> tmp = std::dynamic_pointer_cast<TinyPbStruct>(data);
            if (tmp) {
                reply_datas_.emplace(std::make_pair(tmp->msg_req, tmp));
            }
        }
    }
}

void TcpConnection::output() {
    if (is_over_time_) {  // 检查超时
        InfoLog << "over timer, skip output progress";
        return;
    }

    while (true) {
        TcpConnectionState state = getState();
        if (state != Connected) {
            break;
        }
        if (write_buffer_->readAble() == 0) {  // 写缓冲区无数据
            DebugLog << "app buffer of fd[" << fd_ << "] no data to write, to yield this coroutine";
            break;
        }

        int total_size = write_buffer_->readAble();
        int read_index = write_buffer_->readIndex();
        int rt = write_hook(fd_, &(write_buffer_->buffer[read_index]), total_size);
        if (rt <= 0) {
            ErrorLog << "write empty, error = " << strerror(errno);
        }
        DebugLog << "succ write " << rt << " bytes";
        write_buffer_->recycleRead(rt);  // 更新读索引
        if (write_buffer_->readAble() <= 0) {
            InfoLog << "send all data, now unregister write event and break";
            break;
        }

        if (is_over_time_) {
            InfoLog << "over timer, now break write function";
            break;
        }
    }
}

// 清理连接
void TcpConnection::clearClient() {
    if (getState() == Closed) {
        DebugLog << "this client has closed";
        return;
    }

    // 从Reactor注销FdEvent
    fd_event_->unregisterFromReactor();

    stop_ = true;

    close(fd_event_->getFd());
    setState(TcpConnectionState::Closed);
}

/**
 * 这里只是半关闭，等待客户端也发送FIN，
 * 然后在input()中检测到对端关闭时才调用clearClient()完全关闭连接。
 */
void TcpConnection::shutdownConnection() {
    TcpConnectionState state = getState();
    if (state == TcpConnectionState::Closed || state == TcpConnectionState::NotConnected) {
        DebugLog << "this client has closed";
        return;
    }
    // 设置状态为半关闭
    setState(TcpConnectionState::HalfClosing);
    InfoLog << "shutdown conn[" << peer_addr_->toString() << "], fd = " << fd_;
    // 调用系统shutdown()，发送FIN包
    shutdown(fd_event_->getFd(), SHUT_RDWR);
}

TcpBuffer *TcpConnection::getInBuffer() { return read_buffer_.get(); }

TcpBuffer *TcpConnection::getOutBuffer() { return write_buffer_.get(); }

// 客户顿reply_datas_s中查找并返回响应数据
bool TcpConnection::getResPackageData(const std::string &msg_req, TinyPbStruct::pb_ptr &pb_struct) {
    auto it = reply_datas_.find(msg_req);
    if (it != reply_datas_.end()) {
        DebugLog << "return a resdata";
        pb_struct = it->second;
        reply_datas_.erase(it);
        return true;
    }
    DebugLog << msg_req << "|reply data not exist";
    return false;
}

AbstractCodeC::ptr TcpConnection::getCodec() const { return codec_; }

TcpConnectionState TcpConnection::getState() {
    TcpConnectionState state;
    {
        std::shared_lock<std::shared_mutex> read_lock(rw_mtx_);
        state = state_;
    }
    return state;
}

void TcpConnection::setState(const TcpConnectionState &state) {
    std::unique_lock<std::shared_mutex> write_lock(rw_mtx_);
    state_ = state;
}

void TcpConnection::setOverTimeFlag(bool value) { is_over_time_ = value; }

bool TcpConnection::getOverTimeFlag() { return is_over_time_; }

Coroutine::ptr TcpConnection::getCoroutine() { return loop_cor_; }

}  // namespace tinyrpc
