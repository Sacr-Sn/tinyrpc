#pragma once

#include <map>
#include <string>

/**
 * 是TinyRPC中HTTP协议的基础定义文件，包含了HTTP协议相关的常量、枚举、工具函数和基础类
 */

namespace tinyrpc {

extern std::string g_CRLF;         // HTTP协议的换行符 \r\n
extern std::string g_CRLF_DOUBLE;  // HTTP协议的双换行符 \r\n\r\n，用于分隔请求头和请求体

extern std::string content_type_text;  // 默认的Content-Type
// extern const char* default_html_template;  // 默认的HTML模板
constexpr const char* default_html_template =
    "<html><head><title>{}</title></head><body><h1>{}</h1></body></html>";  // 默认的HTML模板

// HTTP请求方法的枚举
enum HttpMethod { GET = 1, POST = 2 };

// 常用的 HTTP 状态码:
enum HttpCode {
    HTTP_OK = 200,
    HTTP_BADREQUEST = 400,
    HTTP_FORBIDDEN = 403,
    HTTP_NOTFOUND = 404,
    HTTP_INTERNALSERVERERROR = 500,
};

// 工具函数，用于将HTTP状态码转换为对应的描述字符串
const char* httpCodeToString(const int code);

// HTTP请求头和响应头的公共基类
class HttpHeaderComm {
   public:
    // 存储头部的键值对
    std::map<std::string, std::string> maps;

    HttpHeaderComm() = default;

    virtual ~HttpHeaderComm() = default;

    // 获取头部总长度
    int getHeaderTotalLength();

    // 根据键获取值
    std::string getValue(const std::string& key);

    // 设置键值对
    void setKeyValue(const std::string& key, const std::string& value);

    // 将头部转换为HTTP格式的字符串
    std::string toHttpString();
};

/**
 * 专门用于HTTP请求头。
 * 注释中列出了常见的请求头字段（如Accept、User-Agent、Host等），
 * 但实际上都存储在基类的maps中，提供了更大的灵活性
 */
class HttpRequestHeader : public HttpHeaderComm {
   public:
    // std::string m_accept;
    // std::string m_refer;
    // std::string m_accept_language;
    // std::string m_accept_charset;
    // std::string m_user_agent;
    // std::string m_host;
    // std::string m_content_type;
    // std::string m_content_length;
    // std::string m_connection;
    // std::string m_cookie;

    // void storeTpMap();
};

/**
 * 专门用于HTTP响应头
 * 常见的响应头字段（如Server、Content-Type、Content-Length等）都存储在maps中
 */
class HttpResponseHeader : public HttpHeaderComm {
   public:
    // std::string m_server;
    // std::string m_content_type;
    // std::string m_content_length;
    // std::string m_set_cookie;
    // std::string m_connection;

    // void storeToMap();
};

}  // namespace tinyrpc