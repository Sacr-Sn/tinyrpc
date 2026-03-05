#pragma once

/**
 * AbstractData 是 TinyRPC 实现协议抽象的关键设计。
 * 它允许 TCP 层和协议层解耦,使得同一个 TcpConnection 可以处理不同的协议(HTTP 或 TinyPB),
 * 只需要配置不同的 codec 和 dispatcher。
 * 这种设计体现了面向接口编程的思想,是 TinyRPC 支持多协议的基础。
*/

namespace tinyrpc {

class AbstractData {

public:
    AbstractData() = default;

    virtual ~AbstractData() {};

    bool decode_succ {false};  // 标记解码是否成功
    bool encode_succ {false};  // 标记编码是否成功

};

}