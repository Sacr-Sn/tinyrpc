#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <memory>
#include <string>

/**
 * 它封装了网络编程中最基础的地址概念,通过抽象基类实现了对 IPv4 和 Unix Domain Socket 的统一接口。
 */

namespace tinyrpc {

// 一个抽象基类，定义了所有网络地址类型必须实现的接口
class NetAddress {
   public:
    typedef std::shared_ptr<NetAddress> ptr;

    // 获取底层的sockaddr*指针，用于系统调用
    virtual sockaddr* getSockAddr() = 0;

    // 获取地址族（AF_INET或AF_UNIX）
    virtual int getFamily() const = 0;

    // 获取地址结构体长度
    virtual socklen_t getSockLen() const = 0;

    // 将地址转换为字符串表示
    virtual std::string toString() const = 0;
};

/**
 * 专门用于封装IPv4地址
 */
class IPAddress : public NetAddress {
   private:
    std::string ip_;    // IP地址字符串
    uint16_t port_;     // 端口号
    sockaddr_in addr_;  // 底层的sockaddr_in结构体

   public:
    // 验证地址字符串是否有效
    static bool CheckValidIPAddr(const std::string& addr);

    // 通过IP字符串和端口号构造
    IPAddress(const std::string& ip, uint16_t port);

    // 通过"IP:port"格式的字符串构造
    IPAddress(const std::string& addr);

    // 只指定端口号，IP使用INADDR_ANY(0.0.0.0)
    IPAddress(uint16_t port);

    // 从sockaddr_in结构体创建
    IPAddress(sockaddr_in addr);

    sockaddr* getSockAddr();

    int getFamily() const;

    std::string getIP() { return ip_; }

    int getPort() const { return port_; }

    socklen_t getSockLen() const;

    std::string toString() const;
};

/**
 * 封装Unix域套接字地址
 */
class UnixDomainAddress : public NetAddress {
   private:
    std::string m_path;  // Unix socket文件路径
    sockaddr_un addr_;   // 底层的sockaddr_un结构体

   public:
    // 通过文件路径构造
    UnixDomainAddress(std::string& path);

    // 从sockaddr_un结构体构造
    UnixDomainAddress(sockaddr_un addr);

    sockaddr* getSockAddr();

    int getFamily() const;

    socklen_t getSockLen() const;

    std::string getPath() const { return m_path; }

    std::string toString() const;
};

}  // namespace tinyrpc