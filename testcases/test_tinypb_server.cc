#include <google/protobuf/service.h>
#include <atomic>
#include <sstream>

#include "log.h"
#include "net_address.h"
#include "start.h"
#include "tcp_server.h"
#include "test_tinypb_server.pb.h"
#include "tinypb_rpc_dispatcher.h"

/**
 * protobuf 文件提供的只是接口说明，而实际的业务逻辑需要自己实现。
 * 只需要继承QueryService 并重写方法即可
 */

static std::atomic<int> i{0};  // 无锁、线程安全、协程友好

class QueryServiceImpl : public QueryService {
   public:
    QueryServiceImpl() {}
    ~QueryServiceImpl() {}

    void query_name(google::protobuf::RpcController *controller, const ::queryNameReq *request,
                    ::queryNameRes *response, ::google::protobuf::Closure *done) {
        // AppInfoLog("QueryServiceImpl.query_name, req={%s}", request->ShortDebugString().c_str());
        AppInfoLog(std::format("QueryServiceImpl.query_name, req={}", request->ShortDebugString()));
        response->set_id(request->id());
        response->set_name("ikerli");

        // AppInfoLog("QueryServiceImpl.query_name, res={%s}", response->ShortDebugString().c_str());
        AppInfoLog(std::format("QueryServiceImpl.query_name, res={}", response->ShortDebugString()));
        if (done) {
            done->Run();
        }
    }

    void query_age(google::protobuf::RpcController *controller, const ::queryAgeReq *request, ::queryAgeRes *response,
                   ::google::protobuf::Closure *done) {
        // AppInfoLog("QueryServiceImpl.query_age, req={%s}", request->ShortDebugString().c_str());
        AppInfoLog(std::format("QueryServiceImpl.query_age, req={}", request->ShortDebugString()));
        // AppInfoLog << "QueryServiceImpl.query_age, sleep 6 s begin";
        // sleep(6);
        // AppInfoLog << "QueryServiceImpl.query_age, sleep 6 s end";

        response->set_ret_code(0);
        response->set_res_info("OK");
        response->set_req_no(request->req_no());
        response->set_id(request->id());
        response->set_age(++i);

        if (done) {
            done->Run();
        }

        // AppInfoLog("QueryServiceImpl.query_age, res={%s}", response->ShortDebugString().c_str());
        AppInfoLog(std::format("QueryServiceImpl.query_age, res={}", response->ShortDebugString()));
    }
};

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Start TinyRPC server error, input argc is not 2!");
        printf("Start TinyRPC server like this: \n");
        printf("./server a.xml\n");
        return 0;
    }

    tinyrpc::InitConfig(argv[1]);

    REGISTER_SERVICE(QueryServiceImpl);

    tinyrpc::StartRpcServer();

    return 0;
}
