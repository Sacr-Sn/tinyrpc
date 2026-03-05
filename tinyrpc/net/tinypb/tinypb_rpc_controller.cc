#include "tinypb_rpc_controller.h"

namespace tinyrpc {

void TinyPbRpcController::Reset() {}

bool TinyPbRpcController::Failed() const { return is_failed_; }

std::string TinyPbRpcController::ErrorText() const { return error_info_; }

void TinyPbRpcController::StartCancel() {}

void TinyPbRpcController::SetFailed(const std::string& reason) {
    is_failed_ = true;
    error_info_ = reason;
}

bool TinyPbRpcController::IsCanceled() const { return false; }

void TinyPbRpcController::NotifyOnCancel(google::protobuf::Closure* callback) {}

void TinyPbRpcController::SetErrorCode(const int error_code) { error_code_ = error_code; }

int TinyPbRpcController::ErrorCode() const { return error_code_; }

const std::string& TinyPbRpcController::MsgSeq() const { return msg_req_; }

void TinyPbRpcController::SetMsgReq(const std::string& msg_req) { msg_req_ = msg_req; }

void TinyPbRpcController::SetError(const int err_code, const std::string& err_info) {
    SetFailed(err_info);
    SetErrorCode(err_code);
}

void TinyPbRpcController::SetPeerAddr(NetAddress::ptr addr) { peer_addr_ = addr; }

void TinyPbRpcController::SetLocalAddr(NetAddress::ptr addr) { local_addr_ = addr; }

NetAddress::ptr TinyPbRpcController::PeerAddr() { return peer_addr_; }

NetAddress::ptr TinyPbRpcController::LocalAddr() { return local_addr_; }

void TinyPbRpcController::SetTimeout(const int timeout) { timeout_ = timeout; }

int TinyPbRpcController::Timeout() const { return timeout_; }

void TinyPbRpcController::SetMethodName(const std::string& name) { method_name_ = name; }

std::string TinyPbRpcController::GetMethodName() { return method_name_; }

void TinyPbRpcController::SetMethodFullName(const std::string& name) { full_name_ = name; }

std::string TinyPbRpcController::GetMethodFullName() { return full_name_; }

}  // namespace tinyrpc