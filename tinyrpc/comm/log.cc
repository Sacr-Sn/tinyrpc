#include <iostream>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <algorithm>
#include <errno.h>

#ifdef DECLARE_MYSQL_PLUGIN 
#include <mysql/mysql.h>
#endif

#include "log.h"
#include "run_time.h"
#include "../coroutine/coroutine.h"
#include "../net/reactor.h"
#include "../net/timer.h"



namespace tinyrpc {

class Coroutine;

static thread_local pid_t t_thread_id = 0;
static pid_t g_pid = 0;

extern tinyrpc::Logger::ptr gRpcLogger;
extern tinyrpc::Config::ptr gRpcConfig;

static std::atomic_int64_t g_rpc_log_index {0};
static std::atomic_int64_t g_app_log_index {0};

// 崩溃信号处理，确保了程序崩溃前所有日志都被写入磁盘
void CoredumpHandler(int signal_no) {
    // 输出错误日志和控制台信息
    ErrorLog << "progress received invalid signal, will exit";
    printf("progress received invalid signal, will exit\n");
    // 强制刷新所有日志
    gRpcLogger->flush();
    // 等待两个AsyncLogger线程完成
    pthread_join(gRpcLogger->getAsyncLogger()->m_thread, NULL);
    pthread_join(gRpcLogger->getAsyncAppLogger()->m_thread, NULL);

    /**
     * signal函数用于修改信号的处理方式，SIG_DEL代表使用信号的默认处理方式
     * signal_no是传递给处理程序的信号号码
     * 调用这行代码时，它设置信号处理程序为默认处理程序，通常是程序终止并可能生成core dump
    */
    signal(signal_no, SIG_DFL);
    /**
     * raise函数用来发送一个信号给当前进程，这里它发送之前接收到的signal_no信号，触发操作系统生成崩溃信息或 core dump 文件
    */
    raise(signal_no);
}

pid_t gettid() {
    if (t_thread_id == 0) {
        t_thread_id = syscall(SYS_gettid);
    }
    return t_thread_id;
}

void setLogLevel(LogLevel level) {
    // g_log_level = level;
}

bool OpenLog() {
    if (!gRpcLogger) {
        return false;
    }
    return true;
}

LogLevel stringToLevel(const std::string& str) {
    if (str == "DEBUG")  return LogLevel::DEBUG;

    if (str == "INFO")  return LogLevel::INFO;

    if (str == "WARN")  return LogLevel::WARN;

    if (str == "ERROR")  return LogLevel::ERROR;

    if (str == "NONE")  return LogLevel::NONE;

    return LogLevel::DEBUG;
}

std::string levelToString(LogLevel level) {
    std::string re = "DEBUG";
    switch (level) {
        case DEBUG:
            re = "DEBUG";
            return re;
        case INFO:
            re = "INFO";
            return re;
        case WARN:
            re = "WARN";
            return re;
        case ERROR:
            re = "ERROR";
            return re;
        case NONE:
            re = "NONE";
            return re;
        default:
            return re;
    }
}

std::string LogTypeToString(LogType logtype) {
    switch (logtype) {
        case APP_LOG:
            return "app";
        case RPC_LOG:
            return "rpc";
        default:
            return "";
    }
}

void Exit(int code) {
    #ifdef DECLARE_MYSQL_PLUGIN
    mysql_library_end();
    #endif

    printf("It's sorry to said we start TinyRPC server error, look up log file to get more details!\n");
    gRpcLogger->flush();
    pthread_join(gRpcLogger->getAsyncLogger()->m_thread, NULL);

    _exit(code);
}


LogEvent::LogEvent(LogLevel level, const char* file_name, int line, const char* func_name, LogType type)
    : m_level(level), m_file_name(file_name), m_line(line), m_func_name(func_name), m_type(type) {}

LogEvent::~LogEvent() {}

std::stringstream& LogEvent::getStringStream() {
    // 获取时间戳
    gettimeofday(&m_timeval, nullptr);  // 获取当前时间
    struct tm time;
    localtime_r(&(m_timeval.tv_sec), &time);  // 转换为本地时间
    const char* format = "%Y-%m-%d %H:%M:%S";
    char buf[128];
    strftime(buf, sizeof(buf), format, &time);  // 格式化时间

    m_ss << "[" << buf << "." << m_timeval.tv_usec << "]\t";

    // 添加日志级别
    std::string s_level = levelToString(m_level);
    m_ss << "[" << s_level << "]\t";

    // 添加进程和线程信息、协程信息
    if (g_pid == 0) {
        g_pid = getpid();
    }
    m_pid = g_pid;
    if (t_thread_id == 0) {
        t_thread_id = gettid();
    }
    m_tid = t_thread_id;
    m_cor_id = Coroutine::GetCurrentCoroutine()->getCorId();
    // 添加源码位置 -- 文件名和行号
    m_ss << "[" << m_pid << "]\t"
        << "[" << m_tid << "]\t"
        << "[" << m_cor_id << "]\t"
        << "[" << m_file_name << ":" << m_line << "]\t";
    
    // 添加运行时上下文
    RunTime* runtime = getCurrentRunTime();
    if (runtime) {
        std::string msgno = runtime->m_msg_no;
        if (!msgno.empty()) {
            m_ss << "[" << msgno << "]\t";
        }

        std::string interface_name = runtime->m_interface_name;
        if (!interface_name.empty()) {
            m_ss << "[" << interface_name << "]\t";
        }
    }
    return m_ss;
}

std::string LogEvent::toString() {
    return getStringStream().str();
}

void LogEvent::log() {
    m_ss << "\n";
    if (m_level >= gRpcConfig->m_log_level && m_type == RPC_LOG) {
        gRpcLogger->pushRpcLog(m_ss.str());
    } else if (m_level >= gRpcConfig->m_app_log_level && m_type == APP_LOG) {
        gRpcLogger->pushAppLog(m_ss.str());
    }
}


LogTmp::LogTmp(LogEvent::ptr event) : m_event(event) {}

LogTmp::~LogTmp() {
    m_event->log();
}

std::stringstream& LogTmp::getStringStream() {
    return m_event->getStringStream();
}


AsyncLogger::AsyncLogger(const char* file_name, const char* file_path, int max_size, LogType logtype)
    : m_file_name(file_name), m_file_path(file_path), m_max_size(max_size), m_log_type(logtype) {
    int rt = sem_init(&m_semaphore, 0, 0);  // 初始化信号量
    assert(rt == 0);

    // 创建工作线程，执行excute函数
    rt = pthread_create(&m_thread, nullptr, &AsyncLogger::excute, this);
    assert(rt == 0);
    rt = sem_wait(&m_semaphore);  // 等待信号量（确保线程已启动）
    assert(rt == 0);
}

AsyncLogger::~AsyncLogger() {}

// 工作线程主循环
void* AsyncLogger::excute(void* arg) {
    // 初始化条件变量
    AsyncLogger* ptr = reinterpret_cast<AsyncLogger*>(arg);
    int rt = pthread_cond_init(&ptr->m_condition, NULL);
    assert(rt == 0);

    rt = sem_post(&ptr->m_semaphore);
    assert(rt == 0);

    // 等待任务
    while (1) {
        Mutex::Lock lock(ptr->m_mutex);
        // 使用条件变量等待任务队列非空
        while (ptr->m_tasks.empty() && !ptr->m_stop) {
            // 等待时会释放ptr->m_mutex，避免死锁；在条件满足并且线程被唤醒后，pthread_cond_wait会重新获取传入的锁
            pthread_cond_wait(&(ptr->m_condition), ptr->m_mutex.getMutex());
        }
        std::vector<std::string> tmp;
        // 取出队列头部的日志缓冲区
        tmp.swap(ptr->m_tasks.front());
        ptr->m_tasks.pop();
        bool is_stop = ptr->m_stop;
        lock.unlock();

        // 检查日期变化
        timeval now;
        gettimeofday(&now, nullptr);  // 获取当前日期
        struct tm now_time;
        localtime_r(&(now.tv_sec), &now_time);
        const char *format = "%Y%m%d";
        char date[32];
        strftime(date, sizeof(date), format, &now_time);
        if (ptr->m_date != std::string(date)) {  // 如果跨天，重置文件编号并标记需要重新打开文件
            ptr->m_no = 0;
            ptr->m_date = std::string(date);
            ptr->m_need_reopen = true;
        }
        if (!ptr->m_file_handle) {  // 检查文件句柄
            ptr->m_need_reopen = true;
        }

        // 构造文件名
        std::stringstream ss;
        ss << ptr->m_file_path << ptr->m_file_name << "_" << ptr->m_date << " " << LogTypeToString(ptr->m_log_type) << "_"
            << ptr->m_no << ".log";
        std::string full_file_name = ss.str();

        if (ptr->m_need_reopen) {
            if (ptr->m_file_handle) {
                fclose(ptr->m_file_handle);  // 关闭原来的日志文件
            }
            // 以追加写方式打开新日志文件
            ptr->m_file_handle = fopen(full_file_name.c_str(), "a");
            if (ptr->m_file_handle == nullptr) {
                printf("open failed, errno = %d, reason = %s \n", errno, strerror(errno));
            }
            ptr->m_need_reopen = false;
        }

        // 检查文件大小
        if (ftell(ptr->m_file_handle) > ptr->m_max_size) {  // 使用ftell()获取当前文件大小
            fclose(ptr->m_file_handle);  // 关闭旧日志文件
            ptr->m_no++;  // 递增编号
            std::stringstream ss2;
            ss2 << ptr->m_file_path << ptr->m_file_name << "_" << ptr->m_date << "_" << LogTypeToString(ptr->m_log_type) << "_"
                << ptr->m_no << ".log";
            full_file_name = ss2.str();
            ptr->m_file_handle = fopen(full_file_name.c_str(), "a");
            ptr->m_need_reopen = false;
        }

        if (!ptr->m_file_handle) {
            printf("open log file %s failed!", full_file_name.c_str());
        }

        // 写入日志
        for (auto log_str : tmp) {  // 遍历缓冲区中所有日志
            if (!log_str.empty()) {
                fwrite(log_str.c_str(), 1, log_str.length(), ptr->m_file_handle);
            }
        }
        tmp.clear();
        fflush(ptr->m_file_handle);  // 刷新文件缓冲区
        if (is_stop) {
            break;
        }
    }
    if (!ptr->m_file_handle) {
        fclose(ptr->m_file_handle);
    }
    return nullptr;
}

// 添加日志任务
void AsyncLogger::push(std::vector<std::string>& buffer) {
    if (!buffer.empty()) {
        Mutex::Lock lock(m_mutex);
        m_tasks.push(buffer);
        lock.unlock();
        // 发送条件变量信号唤醒工作线程
        pthread_cond_signal(&m_condition);
    }
}

void AsyncLogger::flush() {
    if (m_file_handle) {
        fflush(m_file_handle);
    }
}

void AsyncLogger::stop() {
    if (!m_stop) {
        m_stop = true;
        pthread_cond_signal(&m_condition);
    }
}


Logger::Logger() {}

Logger::~Logger() {
    flush();
    pthread_join(m_async_rpc_logger->m_thread, NULL);
    pthread_join(m_async_app_logger->m_thread, NULL);
}

Logger* Logger::GetLogger() {
    return gRpcLogger.get();
}

void Logger::init(const char* file_name, const char* file_path, int max_size, int sync_inteval) {
    if (!m_is_init) {
        m_sync_inteval = sync_inteval;
        // 预分配缓冲区，为两个缓冲区各预分配100万个空串，可以避免频繁的内存分配
        for (int i=0; i<1000000; i++) {
            m_app_buffer.push_back("");
            m_buffer.push_back("");
        }

        m_async_rpc_logger = std::make_shared<AsyncLogger>(file_name, file_path, max_size, RPC_LOG);
        m_async_app_logger = std::make_shared<AsyncLogger>(file_name, file_path, max_size, APP_LOG);

        // 注册信号处理器
        signal(SIGSEGV, CoredumpHandler);
        signal(SIGABRT, CoredumpHandler);
        signal(SIGTERM, CoredumpHandler);
        signal(SIGKILL, CoredumpHandler);
        signal(SIGINT, CoredumpHandler);
        signal(SIGSTKFLT, CoredumpHandler);

        // SIGPIPE设置为忽略，避免写入关闭的socket导致程序退出
        signal(SIGPIPE, SIG_IGN);
        m_is_init = true;
    }
}

void Logger::start() {
    // 创建一个重复定时器，定期调用loopFunc()刷新日志缓冲区
    TimerEvent::ptr event = std::make_shared<TimerEvent>(m_sync_inteval, true, std::bind(&Logger::loopFunc, this));
    Reactor::GetReactor()->getTimer()->addTimerEvent(event);
}

// 定期刷新缓冲区
void Logger::loopFunc() {  
    std::vector<std::string> app_tmp;
    Mutex::Lock lock1(m_app_buff_mutex);
    app_tmp.swap(m_app_buffer);
    lock1.unlock();

    std::vector<std::string> tmp;
    Mutex::Lock lock2(m_buff_mutex);
    tmp.swap(m_buffer);
    lock2.unlock();

    m_async_rpc_logger->push(tmp);
    m_async_app_logger->push(app_tmp);
}

// 推送单条日志到缓冲区
void Logger::pushRpcLog(const std::string& msg) {
    Mutex::Lock lock(m_buff_mutex);
    m_buffer.push_back(std::move(msg));  // move避免字符串拷贝
    lock.unlock();
}

// 推送单条消息到缓冲区
void Logger::pushAppLog(const std::string& msg) {
    Mutex::Lock lock(m_app_buff_mutex);
    m_app_buffer.push_back(std::move(msg));
    lock.unlock();
}

// 强制刷新 -- 在程序退出或崩溃时被调用
void Logger::flush() {
    loopFunc();  // 刷新缓冲区
    m_async_rpc_logger->stop();  // 停止AsyncLogger
    m_async_rpc_logger->flush();  // 确保日志写入磁盘

    m_async_app_logger->stop();
    m_async_app_logger->flush();
}

}