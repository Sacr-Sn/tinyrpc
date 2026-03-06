#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>

#include <tinyrpc/comm/error_code.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/comm/msg_req.h>
#include <tinyrpc/comm/run_time.h>
#include <tinyrpc/net/tinypb/tinypb_codec.h>
#include <tinyrpc/net/tinypb/tinypb_data.h>
#include <tinyrpc/net/tinypb/tinypb_rpc_channel.h>
#include <tinyrpc/net/tinypb/tinypb_rpc_controller.h>

namespace tinyrpc {

TinyPbRpcChannel::TinyPbRpcChannel(NetAddress::ptr addr) : addr_(addr) {
    DebugLog << "constructing TinyPbRpcChannel, addr: " << addr->toString();
}

// 负责执行实际的 RPC 调用
void TinyPbRpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                                  google::protobuf::RpcController* controller, const google::protobuf::Message* request,
                                  google::protobuf::Message* response, google::protobuf::Closure* done) {
    /**
     * 准备数据结构和控制器
     */
    TinyPbStruct pb_struct;  // 用于存储协议数据
    TinyPbRpcController* rpc_controller = dynamic_cast<TinyPbRpcController*>(controller);
    if (!rpc_controller) {
        ErrorLog << "call failed. failed to dynamic cast TinyPbRpcController";
        return;
    }
    TcpClient::ptr m_client = std::make_shared<TcpClient>(addr_);
    rpc_controller->SetLocalAddr(m_client->getLocalAddr());  // 设置本地地址到控制器
    rpc_controller->SetPeerAddr(m_client->getPeerAddr());    // 设置对端地址到控制器

    /**
     * 设置服务名和序列化请求
     */
    pb_struct.service_full_name = method->full_name();
    DebugLog << "call service_name = " << pb_struct.service_full_name;  // 获取完整的服务方法名
    if (!request->SerializeToString(&(pb_struct.pb_data))) {            // 序列化请求信息
        ErrorLog << "serialize send package error";
        return;
    }

    /**
     * 生成或获取消息编号
     */
    if (!rpc_controller->MsgSeq().empty()) {  // 检查控制器是否已设置消息编号
        pb_struct.msg_req = rpc_controller->MsgSeq();
    } else {
        RunTime* run_time = getCurrentRunTime();
        if (run_time != NULL && !run_time->msg_no.empty()) {  // 尝试从当前协程的RunTime获取
            pb_struct.msg_req = run_time->msg_no;
            DebugLog << "get from RunTime succ, msgno = " << pb_struct.msg_req;
        } else {
            pb_struct.msg_req = MsgReqUtil::genMsgNumber();  // 生成新的消息编号
            DebugLog << "get from RunTime error, generate new msgno = " << pb_struct.msg_req;
        }
        rpc_controller->SetMsgReq(pb_struct.msg_req);
    }

    /**
     * 编码协议数据
     */
    AbstractCodeC::ptr codec_ = m_client->getConnection()->getCodec();      // 获取TcpClient的codec
    codec_->encode(m_client->getConnection()->getOutBuffer(), &pb_struct);  // 将TinyPbStruct编码到输出缓冲区
    if (!pb_struct.encode_succ) {
        rpc_controller->SetError(ERROR_FAILED_ENCODE, "encode tinypb data error");
        return;
    }

    /**
     * 记录请求日志
     */
    InfoLog << "============================================================";
    InfoLog << pb_struct.msg_req << "|" << rpc_controller->PeerAddr()->toString()
            << "|. Set client send request data:" << request->ShortDebugString();
    InfoLog << "============================================================";

    /**
     * 发送请求并等待响应
     * 这里会阻塞当前协程，但不会阻塞线程
     */
    m_client->setTimeout(rpc_controller->Timeout());  // 设置超时时间
    TinyPbStruct::pb_ptr res_data;
    int rt = m_client->sendAndRecvTinyPb(pb_struct.msg_req, res_data);  // 发送请求并等待响应
    if (rt != 0) {                                                      // 网络层错误
        rpc_controller->SetError(rt, m_client->getErrInfo());           // 设置错误码
        ErrorLog << pb_struct.msg_req
                 << "|call rpc occur client error, service_full_name = " << pb_struct.service_full_name
                 << ", error_code = " << rt << ", error_info = " << m_client->getErrInfo();
        return;
    }

    /**
     * 反序列化响应
     * 反序列化后的数据被解析并存储到了response中
     */
    if (!response->ParseFromString(res_data->pb_data)) {
        rpc_controller->SetError(ERROR_FAILED_DESERIALIZE, "faliled to deserialize data from server");
        ErrorLog << pb_struct.msg_req << "|failed to deserialize data";
        return;
    }

    /**
     * 检查业务层错误
     */
    if (res_data->err_code != 0) {
        ErrorLog << pb_struct.msg_req << "|server reply error_code = " << res_data->err_code
                 << ", err_info = " << res_data->err_info;
        rpc_controller->SetError(res_data->err_code, res_data->err_info);
        return;
    }

    /**
     * 记录成功日志并执行回调
     */
    InfoLog << "============================================================";
    InfoLog << pb_struct.msg_req << "|" << rpc_controller->PeerAddr()->toString() << "|call rpc server ["
            << pb_struct.service_full_name << "] succ"
            << ". Get server reply response data:" << response->ShortDebugString();
    InfoLog << "============================================================";
    if (done) {
        done->Run();
    }
}

}  // namespace tinyrpc