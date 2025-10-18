#pragma once

#include <map>
#include <memory>
#include <string>
#include <tinyxml.h>

#ifdef DECLARE_MYSQL_PLUGIN
#include "tinyrpc/comm/mysql_instase.h"
#endif

namespace tinyrpc {

enum LogLevel { DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4, NONE = 5 };

class Config {

  private:
    std::string m_file_path;
    TiXmlDocument *m_xml_file; // xml文件

  public:
    // 日志参数 (log params)
    std::string m_log_path;                    // 日志文件地址
    std::string m_log_prefix;                  // 日志前缀
    int m_log_max_size{0};                     // 日志文件最大容量
    LogLevel m_log_level{LogLevel::DEBUG};     // 应用到rpc框架内部的日志等级
    LogLevel m_app_log_level{LogLevel::DEBUG}; // 应用到app的日志等级
    int m_log_sync_inteval{500};               // 日志同步间隔

    // 协程参数 (coroutine params)
    int m_cor_stack_size{0};
    int m_cor_pool_size{0};

    int m_msg_req_len{0};

    int m_max_connect_timeout{0}; // 单位ms
    int m_iothread_num{0};

    int m_timewheel_bucket_num{0}; // 时间轮
    int m_timewheel_inteval{0};    // 时间轮间隔

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

} // namespace tinyrpc