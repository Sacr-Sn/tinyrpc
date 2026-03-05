#pragma once

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <map>
#include <memory>

#include "abstract_dispatcher.h"
#include "tinypb_data.h"

/**
 * 是TinyRpc中TinyPB协议的请求分发器，负责将解码后的TinyPB请求路由到对应的Protobuf Service方法进行处理。
 */

namespace tinyrpc {

class TinyPbRpcDispatcher : public AbstractDispatcher {
   public:
    typedef std::shared_ptr<google::protobuf::Service> service_ptr;

    // 存储服务名到 Protobuf Service 对象的映射
    std::map<std::string, service_ptr> service_map;

    TinyPbRpcDispatcher() = default;

    ~TinyPbRpcDispatcher() = default;

    void dispatch(AbstractData* data, TcpConnection* conn);

    /**
     * 解析服务全名
     * 输入:"QueryService.query_age"
     * 输出:service_name = "QueryService", method_name = "query_age"
     */
    bool parseServiceFullName(const std::string& full_name, std::string& service_name, std::string& method_name);

    // 注册服务
    void registerService(service_ptr service);
};

}  // namespace tinyrpc