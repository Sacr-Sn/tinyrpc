#pragma once

#include <map>
#include <memory>
#include <string>

#include "abstract_data.h"
#include "http_define.h"

/**
 * 定义了TinyRPC中HTTP请求的数据结构，用于表示和存储解析后的HTTP请求信息
 * 是HTTP协议层的核心数据类型
 */
namespace tinyrpc {

class HttpRequest : public AbstractData {
   public:
    typedef std::shared_ptr<HttpRequest> ptr;

    /**
     * 请求行信息
     */
    HttpMethod request_method;    // HTTP请求方法（GET或POST）
    std::string request_path;     // 请求路径，如/qps
    std::string request_query;    // 查询字符串，如id=1&name=test
    std::string request_version;  // HTTP版本，如HTTP/1.1

    /**
     * 请求头和正文
     */
    HttpRequestHeader request_header;  // 请求头，类型为HttpRequestHeader
    std::string request_body;          // 请求正文内容

    /**
     * 解析后的查询参数
     */
    std::map<std::string, std::string> query_maps;  // 将查询字符串解析为键值对的map
};

}  // namespace tinyrpc