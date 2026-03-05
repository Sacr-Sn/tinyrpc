#pragma once

/**
 * 存储和传递 RPC 调用过程中的运行时上下文信息
 * 每个协程持有一个RunTime对象
 * 
 * RunTime 是一个轻量级的上下文传递机制,通过协程的 thread-local 存储实现。
 * 它避免了在函数调用链中显式传递上下文参数,使得在任何地方都能获取当前请求的追踪信息。
 * 这对于分布式系统的日志追踪和问题排查非常重要
*/

#include <string>

namespace tinyrpc {

class RunTime {
public:
    std::string msg_no;  // 消息请求编号，用于追踪和关联请求响应
    std::string interface_name;  // 接口名称，标识正在调用的服务方法
};

}