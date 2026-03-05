#pragma once

#include <memory>
#include <string>

#include "abstract_data.h"
#include "http_define.h"

/**
 * 定义了TinyRPC中HTTP相应的数据结构，用于表示和存储HTTP响应信息
 */

namespace tinyrpc {

class HttpResponse : public AbstractData {
   public:
    typedef std::shared_ptr<HttpResponse> ptr;

    /**
     * 响应行信息
     */
    std::string response_version;  // HTTP版本
    int response_code;             // HTTP状态码，如200、404
    std::string response_info;     // 状态描述，如"OK"

    /**
     * 响应头和正文
     */
    HttpResponseHeader response_header;  // 响应头
    std::string response_body;           // 响应正文内容
};

}  // namespace tinyrpc