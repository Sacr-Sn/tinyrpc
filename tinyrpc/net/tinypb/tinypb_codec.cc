#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "abstract_data.h"
#include "byte.h"
#include "log.h"
#include "msg_req.h"
#include "tinypb_codec.h"
#include "tinypb_data.h"

namespace tinyrpc {

static const char PB_START = 0x02;  // 协议包其实标志
static const char PB_END = 0x03;    // 协议包结束标志
static const int MSG_REQ_LEN = 20;  // 默认消息请求编号长度

TinyPbCodeC::TinyPbCodeC() {}

TinyPbCodeC::~TinyPbCodeC() {}

void TinyPbCodeC::encode(TcpBuffer* buf, AbstractData* data) {
    if (!buf || !data) {
        ErrorLog << "encode error! buf or data nullptr";
        return;
    }

    TinyPbStruct* tmp = dynamic_cast<TinyPbStruct*>(data);
    int len = 0;
    // 调用encodePbData()进行实际编码
    const char* re = encodePbData(tmp, len);
    if (re == nullptr || len == 0 || !tmp->encode_succ) {
        ErrorLog << "encode error";
        data->encode_succ = false;
        return;
    }
    DebugLog << "encode package len = " << len;
    if (buf != nullptr) {
        buf->writeToBuffer(re, len);  // 将编码后的数据写入TcpBuffer
        DebugLog << "succ encode and write to buffer, write_index = " << buf->writeIndex();
    }
    data = tmp;
    if (re) {
        free((void*)re);
        re = NULL;
    }
}

const char* TinyPbCodeC::encodePbData(TinyPbStruct* data, int& len) {
    if (data->service_full_name.empty()) {
        ErrorLog << "parse error, service_full_name is empty";
        data->encode_succ = false;
        return nullptr;
    }
    if (data->msg_req.empty()) {
        data->msg_req = MsgReqUtil::genMsgNumber();
        data->msg_req_len = data->msg_req.length();
        DebugLog << "generate msg_no = " << data->msg_req;
    }

    // 包长度
    int32_t pk_len = 2 * sizeof(char) + 6 * sizeof(int32_t) + data->pb_data.length() +
                     data->service_full_name.length() + data->msg_req.length() + data->err_info.length();

    DebugLog << "encode pk_len = " << pk_len;

    char* buf = reinterpret_cast<char*>(malloc(pk_len));
    char* tmp = buf;
    *tmp = PB_START;  // 编码起始字符
    tmp++;

    int32_t pk_len_net = htonl(pk_len);
    memcpy(tmp, &pk_len_net, sizeof(int32_t));  // 编码包长度（网络字节序）
    tmp += sizeof(int32_t);

    int32_t msg_req_len = data->msg_req.length();
    DebugLog << "msg_req_len = " << msg_req_len;
    int32_t msg_req_len_net = htonl(msg_req_len);
    memcpy(tmp, &msg_req_len_net, sizeof(int32_t));  // 编码消息请求编号长度（网络字节序）
    tmp += sizeof(int32_t);

    if (msg_req_len != 0) {
        memcpy(tmp, &(data->msg_req[0]), msg_req_len);  // 编码消息请求编号
        tmp += msg_req_len;
    }

    int32_t service_full_name_len = data->service_full_name.length();
    DebugLog << "src service_full_name_len = " << service_full_name_len;
    int32_t service_full_name_len_net = htonl(service_full_name_len);
    memcpy(tmp, &service_full_name_len_net, sizeof(int32_t));  // 编码服务名长度（网络字节序）
    tmp += sizeof(int32_t);

    if (service_full_name_len != 0) {
        memcpy(tmp, &(data->service_full_name[0]), service_full_name_len);  // 编码完整服务名
        tmp += service_full_name_len;
    }

    int32_t err_code = data->err_code;
    DebugLog << "err_code = " << err_code;
    int32_t err_code_net = htonl(err_code);
    memcpy(tmp, &err_code_net, sizeof(int32_t));  // 编码错误码（网络字节序）
    tmp += sizeof(int32_t);

    int32_t err_info_len = data->err_info.length();
    DebugLog << "err_info_len = " << err_info_len;
    int32_t err_info_len_net = htonl(err_info_len);
    memcpy(tmp, &err_info_len_net, sizeof(int32_t));  // 编码错误码长度（网络字节序）
    tmp += sizeof(int32_t);

    if (err_info_len != 0) {
        memcpy(tmp, &(data->err_info[0]), err_info_len);  // 编码错误信息
        tmp += err_info_len;
    }

    memcpy(tmp, &(data->pb_data[0]), data->pb_data.length());  // 编码Protobuf 数据
    tmp += data->pb_data.length();
    DebugLog << "pb_data_len = " << data->pb_data.length();

    int32_t checksum = 1;
    int32_t checksum_net = htonl(checksum);
    memcpy(tmp, &checksum_net, sizeof(int32_t));  // 编码校验和（网络字节序）
    tmp += sizeof(int32_t);

    *tmp = PB_END;  // 编码结束字符

    data->pk_len = pk_len;
    data->msg_req_len = msg_req_len;
    data->service_name_len = service_full_name_len;
    data->err_info_len = err_info_len;

    data->check_num = checksum;
    data->encode_succ = true;

    len = pk_len;

    return buf;
}

void TinyPbCodeC::decode(TcpBuffer* buf, AbstractData* data) {
    if (!buf || !data) {
        ErrorLog << "decode error! buf or data nullptr";
        return;
    }

    std::vector<char> tmp = buf->getBufferVector();
    int start_index = buf->readIndex();
    int end_index = -1;
    int32_t pk_len = -1;

    bool parse_full_pack = false;

    // 找到完整包的边界（start、end）
    for (int i = start_index; i < buf->writeIndex(); i++) {
        if (tmp[i] == PB_START) {
            if (i + 1 < buf->writeIndex()) {
                pk_len = getInt32FromNetByte(&tmp[i + 1]);
                DebugLog << "parse pk_len = " << pk_len;
                int j = i + pk_len - 1;
                DebugLog << "j = " << j << ", i = " << i;

                if (j >= buf->writeIndex()) {
                    DebugLog << "recv package not complete, or pk_start find error, continue next parse";
                    continue;
                }
                if (tmp[j] == PB_END) {
                    start_index = i;
                    end_index = j;
                    parse_full_pack = true;
                    break;
                }
            }
        }
    }

    // 解析各个字段
    if (!parse_full_pack) {
        DebugLog << "not parse full package, return";
        return;
    }

    buf->recycleRead(end_index + 1 - start_index);
    DebugLog << "read_buffer_ size = " << buf->getBufferVector().size() << ", rd=" << buf->readIndex()
             << ", wd=" << buf->writeIndex();

    TinyPbStruct* pb_struct = dynamic_cast<TinyPbStruct*>(data);
    // 包长度
    pb_struct->pk_len = pk_len;
    pb_struct->decode_succ = false;

    // 消息请求编号长度
    int msg_req_len_index = start_index + sizeof(char) + sizeof(int32_t);
    if (msg_req_len_index >= end_index) {
        ErrorLog << "parse error, msg_req_len_index[" << msg_req_len_index << "] >= end_index[" << end_index << "]";
        return;
    }
    pb_struct->msg_req_len = getInt32FromNetByte(&tmp[msg_req_len_index]);
    if (pb_struct->msg_req_len == 0) {
        ErrorLog << "parse error, msg_req empty";
        return;
    }
    DebugLog << "msg_req_len = " << pb_struct->msg_req_len;

    // 消息请求内容
    int msg_req_index = msg_req_len_index + sizeof(int32_t);
    DebugLog << "msg_req_len_index = " << msg_req_index;
    char msg_req[50] = {0};
    memcpy(&msg_req[0], &tmp[msg_req_index], pb_struct->msg_req_len);
    pb_struct->msg_req = std::string(msg_req);
    DebugLog << "msg_req = " << pb_struct->msg_req;

    // 服务名长度
    int service_name_len_index = msg_req_index + pb_struct->msg_req_len;
    if (service_name_len_index >= end_index) {
        ErrorLog << "parse error, service_name_len_index[" << service_name_len_index << "] >= end_index[" << end_index
                 << "]";
        return;
    }
    DebugLog << "service_name_len_index = " << service_name_len_index;
    int service_name_index = service_name_len_index + sizeof(int32_t);
    if (service_name_index >= end_index) {
        ErrorLog << "parse error, service_name_index[" << service_name_index << "] >= end_index[" << end_index << "]";
        return;
    }
    pb_struct->service_name_len = getInt32FromNetByte(&tmp[service_name_len_index]);
    if (pb_struct->service_name_len > pk_len) {
        ErrorLog << "parse error, service_name_len[" << pb_struct->service_name_len << "] >= pk_len [" << pk_len << "]";
        return;
    }
    DebugLog << "service_name_len = " << pb_struct->service_name_len;

    // 服务名
    char service_name[512] = {0};
    memcpy(&service_name[0], &tmp[service_name_index], pb_struct->service_name_len);
    pb_struct->service_full_name = std::string(service_name);
    DebugLog << "service_name = " << pb_struct->service_full_name;

    // 错误码
    int err_code_index = service_name_index + pb_struct->service_name_len;
    pb_struct->err_code = getInt32FromNetByte(&tmp[err_code_index]);

    // 错误信息长度
    int err_info_len_index = err_code_index + sizeof(int32_t);
    if (err_info_len_index >= end_index) {
        ErrorLog << "parse error, err_info_len_index[" << err_info_len_index << "] >= end_index[" << end_index << "]";
        return;
    }
    pb_struct->err_info_len = getInt32FromNetByte(&tmp[err_info_len_index]);
    DebugLog << "err_info_len = " << pb_struct->err_info_len;

    // 错误信息内容
    int err_info_index = err_info_len_index + sizeof(int32_t);
    char err_info[512] = {0};
    memcpy(&err_info[0], &tmp[err_info_index], pb_struct->err_info_len);
    pb_struct->err_info = std::string(err_info);

    // Protobuf 数据
    int pb_data_len = pb_struct->pk_len - pb_struct->service_name_len - pb_struct->msg_req_len -
                      pb_struct->err_info_len - 2 * sizeof(char) - 6 * sizeof(int32_t);
    int pb_data_index = err_info_index + pb_struct->err_info_len;
    DebugLog << "pb_data_len = " << pb_data_len << ", pb_index = " << pb_data_index;
    if (pb_data_index >= end_index) {
        ErrorLog << "parse error, pb_data_index[" << pb_data_index << "] >= end_index[" << end_index << "]";
        return;
    }
    std::string pb_data_str(&tmp[pb_data_index], pb_data_len);
    pb_struct->pb_data = pb_data_str;

    pb_struct->decode_succ = true;
    data = pb_struct;
}

ProtocolType TinyPbCodeC::getProtocolType() { return TinyPb_Protocol; }

}  // namespace tinyrpc