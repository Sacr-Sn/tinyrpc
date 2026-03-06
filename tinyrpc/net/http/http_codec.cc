#include <algorithm>
#include <sstream>

#include <tinyrpc/comm/log.h>
#include <tinyrpc/comm/string_util.h>
#include <tinyrpc/net/http/http_codec.h>
#include <tinyrpc/net/http/http_request.h>
#include <tinyrpc/net/http/http_response.h>

namespace tinyrpc {

HttpCodeC::HttpCodeC() {}

HttpCodeC::~HttpCodeC() {}

void HttpCodeC::encode(TcpBuffer* buf, AbstractData* data) {
    DebugLog << "test encode";
    // 多态，将AbstractData*动态转换为HttpResponse*
    HttpResponse* response = dynamic_cast<HttpResponse*>(data);
    response->encode_succ = false;

    // 拼接响应行、响应头和响应体
    // 响应行格式：HTTP/1.1 200 OK\r\n
    // 响应头：toHttpString()方法将map转换为HTTP头格式
    // 空行：\r\n
    // 响应体：直接追加
    std::string http_res =
        std::format("{} {} {}\r\n{}\r\n{}", response->response_version, response->response_code,
                    response->response_info, response->response_header.toHttpString(), response->response_body);
    DebugLog << "encode http response is: " << http_res;

    // 将拼接好的字符串写入TcpBuffer
    buf->writeToBuffer(http_res.c_str(), http_res.length());
    DebugLog << "succ encode and write to buffer, write_index = " << buf->writeIndex();
    response->encode_succ = true;  // 标记编码成功
    DebugLog << "encode end";
}

void HttpCodeC::decode(TcpBuffer* buf, AbstractData* data) {
    DebugLog << "test http decode start";
    std::string strs = "";
    if (!buf || !data) {
        ErrorLog << "decode error! buf ot data nullptr";
        return;
    }

    // 类型转换
    HttpRequest* request = dynamic_cast<HttpRequest*>(data);
    if (!request) {
        ErrorLog << "not httprequest type";
        return;
    }

    // 解析请求行
    strs = buf->getBufferString();
    bool is_parse_request_line = false;
    bool is_parse_request_header = false;
    bool is_parse_request_content = false;
    int read_size = 0;
    std::string tmp(strs);
    DebugLog << "pending to parse str: " << tmp << ", total size = " << tmp.size();
    int len = tmp.length();
    while (1) {
        // 解析请求行
        if (!is_parse_request_line) {
            size_t i = tmp.find(g_CRLF);
            if (i == tmp.npos) {
                DebugLog << "not found CRLF in buffer";
                return;
            }
            if (i == tmp.length() - 2) {
                DebugLog << "need to read more data";
                break;
            }
            is_parse_request_line = parseHttpRequestLine(request, tmp.substr(0, i));
            if (!is_parse_request_line) {
                return;
            }
            tmp = tmp.substr(i + 2, len - 2 - i);
            len = tmp.length();
            read_size = read_size + i + 2;
        }
        // 解析请求头
        if (!is_parse_request_header) {
            size_t j = tmp.find(g_CRLF_DOUBLE);
            if (j == tmp.npos) {
                DebugLog << "not found CRLF in buffer";
                return;
            }
            is_parse_request_header = parseHttpRequestHeader(request, tmp.substr(0, j));
            if (!is_parse_request_header) {
                return;
            }
            tmp = tmp.substr(j + 4, len - 4 - j);
            len = tmp.length();
            read_size = read_size + j + 4;
        }
        // 解析请求正文
        if (!is_parse_request_content) {
            int content_len = std::atoi(request->request_header.maps["Content-Length"].c_str());
            if ((int)strs.length() - read_size < content_len) {
                DebugLog << "need to read more data";
                return;
            }
            if (request->request_method == POST && content_len != 0) {
                is_parse_request_content = parseHttpRequestContent(request, tmp.substr(0, content_len));
                if (!is_parse_request_content) {
                    return;
                }
                read_size = read_size + content_len;
            } else {  // 对于GET请求或无正文的请求，直接标记为解析成功
                is_parse_request_content = true;
            }
        }
        if (is_parse_request_line && is_parse_request_header && is_parse_request_content) {
            DebugLog << "parse http request success, read size is " << read_size << " bytes";
            buf->recycleRead(read_size);
            break;
        }
    }

    request->decode_succ = true;
    data = request;

    DebugLog << "test http decode end";
}

/**
 * 解析请求行
 */
bool HttpCodeC::parseHttpRequestLine(HttpRequest* request, const std::string& tmp) {
    size_t s1 = tmp.find_first_of(" ");  // HTTP方法
    size_t s2 = tmp.find_last_of(" ");   // HTTP版本
    if (s1 == tmp.npos || s2 == tmp.npos || s1 == s2) {
        ErrorLog << "error read Http Request Line, space is not 2";
        return false;
    }

    std::string method = tmp.substr(0, s1);
    std::transform(method.begin(), method.end(), method.begin(), toupper);  // 转换为大写
    if (method == "GET") {
        request->request_method = HttpMethod::GET;
    } else if (method == "POST") {
        request->request_method = HttpMethod::POST;
    } else {
        ErrorLog << "parse http request line error, not support http method:" << method;
        return false;
    }

    std::string version = tmp.substr(s2 + 1, tmp.length() - s2 - 1);
    std::transform(version.begin(), version.end(), version.begin(), toupper);  // 转换为大写
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        ErrorLog << "parse http request line error, not support http version:" << version;
        return false;
    }
    request->request_version = version;

    // 解析url
    std::string url = tmp.substr(s1 + 1, s2 - s1 - 1);
    size_t j = url.find("://");
    if (j != url.npos && j + 3 >= url.length()) {
        ErrorLog << "parse http request line error, bad url:" << url;
        return false;
    }
    int l = 0;
    if (j == url.npos) {
        DebugLog << "url only have path, url is" << url;
    } else {
        url = url.substr(j + 3, s2 - s1 - j - 4);  // 移除http://前缀（如果有）
        DebugLog << "delete http prefix, utl = " << url;
        j = url.find_first_of("/");
        l = url.length();
        if (j == url.npos || j == url.length() - 1) {
            DebugLog << "http request root path, and query is empty";
            return true;
        }
        url = url.substr(j + 1, l - j - 1);
    }

    // 分离路径和查询字符串（以?分隔）
    l = url.length();
    j = url.find_first_of("?");
    if (j == url.npos) {
        request->request_path = url;
        DebugLog << "http request path:" << request->request_path << ", and query is empty";
        return true;
    }
    request->request_path = url.substr(0, j);
    request->request_query = url.substr(j + 1, l - j - 1);
    DebugLog << "http request path:" << request->request_path << ", and query:" << request->request_query;
    // 将查询字符串解析为键值对
    StringUtil::SplitStrToMap(request->request_query, "&", "=", request->query_maps);
    return true;
}

/**
 * 解析请求头
 */
bool HttpCodeC::parseHttpRequestHeader(HttpRequest* request, const std::string& str) {
    if (str.empty() || str.length() < 4 || str == "\r\n\r\n") {
        return true;
    }
    std::string tmp = str;
    // 将请求头字符串解析为map
    StringUtil::SplitStrToMap(tmp, "\r\n", ":", request->request_header.maps);
    return true;
}

/**
 * 解析请求正文
 */
bool HttpCodeC::parseHttpRequestContent(HttpRequest* request, const std::string& str) {
    if (str.empty()) {
        return true;
    }
    // 直接将正文内容存request_body中
    request->request_body = str;
    return true;
}

/**
 * 获取协议类型
 */
ProtocolType HttpCodeC::getProtocolType() { return Http_Protocol; }
}  // namespace tinyrpc