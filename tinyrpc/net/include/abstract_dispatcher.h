#pragma once

#include <memory>
#include <google/protobuf/service.h>

#include "abstract_data.h"
#include "tcp_connection.h"

/**
 * 定义了TinyRPC中请求分发器的抽象基类，为不同协议（Http和TinyPB）提供统一的请求分发接口
 * 它与AbstractCodeC配合，共同构成了协议层的抽象体系
*/

namespace tinyrpc {

class TcpConnection;

class AbstractDispatcher {

public:
    typedef std::shared_ptr<AbstractDispatcher> ptr;

    AbstractDispatcher() {}

    virtual ~AbstractDispatcher() {}

    /**
     * 分发请求到对应的处理器
     * AbstractData* data - 已解码的协议数据
     * TcpConnection* conn - Tcp连接对象，用于写入响应
    */
    virtual void dispatch(AbstractData* data, TcpConnection* conn) = 0;

};

}