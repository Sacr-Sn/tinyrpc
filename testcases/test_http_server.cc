#include <google/protobuf/service.h>
#include <atomic>
#include <future>

#include "http_define.h"
#include "http_request.h"
#include "http_response.h"
#include "http_servlet.h"
#include "net_address.h"
#include "start.h"
#include "test_tinypb_server.pb.h"
#include "tinypb_rpc_async_channel.h"
#include "tinypb_rpc_channel.h"
#include "tinypb_rpc_closure.h"
#include "tinypb_rpc_controller.h"

/**
 * TinyRPC 提供类似 JAVA 的 Servlet 接口来实现 HTTP 服务。你只需要简单的继承
 * HttpServlet 类并实现 handle 方法即可，如一个 HTTP 的 echo 如下
 */

const char *html =
    "<html><body><h1>Welcome to TinyRPC, just enjoy "
    "it!</h1><p>%s</p></body></html>\n";
constexpr const char *html_format = "<html><body><h1>Welcome to TinyRPC, just enjoy it!</h1><p>{}</p></body></html>\n";

// 用来访问tinypb服务
tinyrpc::IPAddress::ptr addr = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 39999);

class BlockCallHttpServlet : public tinyrpc::HttpServlet {
   public:
    BlockCallHttpServlet() = default;
    ~BlockCallHttpServlet() = default;

    void handle(tinyrpc::HttpRequest *req, tinyrpc::HttpResponse *res) {
        AppDebugLog("BlockCallHttpServlet get request");
        AppDebugLog(
            "BlockCallHttpServlet success recive http request, now to "
            "get http response");
        setHttpCode(res, tinyrpc::HTTP_OK);
        setHttpContentType(res, "text/html;charset=utf-8");

        queryAgeReq rpc_req;
        queryAgeRes rpc_res;
        // AppDebugLog("now to call QueryServer TinyRPC server to query who's id is %s", req->query_maps["id"].c_str());
        AppDebugLog(
            std::format("now to call QueryServer TinyRPC server to query who's id is {}", req->query_maps["id"]));
        rpc_req.set_id(std::atoi(req->query_maps["id"].c_str()));

        tinyrpc::TinyPbRpcChannel channel(addr);
        QueryService_Stub stub(&channel);

        tinyrpc::TinyPbRpcController rpc_controller;
        rpc_controller.SetTimeout(5000);

        AppDebugLog("BlockCallHttpServlet end to call RPC");
        stub.query_age(&rpc_controller, &rpc_req, &rpc_res, NULL);
        AppDebugLog("BlockCallHttpServlet end to call RPC");

        if (rpc_controller.ErrorCode() != 0) {
            std::string body = std::format(html_format, "failed to call QueryServer rpc server");
            setHttpBody(res, body);
            return;
        }

        if (rpc_res.ret_code() != 0) {
            std::string msg = std::format("QueryServer rpc server return bad result, ret = {}, and res_info = {}",
                                          rpc_res.ret_code(), rpc_res.res_info());
            std::string body = std::format(html_format, msg);
            setHttpBody(res, body);
            return;
        }

        std::string msg = std::format("Success!! Your age is {}, and Your id is {}", rpc_res.age(), rpc_res.id());
        std::string body = std::format(html_format, msg);
        setHttpBody(res, body);
    }

    std::string getServletName() { return "BlockCallHttpServlet"; }
};

class NonBlockCallHttpServlet : public tinyrpc::HttpServlet {
   public:
    NonBlockCallHttpServlet() = default;
    ~NonBlockCallHttpServlet() = default;

    void handle(tinyrpc::HttpRequest *req, tinyrpc::HttpResponse *res) {
        AppInfoLog("NonBlockCallHttpServlet get request");
        AppDebugLog(
            "NonBlockCallHttpServlet success receive http request, now "
            "to get http response");
        setHttpCode(res, tinyrpc::HTTP_OK);
        setHttpContentType(res, "text/html;charset=utf-8");

        std::shared_ptr<queryAgeReq> rpc_req = std::make_shared<queryAgeReq>();
        std::shared_ptr<queryAgeRes> rpc_res = std::make_shared<queryAgeRes>();
        // AppDebugLog("now to call QueryServer TinyRPC server to query who's id is %s", req->query_maps["id"].c_str());
        AppDebugLog(
            std::format("now to call QueryServer TinyRPC server to query who's id is {}", req->query_maps["id"]));
        rpc_req->set_id(std::atoi(req->query_maps["id"].c_str()));

        std::shared_ptr<tinyrpc::TinyPbRpcController> rpc_controller = std::make_shared<tinyrpc::TinyPbRpcController>();
        rpc_controller->SetTimeout(10000);

        AppDebugLog("NonBlockCallHttpServlet begin to call RPC async");

        tinyrpc::TinyPbRpcAsyncChannel::ptr async_channel = std::make_shared<tinyrpc::TinyPbRpcAsyncChannel>(addr);

        auto cb = [rpc_res]() {
            printf("call succ, res = %s\n", rpc_res->ShortDebugString().c_str());
            // AppDebugLog("NonBlockCallHttpServlet async call end, res=%s", rpc_res->ShortDebugString().c_str());
            AppDebugLog(std::format("NonBlockCallHttpServlet async call end, res={}", rpc_res->ShortDebugString()));
        };

        std::shared_ptr<tinyrpc::TinyPbRpcClosure> closure = std::make_shared<tinyrpc::TinyPbRpcClosure>(cb);
        async_channel->saveCallee(rpc_controller, rpc_req, rpc_res, closure);

        QueryService_Stub stub(async_channel.get());

        stub.query_age(rpc_controller.get(), rpc_req.get(), rpc_res.get(), NULL);
        AppDebugLog(
            "NonBlockCallHttpServlet async end, now you can to some "
            "another thing");

        async_channel->wait();
        AppDebugLog("wait() back, now to check is rpc call succ");

        if (rpc_controller->ErrorCode() != 0) {
            AppDebugLog("failed to call QueryServer rpc server");
            std::string body = std::format(html_format, "failed to call QueryServer rpc server");
            setHttpBody(res, body);
            return;
        }

        if (rpc_res->ret_code() != 0) {
            std::string msg = std::format("QueryServer rpc server return bad result, ret = {}, and res_info = {}",
                                          rpc_res->ret_code(), rpc_res->res_info());
            std::string body = std::format(html_format, msg);
            setHttpBody(res, body);
            return;
        }

        std::string msg = std::format("Success!! Your age is {}, and your id is {}", rpc_res->age(), rpc_res->id());
        std::string body = std::format(html_format, msg);
        setHttpBody(res, body);
    }

    std::string getServletName() { return "NonBlockCallHttpServlet"; }
};

class QPSHttpServlet : public tinyrpc::HttpServlet {
   public:
    QPSHttpServlet() = default;
    ~QPSHttpServlet() = default;

    void handle(tinyrpc::HttpRequest *req, tinyrpc::HttpResponse *res) {
        AppDebugLog("QPSHttpServlet get request");
        setHttpCode(res, tinyrpc::HTTP_OK);
        setHttpContentType(res, "text/html;charset=utf-8");

        std::string msg = std::format("QPSHttpServlet Echo Success!! Your id is {}", req->query_maps["id"]);
        std::string body = std::format(html_format, msg);
        setHttpBody(res, body);
        AppDebugLog(msg);
    }

    std::string getServletName() { return "QPSHttpServlet"; }
};

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Start TinyRPC server error, input argc is not 2!");
        printf("Start TinyRPC server like this: \n");
        printf("./server a.xml\n");
        return 0;
    }

    tinyrpc::InitConfig(argv[1]);

    REGISTER_HTTP_SERVLET("/qps", QPSHttpServlet);

    REGISTER_HTTP_SERVLET("/block", BlockCallHttpServlet);
    REGISTER_HTTP_SERVLET("/nonblock", NonBlockCallHttpServlet);

    tinyrpc::StartRpcServer();
    return 0;
}
