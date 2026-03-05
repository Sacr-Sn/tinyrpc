#pragma once

#include <stdint.h>

#include "abstract_codec.h"
#include "abstract_data.h"
#include "tinypb_data.h"

/**
 * 定义了TinyRPC中TinyPB协议的编解码器，负责将TinyPB协议数据在字节流和TinyPB对象之间进行转换
*/

namespace tinyrpc {

class TinyPbCodeC : public AbstractCodeC {

public:
    TinyPbCodeC();

    ~TinyPbCodeC();

    // 将TinyPbStruct编码为字节流
    void encode(TcpBuffer* buf, AbstractData* data);

    // 将字节流解码为TinyPbStruct
    void decode(TcpBuffer* buf, AbstractData* data);

    // 获取协议类型
    virtual ProtocolType getProtocolType();

    // 实际编码逻辑
    const char* encodePbData(TinyPbStruct* data, int& len);

};

}