#include <string>

#include <tinyrpc/net/comm/abstract_codec.h>
#include <tinyrpc/net/comm/abstract_data.h>
#include <tinyrpc/net/http/http_request.h>

/**
 * 实现了TinyRPC中HTTP协议的编解码器，
 * 负责将HTTP请求解析为HttpRequest对象，以及将HttpResponse对象编码为字节流
 */

namespace tinyrpc {

class HttpCodeC : public AbstractCodeC {
   public:
    HttpCodeC();

    ~HttpCodeC();

    /* 继承自AbstractCodeC中的三个核心接口 */
    // 将HttpResponse编码为字节流
    void encode(TcpBuffer* buf, AbstractData* data);

    // 将字节流解码为HttpRequest
    void decode(TcpBuffer* buf, AbstractData* data);

    // 返回协议类型
    ProtocolType getProtocolType();

   private:
    /* 三个私有方法，用于解析HTTP请求的不同部分 */

    // 解析请求行（方法、路径、版本）
    bool parseHttpRequestLine(HttpRequest* request, const std::string& tmp);

    // 解析请求头
    bool parseHttpRequestHeader(HttpRequest* request, const std::string& tmp);

    // 解析请求正文
    bool parseHttpRequestContent(HttpRequest* request, const std::string& tmp);
};

}  // namespace tinyrpc