#pragma once

#include <memory>
#include <map>

#include "abstract_dispatcher.h"
#include "http_servlet.h"

/**
 * 实现了TinyRPC中HTTP请求的分发器，负责根据URL路径将HTTP请求路由到对应的HttpServlet进行处理。
*/

namespace tinyrpc {

class HttpDispatcher : public AbstractDispatcher {

public:
    // 存储URL路径到Servlet的映射关系
    std::map<std::string, HttpServlet::ptr> m_servlets;

    HttpDispatcher() = default;

    ~HttpDispatcher() = default;

    /**
     * 实现请求分发逻辑
     * AbstractData* data - 已解码的协议数据
     * TcpConnection* conn - Tcp连接对象，用于写入响应
    */
    void dispatch(AbstractData* data, TcpConnection* conn);

    /**
     * 注册Servlet到指定路径s
    */
    void registerServlet(const std::string& path, HttpServlet::ptr servlet);

};

}