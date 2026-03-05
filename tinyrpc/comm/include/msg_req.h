#pragma once

#include <string>

/**
 * 是TinyRPC中的消息请求编号生成器，用于为每个RPC请求生成唯一的标识符(msg_req)。
 * 这个编号在整个RPC调用链路中用于追踪和关联请求响应，是日志追踪和问题排查的关键。
*/

namespace tinyrpc {

class MsgReqUtil {

public:
    /**
     * 生成唯一的消息请求编号
    */
    static std::string genMsgNumber();

};

}