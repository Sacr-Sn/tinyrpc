#pragma once

#include <string>
#include <vector>

#include "abstract_data.h"
#include "log.h"

/**
 * 定义了TinyPB协议的数据结构，是TinyPB协议层的核心数据结构。
 * 用于表示TinyPB协议的请求和响应数据
*/

namespace tinyrpc {

class TinyPbStruct : public AbstractData {

public:
    typedef std::shared_ptr<TinyPbStruct> pb_ptr;

    /* 协议头字段 */
    std::string msg_req;  // 消息请求编号，用于追踪RPC调用
    std::string service_full_name;  // 完整的服务方法名(如QueryService.query_age)
    int32_t err_code {0};  // 框架级错误码
    std::string err_info;  // 详细错误信息
    std::string pb_data;  // 业务protobuf序列化数据

    /* 协议元数据 */
    int32_t pk_len {0};  // 整个包的长度
    int32_t msg_req_len {0};  // msg_req 字符串长度
    int32_t service_name_len {0};  // service_full_name 长度
    int32_t err_info_len {0};  // err_info 长度
    int32_t check_num {-1};  // 包校验和

    TinyPbStruct() = default;

    TinyPbStruct(const TinyPbStruct&) = default;
    TinyPbStruct& operator=(const TinyPbStruct&) = default;

    TinyPbStruct(TinyPbStruct&&) = default;
    TinyPbStruct& operator=(TinyPbStruct&&) = default;

    ~TinyPbStruct() = default;


};

}

