#include <google/protobuf/service.h>

#include <tinyrpc/comm/log.h>
#include <tinyrpc/comm/msg_req.h>
#include <tinyrpc/net/http/http_dispatcher.h>
#include <tinyrpc/net/http/http_request.h>
#include <tinyrpc/net/http/http_servlet.h>

namespace tinyrpc {

void HttpDispatcher::dispatch(AbstractData* data, TcpConnection* conn) {
    /* 类型转换和初始化 */
    HttpRequest* request = dynamic_cast<HttpRequest*>(data);
    HttpResponse response;
    // 生成消息编号并设置到当前协程的运行时上下文
    Coroutine::GetCurrentCoroutine()->getRunTime()->msg_no = MsgReqUtil::genMsgNumber();
    setCurrentRunTime(Coroutine::GetCurrentCoroutine()->getRunTime());

    InfoLog << "begin to dispatch client http request, msg_no = "
            << Coroutine::GetCurrentCoroutine()->getRunTime()->msg_no;

    /* 提取URL路径 */
    std::string url_path = request->request_path;

    /* 查找对应的Servlet */
    if (!url_path.empty()) {
        // 在m_servlets map中查找路径对应的Servlet
        auto it = m_servlets.find(url_path);
        if (it == m_servlets.end()) {  // 如果找不到，使用NotFoundHttpServlet处理404错误
            ErrorLog << "404, url path{ " << url_path
                     << " }, msg_no = " << Coroutine::GetCurrentCoroutine()->getRunTime()->msg_no;
            NotFoundHttpServlet servlet;
            Coroutine::GetCurrentCoroutine()->getRunTime()->interface_name = servlet.getServletName();
            servlet.setCommParam(request, &response);
            servlet.handle(request, &response);
        } else {
            // 将Servlet名称设置到当前协程的运行时上下文，便于日志追踪
            Coroutine::GetCurrentCoroutine()->getRunTime()->interface_name = it->second->getServletName();
            it->second->setCommParam(request, &response);
            it->second->handle(request, &response);
        }
    }

    // 使用codec将HttpReponse编码输出到缓冲区
    conn->getCodec()->encode(conn->getOutBuffer(), &response);

    InfoLog << "end dispatch client http request, msg_no = " << Coroutine::GetCurrentCoroutine()->getRunTime()->msg_no;
}

void HttpDispatcher::registerServlet(const std::string& path, HttpServlet::ptr servlet) {
    auto it = m_servlets.find(path);
    if (it == m_servlets.end()) {
        DebugLog << "register servlet success to path { " << path << "}";
        m_servlets[path] = servlet;
    } else {
        ErrorLog << "failed to register, because path {" << path << " } has already register servlet";
    }
}

}  // namespace tinyrpc