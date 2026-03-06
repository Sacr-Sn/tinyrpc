#pragma once

#include <google/protobuf/service.h>
#include <stdio.h>
#include <functional>
#include <memory>

// #include <tinyrpc/comm/log.h>
// #include <tinyrpc/net/tcp/tcp_server.h>
// #include <tinyrpc/net/comm/timer.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/net/comm/timer.h>
#include <tinyrpc/net/tcp/tcp_server.h>

/**
 * 是TinyRPC的启动入口模块，提供了框架初始化和服务启动的核心接口。
 * 封装了配置读取、服务注册、服务启动等关键步骤，
 * 使得用户只需几行代码就能启动一个完整的RPC服务.
 */

namespace tinyrpc {

/**
 * 服务注册宏定义
 */

/**
 * 用于注册HTTP Servlet，工作流程
 *  1.调用GetServer()获取全局TcpServer对象；
 *  2.调用registerHttpServlet()，注册Servlet到指定路径；
 *  3.如果注册失败，输出错误信息并退出程序；
 */
#define REGISTER_HTTP_SERVLET(path, servlet)                                                                        \
    do {                                                                                                            \
        if (!tinyrpc::GetServer()->registerHttpServlet(path, std::make_shared<servlet>())) {                        \
            printf(                                                                                                 \
                "Start TinyRPC server error, because register http servelt error, please look up rpc log get more " \
                "details!\n");                                                                                      \
            tinyrpc::Exit(0);                                                                                       \
        }                                                                                                           \
    } while (0)

/**
 * 用于注册Protobuf Service，工作流程：
 *  1.获取全局TcpServer对象
 *  2.调用registerService()，注册Protobuf Service
 *  3.注册失败则退出程序
 */
#define REGISTER_SERVICE(service)                                                                                  \
    do {                                                                                                           \
        if (!tinyrpc::GetServer()->registerService(std::make_shared<service>())) {                                 \
            printf(                                                                                                \
                "Start TinyRPC server error, because register protobuf service error, please look up rpc log get " \
                "more details!\n");                                                                                \
            tinyrpc::Exit(0);                                                                                      \
        }                                                                                                          \
    } while (0)

// 初始化配置
void InitConfig(const char* file);

// 启动RPC服务器
void StartRpcServer();

// 获取全局 TcpServer 对象
TcpServer::ptr GetServer();

// 获取IO线程池大小
int GetIOThreadPoolSize();

// 获取全局配置对象
Config::ptr GetConfig();

// 添加定时器事件
void AddTimerEvent(TimerEvent::ptr event);

}  // namespace tinyrpc