#pragma once

#include <memory>
#include <string>

// #include <tinyrpc/net/comm/abstract_data.h>
#include <tinyrpc/net/comm/abstract_data.h>
// #include <tinyrpc/net/tcp/tcp_buffer.h>
#include <tinyrpc/net/tcp/tcp_buffer.h>

/**
 * 定义了TinyRPC中协议编解码器的抽象基类，
 * 为不同协议（HTTP和TinyPB）提供统一的编解码接口。
 * 是协议层与传输层解耦的关键设计
 */

namespace tinyrpc {

enum ProtocolType { TinyPb_Protocol = 1, Http_Protocol = 2 };

class AbstractCodeC {
   public:
    typedef std::shared_ptr<AbstractCodeC> ptr;

    AbstractCodeC() {}

    virtual ~AbstractCodeC() {}

    // 将协议数据编码到缓冲区
    virtual void encode(TcpBuffer* buf, AbstractData* data) = 0;

    // 从缓冲区解码协议数据
    virtual void decode(TcpBuffer* buf, AbstractData* data) = 0;

    // 获取协议类型（Http或TinyPB）
    virtual ProtocolType getProtocolType() = 0;
};

}  // namespace tinyrpc