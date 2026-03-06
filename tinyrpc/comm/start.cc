#include <google/protobuf/service.h>

// #include <tinyrpc/coroutine/coroutine_hook.h>
// #include <tinyrpc/net/tcp/tcp_server.h>
// #include #include <tinyrpc/comm/config.h>
// #include <tinyrpc/comm/log.h>
// #include <tinyrpc/comm/start.h>
#include <tinyrpc/comm/config.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/comm/start.h>
#include <tinyrpc/coroutine/coroutine_hook.h>
#include <tinyrpc/net/tcp/tcp_server.h>

namespace tinyrpc {

tinyrpc::Config::ptr gRpcConfig;     // 全局配置对象
tinyrpc::Logger::ptr gRpcLogger;     // 全局日志对象
tinyrpc::TcpServer::ptr gRpcServer;  // 全局TCP服务器对象

static int g_init_config = 0;

// 配置初始化
void InitConfig(const char *file) {
    // 在配置初始化阶段禁用协程 Hook，避免在协程未就绪时出现问题
    tinyrpc::SetHook(false);

// MySQL初始化（可选）
#ifdef DECLARE_MYSQL_PULGIN
    int rt = mysql_library_init(0, NULL, NULL);
    if (rt != 0) {
        printf("Start TinyRPC server error, call mysql_library_init error\n");
        mysql_library_end();
        exit(0);
    }
#endif

    // 重新启用Hook
    tinyrpc::SetHook(true);

    // 创建配置对象并读取配置
    if (g_init_config == 0) {  // 确保配置只初始化一次
        gRpcConfig = std::make_shared<tinyrpc::Config>(file);
        // 读取XML配置文件
        gRpcConfig->readConf();
        g_init_config = 1;
    }
}

TcpServer::ptr GetServer() { return gRpcServer; }

void StartRpcServer() {
    gRpcLogger->start();  // 启动异步日志线程
    gRpcServer->start();  // 开始监听和接受连接
}

int GetIOThreadPoolSize() { return gRpcServer->getIOThreadPool()->getIOThreadPoolSize(); }

Config::ptr GetConfig() { return gRpcConfig; }

void AddTimerEvent(TimerEvent::ptr event) {}

}  // namespace tinyrpc