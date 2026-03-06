#include <arpa/inet.h>
#include <sys/socket.h>

#include <tinyrpc/comm/error_code.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/coroutine/coroutine_pool.h>
#include <tinyrpc/net/comm/fd_event.h>
#include <tinyrpc/net/comm/net_address.h>
#include <tinyrpc/net/comm/timer.h>
#include <tinyrpc/net/http/http_codec.h>
#include <tinyrpc/net/tcp/tcp_client.h>
#include <tinyrpc/net/tinypb/tinypb_codec.h>

namespace tinyrpc {

TcpClient::TcpClient(NetAddress::ptr addr, ProtocolType type) : peer_addr_(addr) {
    family_ = peer_addr_->getFamily();
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == -1) {
        ErrorLog << "call socket error, fd=-1, sys error = " << strerror(errno);
    }
    DebugLog << "TcpClient() create fd = " << fd_;
    local_addr_ = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 0);  // 系统自动分配端口
    reactor_ = Reactor::GetReactor();                                    // 获取当前线程的Reactor实例

    // 根据协议类型创建编解码器
    if (type == Http_Protocol) {
        codec_ = std::make_shared<HttpCodeC>();
    } else {
        codec_ = std::make_shared<TinyPbCodeC>();
    }

    // 创建连接对象，传入this指针、reactor、fd等参数
    connection_ = std::make_shared<TcpConnection>(this, reactor_, fd_, 128, peer_addr_);
}

TcpClient::~TcpClient() {
    if (fd_ > 0) {
        FdEventContainer::GetFdContainer()->getFdEvent(fd_)->unregisterFromReactor();
    }
    close(fd_);
    DebugLog << "~TcpClient() close fd = " << fd_;
}

TcpConnection* TcpClient::getConnection() {
    if (!connection_.get()) {
        connection_ = std::make_shared<TcpConnection>(this, reactor_, fd_, 128, peer_addr_);
    }
    return connection_.get();
}

void TcpClient::resetFd() {
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(fd_);
    fd_event->unregisterFromReactor();
    close(fd_);
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == -1) {
        ErrorLog << "call socket error, fd=-1, sys error=" << strerror(errno);
    } else {
    }
}

/**
 * 这是TcpClient最重要的方法，实现了完整的RPC调用流程。
 * 完整的 RPC 调用流程:
 *  1.创建 TcpClient:传入服务器地址
 *  2.设置地址信息:将本地和对端地址设置到 controller
 *  3.序列化请求:将 protobuf 消息序列化到 TinyPbStruct
 *  4.编码数据:使用 codec 将数据编码到输出缓冲区
 *  5.设置超时:调用 setTimeout() 设置超时时间
 *  6.发送并接收:调用 sendAndRecvTinyPb() 完成通信
 *  7.处理响应:解析响应数据并检查错误
 */
int TcpClient::sendAndRecvTinyPb(const std::string& msg_no, TinyPbStruct::pb_ptr& res) {
    // 步骤一：设置超时定时器
    bool is_timeout = false;
    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();
    auto timer_cb = [this, &is_timeout, cur_cor]() {  // 创建超时回调
        InfoLog << "TcpClient timer out event occur";
        is_timeout = true;
        this->connection_->setOverTimeFlag(true);
        tinyrpc::Coroutine::Resume(cur_cor);
    };
    // 添加定时器事件到Reactor
    TimerEvent::ptr event = std::make_shared<TimerEvent>(max_timeout_, false, timer_cb);
    reactor_->getTimer()->addTimerEvent(event);
    DebugLog << "add rpc timer event, timeout on " << event->arrive_time;

    while (!is_timeout) {
        DebugLog << "begin to connect";
        // 步骤2：建立连接
        // 检查连接状态，如果未连接则调用connect_hook()
        if (connection_->getState() != Connected) {
            int rt =
                connect_hook(fd_, reinterpret_cast<sockaddr*>(peer_addr_->getSockAddr()), peer_addr_->getSockLen());
            if (rt == 0) {  // 连接成功
                DebugLog << "connect [" << peer_addr_->toString() << "] succ";
                connection_->setUpClient();  // 设置连接状态
                break;
            }
            resetFd();  // 连接失败，重置socket
            if (is_timeout) {
                InfoLog << "connect timeout, break";
                goto err_deal;
            }
            if (errno == ECONNREFUSED) {  // 对端关闭
                err_info_ = std::format("connect error, peer[ {} ] closed", peer_addr_->toString());
                ErrorLog << "cancel overtime event, err info = " << err_info_;
                reactor_->getTimer()->delTimerEvent(event);
                return ERROR_PEER_CLOSED;
            }
            if (errno == EAFNOSUPPORT) {  // 系统错误
                err_info_ =
                    std::format("connect cur sys error, err_info is [ {} ] closed", std::string(strerror(errno)));
                ErrorLog << "cancel overtime event, err info = " << err_info_;
                reactor_->getTimer()->delTimerEvent(event);
                return ERROR_CONNECT_SYS_ERR;
            }
        } else {
            break;
        }
    }

    // 步骤3：发送数据
    // 再次检查连接状态
    if (connection_->getState() != Connected) {
        err_info_ =
            std::format("connect peer addr [ {} ] error. sys error = {}", peer_addr_->toString(), strerror(errno));
        reactor_->getTimer()->delTimerEvent(event);
        return ERROR_FAILED_CONNECT;
    }

    connection_->setUpClient();            // 确保客户端设置完成
    connection_->output();                 // 发送数据
    if (connection_->getOverTimeFlag()) {  // 检查是否超时
        InfoLog << "send data over time";
        is_timeout = true;
        goto err_deal;
    }

    // 步骤4：接收响应
    while (!connection_->getResPackageData(msg_no, res)) {  // 尝试获取响应数据
        DebugLog << "redo getResPackageData";
        connection_->input();  // 如果没有完整响应，调用input()继续读取

        if (connection_->getOverTimeFlag()) {  // 检查超时
            InfoLog << "read data over time";
            is_timeout = true;
            goto err_deal;
        }
        if (connection_->getState() == Closed) {  // 检查连接状态
            InfoLog << "peer close";
            goto err_deal;
        }

        connection_->execute();  // 处理接收到的数据
    }

    // 步骤5：清理和返回
    reactor_->getTimer()->delTimerEvent(event);
    err_info_ = "";
    return 0;

// 步骤6：错误处理
err_deal:
    FdEventContainer::GetFdContainer()->getFdEvent(fd_)->unregisterFromReactor();
    close(fd_);
    fd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (is_timeout) {
        err_info_ = std::format("call rpc failed over {} ms", max_timeout_);
        connection_->setOverTimeFlag(false);
        return ERROR_RPC_CALL_TIMEOUT;
    } else {
        err_info_ = std::format("call rpc failed, peer closed [ {} ]", peer_addr_->toString());
        return ERROR_PEER_CLOSED;
    }
}

void TcpClient::stop() {
    if (!is_stop_) {
        is_stop_ = true;
        reactor_->stop();
    }
}

}  // namespace tinyrpc
