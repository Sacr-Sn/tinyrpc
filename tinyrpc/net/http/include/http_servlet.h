#pragma once

#include <memory>

#include "http_request.h"
#include "http_response.h"

/**
 * http_servlet定义并实现了TinyRPC中HTTP SERVLET的抽象接口和基础实现，提供了类似Java Servlet的HTTP请求处理机制
 * 它是HTTP协议层的核心组件，用于处理HTTP请求并生成响应
*/

namespace tinyrpc {

/**
 * std::enable_shared_from_this 是 C++ 标准库中的一个辅助模板类，它用于支持在类内部获取指向当前对象的 shared_ptr。
 * 如果一个类继承了 std::enable_shared_from_this，它就可以通过成员函数来创建指向该对象的 shared_ptr，即便这个对象本身并不是通过 shared_ptr 来创建的。
 * 这意味着该类可以通过 shared_from_this() 成员函数来获取当前对象的 shared_ptr。
*/
class HttpServlet : public std::enable_shared_from_this<HttpServlet> {

public:
    typedef std::shared_ptr<HttpServlet> ptr;

    HttpServlet();

    virtual ~HttpServlet();

    // 处理HTTP 请求的核心方法，必须被子类重写
    virtual void handle(HttpRequest* req, HttpResponse* res) = 0;

    // 返回Servlet名称，用于日志和调试
    virtual std::string getServletName() = 0;

    // 处理404错误
    void handleNotFound(HttpRequest* req, HttpResponse* res);

    // 设置响应中HTTP状态码
    void setHttpCode(HttpResponse* res, const int code);

    // 设置响应中Content_Type头
    void setHttpContentType(HttpResponse* res, const std::string& content_type);

    // 设置响应正文
    void setHttpBody(HttpResponse* res, const std::string& body);

    // 设置通用参数（如版本、Connection头）
    void setCommParam(HttpRequest* req, HttpResponse* res);
};


/**
 * 一个servlet的具体实现，专门用于处理404错误
*/
class NotFoundHttpServlet : public HttpServlet {

public:
    NotFoundHttpServlet();

    ~NotFoundHttpServlet();

    void handle(HttpRequest* req, HttpResponse* res);

    std::string getServletName();

};

}