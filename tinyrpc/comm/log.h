#pragma once

#include <unistd.h>
#include <sstream>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <semaphore.h>
#include <memory>
#include <vector>
#include <queue>

#include "../net/mutex.h"
#include "config.h"

/**
 * 定义了 TinyRPC 完整的异步日志系统接口。
 * 通过宏定义提供简洁的使用方式,通过 LogEvent、LogTmp、AsyncLogger、Logger 四个类实现异步、分级、分类的日志功能。
 * 双缓冲和异步写入设计确保了日志不会阻塞主业务逻辑。
 * 
 * 执行过程：
 *  流式写日志，如 DEBUG << "a log"  -> 被宏检测到并创建被RAII包装的LogEvent，RAII析构时调用LogEvent的log() -> 
 *  LogEvent的log()中用全局的logger调用pushToRpcLog，日志被暂存到logger的buffer -> 
 *  logger创建定时器，定期执行loopFunc()，调用push()把buffer传给asynclogger的queue -> 
 *  asynclogger单独开一个线程，每当queue<vector<string>>不为空时逐vector将日志持久化
*/

namespace tinyrpc {

// 用于访问日志级别配置
extern tinyrpc::Config::ptr gRpcConfig;

// 用于printf风格的字符串格式化，主要供应用日志宏使用
template <typename... Args>
std::string formatString(const char* str, Args&&... args) {
    int size = snprintf(nullptr, 0, str, args...);

    std::string result;
    if (size > 0) {
        result.resize(size);
        snprintf(&result[0], size + 1, str, args...);
    }

    return result;
}

// 定义四个级别的RPC日志宏
#define DebugLog \
	if (tinyrpc::OpenLog() && tinyrpc::LogLevel::DEBUG >= tinyrpc::gRpcConfig->m_log_level) \
		tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG))).getStringStream()

#define InfoLog \
	if (tinyrpc::OpenLog() && tinyrpc::LogLevel::INFO >= tinyrpc::gRpcConfig->m_log_level) \
		tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG))).getStringStream()

#define WarnLog \
	if (tinyrpc::OpenLog() && tinyrpc::LogLevel::WARN >= tinyrpc::gRpcConfig->m_log_level) \
		tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG))).getStringStream()

#define ErrorLog \
	if (tinyrpc::OpenLog() && tinyrpc::LogLevel::ERROR >= tinyrpc::gRpcConfig->m_log_level) \
		tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG))).getStringStream()


#define AppDebugLog(str, ...) \
  if (tinyrpc::OpenLog() && tinyrpc::LogLevel::DEBUG >= tinyrpc::gRpcConfig->m_app_log_level) \
  { \
    tinyrpc::Logger::GetLogger()->pushAppLog(tinyrpc::LogEvent(tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");\
  } \

#define AppInfoLog(str, ...) \
  if (tinyrpc::OpenLog() && tinyrpc::LogLevel::INFO>= tinyrpc::gRpcConfig->m_app_log_level) \
  { \
    tinyrpc::Logger::GetLogger()->pushAppLog(tinyrpc::LogEvent(tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");\
  } \

#define AppWarnLog(str, ...) \
  if (tinyrpc::OpenLog() && tinyrpc::LogLevel::WARN>= tinyrpc::gRpcConfig->m_app_log_level) \
  { \
    tinyrpc::Logger::GetLogger()->pushAppLog(tinyrpc::LogEvent(tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");\
  } \

#define AppErrorLog(str, ...) \
  if (tinyrpc::OpenLog() && tinyrpc::LogLevel::ERROR>= tinyrpc::gRpcConfig->m_app_log_level) \
  { \
    tinyrpc::Logger::GetLogger()->pushAppLog(tinyrpc::LogEvent(tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");\
  } \



enum LogType {
    RPC_LOG = 1,
    APP_LOG = 2
};

// 获取线程ID
pid_t gettid();

LogLevel stringToLevel(const std::string& str);
std::string levelToString(LogLevel level);

// 检查日志是否开启
bool OpenLog(); 

std::string LogTypeToString(LogType logtype);

// 表示一个日志事件
class LogEvent {

private:
    timeval m_timeval;  // 时间戳
    LogLevel m_level;  // 日志级别

    pid_t m_pid {0};  // 进程IDD
    pid_t m_tid {0};  // 线程ID
    int m_cor_id {0};  // 协程ID

    const char* m_file_name;  // 源码文件名
    int m_line {0};  // 源码所在行
    const char* m_func_name;  // 源码函数名

    LogType m_type;  // 日志类型
    std::string m_msg_no;  // 消息编号

    std::stringstream m_ss;  // 字符串流，存储日志内容

public:
    typedef std::shared_ptr<LogEvent> ptr;

    LogEvent(LogLevel level, const char* file_name, int line, const char* func_name, LogType type);

    ~LogEvent();

    // 获取字符串流供写入日志内容 -- 负责格式化日志消息
    std::stringstream& getStringStream();

    // 将日志事件转换为字符串
    std::string toString();

    // 执行日志输出
    void log();

};

/**
 * 一个RAII包装类
 * 构造时，接受LogEvent智能指针
 * 析构时，自动调用LogEvent::log()输出日志
 * 这种设计使得日志宏可以使用流式语法，在表达式结束时自动输出
*/
class LogTmp {

private:
    LogEvent::ptr m_event;

public:
    explicit LogTmp(LogEvent::ptr event);

    ~LogTmp();

    std::stringstream& getStringStream();

};

/**
 * 异步日志写入器
 * 在独立线程中运行；
 * 维护日志任务队列；
 * 负责将日志写入文件；
 * 支持日志文件公洞（按日期和大小）
*/
class AsyncLogger {

private:
    const char* m_file_name;  // 文件名
    const char* m_file_path;  // 路径
    int m_max_size {0};  // 单个日志文件最大大小
    LogType m_log_type;  // 日志类型
    int m_no {0};
    bool m_need_reopen {false};
    FILE* m_file_handle {nullptr};  // 文件句柄
    std::string m_date;

    Mutex m_mutex;
    pthread_cond_t m_condition;
    bool m_stop {false};

public:
    typedef std::shared_ptr<AsyncLogger> ptr;

    pthread_t m_thread;  // 工作线程
    sem_t m_semaphore;

    std::queue<std::vector<std::string>> m_tasks;

    AsyncLogger(const char* file_name, const char* file_path, int max_size, LogType logtypr);
    ~AsyncLogger();

    void push(std::vector<std::string>& buffer);

    void flush();

    static void* excute(void*);

    void stop();
};

/**
 * 是日志系统的中心管理类
 * 设计亮点：
 *  1.双缓冲机制：维护两个缓冲区，定期swap并刷新到AsyncLogger，减少锁竞争
 *  2.级别过滤优化：日志宏在最外层就检查级别，不满足条件时是空操作，避免字符串格式化开销
 *  3.RAII自动输出：通过LogTmp的析构函数自动调用log()，使得流式语法成为可能
 *  4.分离RPC和APP日志：两种日志类型独立配置、独立缓冲、独立文件,便于分别管理和查看
*/
class Logger {

private:
    Mutex m_app_buff_mutex;
    Mutex m_buff_mutex;

    bool m_is_init {false};

    AsyncLogger::ptr m_async_rpc_logger;  // rpc异步日志写入器
    AsyncLogger::ptr m_async_app_logger;  // app异步日志写入器

    int m_sync_inteval {0};  // 同步间隔(ms)

public:
    typedef std::shared_ptr<Logger> ptr;

    std::vector<std::string> m_buffer;  // rpc日志缓冲区
    std::vector<std::string> m_app_buffer;  // app日志缓冲区

    Logger();
    ~Logger();

    static Logger* GetLogger();  // 获取单例实例

    AsyncLogger::ptr getAsyncLogger() {
        return m_async_rpc_logger;
    }

    AsyncLogger::ptr getAsyncAppLogger() {
        return m_async_app_logger;
    }

    void init(const char* file_name, const char* file_path, int max_size, int sync_inteval);
    
    // 推送rpc日志到缓冲区
    void pushRpcLog(const std::string& log_msg);
    // 推送app日志到缓冲区
    void pushAppLog(const std::string& log_msg);
    // 定期刷新日志到AsyncLogger
    void loopFunc();

    // 强制刷新所有日志
    void flush();

    // 启动日志系统
    void start();

};

}