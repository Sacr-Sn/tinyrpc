#pragma once

#include <semaphore.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <condition_variable>
#include <format>
#include <memory>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

#include "config.h"

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

template <typename... Args>
std::string formatString(const std::string_view fmt, Args&&... args) {
    return std::vformat(fmt, std::make_format_args(args...));
}

#define DebugLog                                                                                               \
    if (tinyrpc::OpenLog() && tinyrpc::LogLevel::DEBUG >= tinyrpc::gRpcConfig->log_level_)                     \
    tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, \
                                                                 __func__, tinyrpc::LogType::RPC_LOG)))        \
        .getStringStream()

#define InfoLog                                                                                               \
    if (tinyrpc::OpenLog() && tinyrpc::LogLevel::INFO >= tinyrpc::gRpcConfig->log_level_)                     \
    tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::INFO, __FILE__, __LINE__, \
                                                                 __func__, tinyrpc::LogType::RPC_LOG)))       \
        .getStringStream()

#define WarnLog                                                                                               \
    if (tinyrpc::OpenLog() && tinyrpc::LogLevel::WARN >= tinyrpc::gRpcConfig->log_level_)                     \
    tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::WARN, __FILE__, __LINE__, \
                                                                 __func__, tinyrpc::LogType::RPC_LOG)))       \
        .getStringStream()

#define ErrorLog                                                                                               \
    if (tinyrpc::OpenLog() && tinyrpc::LogLevel::ERROR >= tinyrpc::gRpcConfig->log_level_)                     \
    tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, \
                                                                 __func__, tinyrpc::LogType::RPC_LOG)))        \
        .getStringStream()

#define AppDebugLog(str, ...)                                                                                    \
    if (tinyrpc::OpenLog() && tinyrpc::LogLevel::DEBUG >= tinyrpc::gRpcConfig->app_log_level_) {                 \
        tinyrpc::Logger::GetLogger()->pushAppLog(                                                                \
            tinyrpc::LogEvent(tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG) \
                .toString() +                                                                                    \
            "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" +                               \
            tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");                                                   \
    }

#define AppInfoLog(str, ...)                                                                                    \
    if (tinyrpc::OpenLog() && tinyrpc::LogLevel::INFO >= tinyrpc::gRpcConfig->app_log_level_) {                 \
        tinyrpc::Logger::GetLogger()->pushAppLog(                                                               \
            tinyrpc::LogEvent(tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG) \
                .toString() +                                                                                   \
            "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" +                              \
            tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");                                                  \
    }

#define AppWarnLog(str, ...)                                                                                    \
    if (tinyrpc::OpenLog() && tinyrpc::LogLevel::WARN >= tinyrpc::gRpcConfig->app_log_level_) {                 \
        tinyrpc::Logger::GetLogger()->pushAppLog(                                                               \
            tinyrpc::LogEvent(tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG) \
                .toString() +                                                                                   \
            "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" +                              \
            tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");                                                  \
    }

#define AppErrorLog(str, ...)                                                                                    \
    if (tinyrpc::OpenLog() && tinyrpc::LogLevel::ERROR >= tinyrpc::gRpcConfig->app_log_level_) {                 \
        tinyrpc::Logger::GetLogger()->pushAppLog(                                                                \
            tinyrpc::LogEvent(tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG) \
                .toString() +                                                                                    \
            "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" +                               \
            tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");                                                   \
    }

enum LogType {
    RPC_LOG = 1,
    APP_LOG = 2,
};

pid_t gettid();

LogLevel stringToLevel(const std::string& str);
std::string levelToString(LogLevel level);

bool OpenLog();

class LogEvent {
   public:
    typedef std::shared_ptr<LogEvent> ptr;
    LogEvent(LogLevel level, const char* file_name, int line, const char* func_name, LogType type);

    ~LogEvent();

    std::stringstream& getStringStream();

    std::string toString();

    void log();

   private:
    // uint64_t m_timestamp;
    timeval timeval_;  // 时间间隔
    LogLevel level_;   // 日志级别
    pid_t pid_{0};
    pid_t tid_{0};
    int cor_id_{0};  // 协程ID

    const char* file_name_;  // 文件名(源文件，不是日志文件)
    int line_{0};            // 行号
    const char* func_name_;  // 函数名
    LogType type_;           // 日志类型，RPC日志还是APP日志
    std::string msg_no_;     // 日志编号

    std::stringstream ss_;  // 日志内容流
};

class LogTmp {
   public:
    explicit LogTmp(LogEvent::ptr event);

    ~LogTmp();

    std::stringstream& getStringStream();

   private:
    LogEvent::ptr event_;
};

class AsyncLogger {
   public:
    typedef std::shared_ptr<AsyncLogger> ptr;

    AsyncLogger(const char* file_name, const char* file_path, int max_size, LogType logtype);
    ~AsyncLogger();

    void push(std::vector<std::string>& buffer);

    void flush();

    static void* execute(void*);

    void stop();

   public:
    std::queue<std::vector<std::string>> tasks;

   private:
    const char* file_name_;
    const char* file_path_;
    int max_size_{0};
    LogType log_type_;
    int no_{0};
    bool need_reopen_{false};
    FILE* file_handle_{nullptr};
    std::string date_;

    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_{false};

   public:
    std::thread thread_;
    sem_t semaphore_;
};

class Logger {
   public:
    static Logger* GetLogger();

   public:
    typedef std::shared_ptr<Logger> ptr;

    Logger();
    ~Logger();

    void init(const char* file_name, const char* file_path, int max_size, int sync_interval);

    void pushRpcLog(const std::string& log_msg);
    void pushAppLog(const std::string& log_msg);
    void loopFunc();

    void flush();

    void start();

    AsyncLogger::ptr getAsyncLogger() { return async_rpc_logger_; }

    AsyncLogger::ptr getAsyncAppLogger() { return async_app_logger_; }

   public:
    std::vector<std::string> buffer;      // RPC日志缓冲区
    std::vector<std::string> app_buffer;  // APP日志缓冲区

   private:
    std::mutex app_buff_mtx_;
    std::mutex buff_mtx_;
    bool is_init_{false};
    AsyncLogger::ptr async_rpc_logger_;
    AsyncLogger::ptr async_app_logger_;

    int sync_interval_{0};
};

void Exit(int code);

}  // namespace tinyrpc
