#pragma once

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

namespace tinyrpc {

/**
 * 用于将网络字节序(大端序)的数据转换为主机字节序
*/
int32_t getInt32FromNetByte(const char* buf) {
    int32_t tmp;
    // 使用 memcpy 从缓冲区复制 4 字节数据到 int32_t 变量
    memcpy(&tmp, buf, sizeof(tmp));
    // 调用 ntohl() (network to host long) 将网络字节序转换为主机字节序
    return ntohl(tmp);
}

}