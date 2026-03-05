#pragma once

#include <tinyxml.h>
#include <map>
#include <memory>
#include <string>

#ifdef DECLARE_MYSQL_PLUGIN
#    include "mysql_instase.h"
#endif

namespace tinyrpc {

enum LogLevel { DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4, NONE = 5 };

class Config {
   private:
    std::string file_path_;
    TiXmlDocument *xml_file_;  // xml文件

   public:
    // 日志参数 (log params)
    std::string log_path_;                     // 日志文件地址
    std::string log_prefix_;                   // 日志前缀
    int log_max_size_{0};                      // 日志文件最大容量
    LogLevel log_level_{LogLevel::DEBUG};      // 应用到rpc框架内部的日志等级
    LogLevel app_log_level_{LogLevel::DEBUG};  // 应用到app的日志等级
    int log_sync_interval_{500};               // 日志同步间隔

    // 协程参数 (coroutine params)
    int cor_stack_size_{0};
    int cor_pool_size_{0};

    int msg_req_len_{0};

    int max_connect_timeout_{0};  // 单位ms
    int iothread_num_{0};

    int timewheel_bucket_num_{0};  // 时间轮
    int timewheel_interval_{0};    // 时间轮间隔

#ifdef DECLARE_MYSQL_PLUGIN
    std::map<std::string, MySQLOption> m_mysql_options;
#endif

    typedef std::shared_ptr<Config> ptr;

    Config(const char *file_path);

    ~Config();

    void readConf();

    void readDBConfig(TiXmlElement *node);

    void readLogConfig(TiXmlElement *node);

    TiXmlElement *getXmlNode(const std::string &name);
};

}  // namespace tinyrpc