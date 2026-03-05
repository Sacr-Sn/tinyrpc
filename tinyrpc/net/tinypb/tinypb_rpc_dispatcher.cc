#include "tinypb_rpc_dispatcher.h"
#include "tinypb_codec.h"
#include "tinypb_rpc_closure.h"

#include "error_code.h"
#include "msg_req.h"
#include "tinypb_rpc_controller.h"

namespace tinyrpc {

class TcpBuffer;

void TinyPbRpcDispatcher::dispatch(AbstractData* data, TcpConnection* conn) {
    TinyPbStruct* tmp = dynamic_cast<TinyPbStruct*>(data);
    if (tmp == nullptr) {
        ErrorLog << "dynamic_cast error";
        return;
    }

    // 设置当前协程的运行时上下文
    Coroutine::GetCurrentCoroutine()->getRunTime()->msg_no = tmp->msg_req;
    setCurrentRunTime(Coroutine::GetCurrentCoroutine()->getRunTime());

    InfoLog << "begin to dispatch client tinypb request, msgno = " << tmp->msg_req;

    std::string service_name;
    std::string method_name;
    TinyPbStruct reply_pk;  // 响应对象
    reply_pk.service_full_name = tmp->service_full_name;
    reply_pk.msg_req = tmp->msg_req;
    if (reply_pk.msg_req.empty()) {
        reply_pk.msg_req = MsgReqUtil::genMsgNumber();
    }

    // 解析完整服务名和方法名
    if (!parseServiceFullName(tmp->service_full_name, service_name, method_name)) {
        ErrorLog << reply_pk.msg_req << "|parse service name " << tmp->service_full_name << "error";
        reply_pk.err_code = ERROR_PARSE_SERVICE_NAME;
        reply_pk.err_info = std::format("cannot parse service_name:[{}]", tmp->service_full_name);
        conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
        return;
    }

    Coroutine::GetCurrentCoroutine()->getRunTime()->interface_name = tmp->service_full_name;
    auto it = service_map.find(service_name);          // 查找服务
    if (it == service_map.end() || !((*it).second)) {  // 服务不存在
        reply_pk.err_code = ERROR_SERVICE_NOT_FOUND;
        reply_pk.err_info = std::format("not found service_name:[{}]", service_name);
        ErrorLog << reply_pk.msg_req << "|" << reply_pk.err_info;
        conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));

        InfoLog << "end dispatch client tinypb request, msgno = " << tmp->msg_req;
        return;
    }

    service_ptr service = (*it).second;
    // 使用Protobuf的反射机制查找方法描述符
    const google::protobuf::MethodDescriptor* method = service->GetDescriptor()->FindMethodByName(method_name);
    if (!method) {
        reply_pk.err_code = ERROR_METHOD_NOT_FOUND;
        reply_pk.err_info = std::format("not found method_name:[{}]", method_name);
        ErrorLog << reply_pk.msg_req << "|" << reply_pk.err_info;
        conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
        return;
    }

    // 使用Protobuf的反射机制创建请求对象
    google::protobuf::Message* request = service->GetRequestPrototype(method).New();
    DebugLog << reply_pk.msg_req << "|request.name = " << request->GetDescriptor()->full_name();
    // 从pb_data字段反序列化请求数据
    if (!request->ParseFromString(tmp->pb_data)) {
        reply_pk.err_code = ERROR_FAILED_SERIALIZE;
        reply_pk.err_info =
            std::format("failed to parse request data, request.name:[{}]", request->GetDescriptor()->full_name());
        delete request;
        conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
        return;
    }

    InfoLog << "============================================================";
    InfoLog << reply_pk.msg_req << "|Get client request data:" << request->ShortDebugString();
    InfoLog << "============================================================";

    // 创建响应Message对象
    google::protobuf::Message* response = service->GetResponsePrototype(method).New();
    DebugLog << reply_pk.msg_req << "|response.name = " << response->GetDescriptor()->full_name();
    // 创建控制器并设置消息编号、方法名等
    TinyPbRpcController rpc_controller;
    rpc_controller.SetMsgReq(reply_pk.msg_req);
    rpc_controller.SetMethodName(method_name);
    rpc_controller.SetMethodFullName(tmp->service_full_name);

    // 创建空的TinyPbRpcClosure回调
    std::function<void()> reply_package_func = []() {};  // RPC调用完成后执行
    TinyPbRpcClosure closure(reply_package_func);
    // 调用实际的服务方法
    service->CallMethod(method, &rpc_controller, request, response, &closure);
    InfoLog << "Call [" << reply_pk.service_full_name << "] succ, now send reply package";

    // 序列化响应
    if (!(response->SerializeToString(&(reply_pk.pb_data)))) {
        reply_pk.pb_data = "";
        ErrorLog << reply_pk.msg_req << "|reply error! encode reply package error";
        reply_pk.err_code = ERROR_FAILED_SERIALIZE;
        reply_pk.err_info = "failed to serialize reply data";
    } else {
        InfoLog << "============================================================";
        InfoLog << reply_pk.msg_req << "|Set server response data:" << response->ShortDebugString();
        InfoLog << "============================================================";
    }

    delete request;
    delete response;

    // 将响应编码到输出缓冲区，准备发送给客户端
    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
}

// 解析服务全名
bool TinyPbRpcDispatcher::parseServiceFullName(const std::string& full_name, std::string& service_name,
                                               std::string& method_name) {
    if (full_name.empty()) {
        ErrorLog << "service_full_name empty";
        return false;
    }
    std::size_t i = full_name.find(".");
    if (i == full_name.npos) {
        ErrorLog << "not found [.]";
        return false;
    }

    service_name = full_name.substr(0, i);
    DebugLog << "service_name = " << service_name;
    method_name = full_name.substr(i + 1, full_name.length() - i - 1);
    DebugLog << "method_name = " << method_name;
    return true;
}

void TinyPbRpcDispatcher::registerService(service_ptr service) {
    std::string service_name = service->GetDescriptor()->full_name();
    service_map[service_name] = service;
    InfoLog << "succ register service[" << service_name << "]";
}

}  // namespace tinyrpc