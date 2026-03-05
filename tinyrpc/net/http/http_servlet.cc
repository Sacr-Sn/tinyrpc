#include "http_servlet.h"
#include "http_define.h"
#include "log.h"

namespace tinyrpc {

// extern const char* default_html_template;  // 默认的HTML模板
// constexpr const char* default_html_template =
//     "<html><head><title>{}</title></head><body><h1>{}</h1></body></html>";  // 默认的HTML模板
extern std::string content_type_text;  // 默认的Content_Type

HttpServlet::HttpServlet() {}

HttpServlet::~HttpServlet() {}

void HttpServlet::handle(HttpRequest* req, HttpResponse* res) {}

void HttpServlet::handleNotFound(HttpRequest* req, HttpResponse* res) {
    DebugLog << "return 404 html";
    setHttpCode(res, HTTP_NOTFOUND);  // 设置HTTP状态码为404

    // 格式化HTML响应，包含状态码和描述
    std::string res_body =
        std::format(tinyrpc::default_html_template, std::to_string(HTTP_NOTFOUND), httpCodeToString(HTTP_NOTFOUND));
    res->response_body = res_body;                                                              // 设置响应正文
    res->response_header.maps["Content-Type"] = content_type_text;                              // 设置Content-Type
    res->response_header.maps["Content-Length"] = std::to_string(res->response_body.length());  // 设置Content-Length
}

void HttpServlet::setHttpCode(HttpResponse* res, const int code) {
    res->response_code = code;
    res->response_info = std::string(httpCodeToString(code));
}

void HttpServlet::setHttpContentType(HttpResponse* res, const std::string& content_type) {
    res->response_header.maps["Content-Type"] = content_type;
}

void HttpServlet::setHttpBody(HttpResponse* res, const std::string& body) {
    res->response_body = body;
    res->response_header.maps["Content-Length"] = std::to_string(res->response_body.length());
}

void HttpServlet::setCommParam(HttpRequest* req, HttpResponse* res) {
    DebugLog << "set response version = " << req->request_version;
    res->response_version = req->request_version;
    res->response_header.maps["Connection"] = req->request_header.maps["Connection"];
}

NotFoundHttpServlet::NotFoundHttpServlet() {}

NotFoundHttpServlet::~NotFoundHttpServlet() {}

void NotFoundHttpServlet::handle(HttpRequest* req, HttpResponse* res) { handleNotFound(req, res); }

std::string NotFoundHttpServlet::getServletName() { return "NotFoundHttpServlet"; }

}  // namespace tinyrpc