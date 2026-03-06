#include <sstream>

#include <tinyrpc/comm/log.h>
#include <tinyrpc/net/comm/net_address.h>

namespace tinyrpc {

bool IPAddress::CheckValidIPAddr(const std::string& addr) {
    size_t i = addr.find_first_of(":");
    if (i == addr.npos) {
        return false;
    }

    int port = std::atoi(addr.substr(i + 1, addr.size() - i - 1).c_str());
    if (port < 0 || port > 65535) {
        return false;
    }

    if (inet_addr(addr.substr(0, i).c_str()) == INADDR_NONE) {
        return false;
    }

    return true;
}

IPAddress::IPAddress(const std::string& ip, uint16_t port) : ip_(ip), port_(port) {
    memset(&addr_, 0, sizeof(addr_));  // sockaddr_in结构体填充0
    addr_.sin_family = AF_INET;        // 设置地址族
    addr_.sin_addr.s_addr = inet_addr(ip_.c_str());
    addr_.sin_port = htons(port_);

    DebugLog << "create upv4 address succ [" << toString() << "]";
}

IPAddress::IPAddress(sockaddr_in addr) : addr_(addr) {
    DebugLog << "ip[" << ip_ << "], port[" << addr.sin_port;
    ip_ = std::string(inet_ntoa(addr_.sin_addr));
    port_ = ntohs(addr_.sin_port);
}

IPAddress::IPAddress(const std::string& addr) {
    size_t i = addr.find_first_of(":");
    if (i == addr.npos) {
        ErrorLog << "invalid addr[" << addr << "]";
        return;
    }

    ip_ = addr.substr(0, i);
    port_ = std::atoi(addr.substr(i + 1, addr.size() - i - 1).c_str());

    memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = inet_addr(ip_.c_str());
    addr_.sin_port = htons(port_);
    DebugLog << "create ipv4 address succ [" << toString() << "]";
}

IPAddress::IPAddress(uint16_t port) : port_(port) {
    memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = INADDR_ANY;  // 表示绑定到所有可用网络接口
    addr_.sin_port = htons(port_);

    DebugLog << "create ipv4 address succ [" << toString() << "]";
}

int IPAddress::getFamily() const { return addr_.sin_family; }

sockaddr* IPAddress::getSockAddr() { return reinterpret_cast<sockaddr*>(&addr_); }

std::string IPAddress::toString() const { return std::format("{}:{}", ip_, port_); }

socklen_t IPAddress::getSockLen() const { return sizeof(addr_); }

UnixDomainAddress::UnixDomainAddress(std::string& path) : m_path(path) {
    memset(&addr_, 0, sizeof(addr_));
    unlink(m_path.c_str());      // 删除已存在的socket文件
    addr_.sun_family = AF_UNIX;  // 设置地址族
    strcpy(addr_.sun_path, m_path.c_str());
}

UnixDomainAddress::UnixDomainAddress(sockaddr_un addr) : addr_(addr) { m_path = addr_.sun_path; }

int UnixDomainAddress::getFamily() const { return addr_.sun_family; }

sockaddr* UnixDomainAddress::getSockAddr() { return reinterpret_cast<sockaddr*>(&addr_); }

socklen_t UnixDomainAddress::getSockLen() const { return sizeof(addr_); }

std::string UnixDomainAddress::toString() const { return m_path; }

}  // namespace tinyrpc