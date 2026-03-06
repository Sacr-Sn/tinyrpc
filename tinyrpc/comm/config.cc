#include <assert.h>
#include <stdio.h>
#include <tinyxml.h>
#include <algorithm>
#include <memory>

// #include #include <tinyrpc/comm/config.h>
// #include <tinyrpc/comm/log.h>
// #include <tinyrpc/net/comm/net_address.h>
// #include <tinyrpc/net/tcp/tcp_server.h>
#include <tinyrpc/comm/config.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/net/comm/net_address.h>
#include <tinyrpc/net/tcp/tcp_server.h>

namespace tinyrpc {

extern tinyrpc::Logger::ptr gRpcLogger;     // 全局日志对象
extern tinyrpc::TcpServer::ptr gRpcServer;  // 全局TCP服务器对象

// 构造函数 - 加载XML文件
Config::Config(const char *file_path) : file_path_(std::string(file_path)) {
    xml_file_ = new TiXmlDocument();
    bool rt = xml_file_->LoadFile(file_path);  // 加载XML文件
    if (!rt) {
        printf(
            "start tinyrpc server error! read conf file [%s] error info: "
            "[%s], errorid: [%d], error_row_column:[%d row %d column]\n",
            file_path, xml_file_->ErrorDesc(), xml_file_->ErrorId(), xml_file_->ErrorRow(), xml_file_->ErrorCol());
        exit(0);
    }
}

// 读取日志配置并初始化日志系统
void Config::readLogConfig(TiXmlElement *log_node) {
    TiXmlElement *node = log_node->FirstChildElement("log_path");
    if (!node || !node->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [log_path] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    log_path_ = std::string(node->GetText());

    node = log_node->FirstChildElement("log_prefix");
    if (!node || !node->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [log_prefix] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    log_prefix_ = std::string(node->GetText());

    node = log_node->FirstChildElement("log_max_file_size");
    if (!node || !node->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [log_max_file_size] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    int log_max_size = std::atoi(node->GetText());
    log_max_size_ = log_max_size * 1024 * 1024;

    node = log_node->FirstChildElement("rpc_log_level");
    if (!node || !node->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [rpc_log_level] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    std::string log_level = std::string(node->GetText());
    log_level_ = stringToLevel(log_level);

    node = log_node->FirstChildElement("app_log_level");
    if (!node || !node->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [app_log_level] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    log_level = std::string(node->GetText());
    app_log_level_ = stringToLevel(log_level);

    node = log_node->FirstChildElement("log_sync_interval");
    if (!node || !node->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [log_sync_interval] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    log_sync_interval_ = std::atoi(node->GetText());

    // 在读取完所有日志配置后,创建并初始化全局 Logger 对象
    gRpcLogger = std::make_shared<Logger>();
    gRpcLogger->init(log_prefix_.c_str(), log_path_.c_str(), log_max_size_, log_sync_interval_);
}

void Config::readDBConfig(TiXmlElement *node) {
#ifdef DECLARE_MYSQL_PLUGIN

    printf("read db config\n");
    if (!node) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [database] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    for (TiXmlElement *element = node->FirstChildElement("db_key"); element != NULL;
         element = element->NextSiblingElement()) {
        std::string key = element->FirstAttribute()->Value();
        printf("key is %s\n", key.c_str());
        TiXmlElement *ip_e = element->FirstChildElement("ip");
        std::string ip;
        int port = 3306;
        if (ip_e) {
            ip = std::string(ip_e->GetText());
        }
        if (ip.empty()) {
            continue;
        }

        TiXmlElement *port_e = element->FirstChildElement("port");
        if (port_e && port_e->GetText()) {
            port = std::atoi(port_e->GetText());
        }

        MySQLOption option(IPAddress(ip, port));

        TiXmlElement *user_e = element->FirstChildElement("user");
        if (user_e && user_e->GetText()) {
            option.m_user = std::string(user_e->GetText());
        }

        TiXmlElement *passwd_e = element->FirstChildElement("passwd");
        if (passwd_e && passwd_e->GetText()) {
            option.m_passwd = std::string(passwd_e->GetText());
        }

        TiXmlElement *select_db_e = element->FirstChildElement("select_db");
        if (select_db_e && select_db_e->GetText()) {
            option.m_select_db = std::string(select_db_e->GetText());
        }

        TiXmlElement *char_set_e = element->FirstChildElement("char_set");
        if (char_set_e && char_set_e->GetText()) {
            option.m_char_set = std::string(char_set_e->GetText());
        }
        m_mysql_options.insert(std::make_pair(key, option));

        std::string info = std::format(
            "read config from file [{}], key:{}, [addr: {}, user: {}, passwd: {}, select_db:{}, charset:{}}\n",
            file_path_, key, option.m_addr.toString(), option.m_user, option.m_passwd, option.m_select_db,
            option.m_char_set);
        InfoLog << info;
    }

#endif
}

// 按顺序读取所有配置项并初始化相应组件
void Config::readConf() {
    TiXmlElement *root = xml_file_->RootElement();

    // 获取<log>结点并调用readLogConfig()
    TiXmlElement *log_node = root->FirstChildElement("log");
    if (!log_node) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [log] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    readLogConfig(log_node);

    TiXmlElement *time_wheel_node = root->FirstChildElement("time_wheel");
    if (!time_wheel_node) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [time_wheel] xml node\n",
            file_path_.c_str());
        exit(0);
    }

    // 读取协程相关配置
    TiXmlElement *coroutine_node = root->FirstChildElement("coroutine");
    if (!coroutine_node) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [coroutine] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    // 协程栈大小(KB),转换为字节
    if (!coroutine_node->FirstChildElement("coroutine_stack_size") ||
        !coroutine_node->FirstChildElement("coroutine_stack_size")->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [coroutine.coroutine_stack_size] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    // 协程池大小
    if (!coroutine_node->FirstChildElement("coroutine_pool_size") ||
        !coroutine_node->FirstChildElement("coroutine_pool_size")->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [coroutine.coroutine_pool_size] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    int cor_stack_size = std::atoi(coroutine_node->FirstChildElement("coroutine_stack_size")->GetText());
    cor_stack_size_ = 1024 * cor_stack_size;
    cor_pool_size_ = std::atoi(coroutine_node->FirstChildElement("coroutine_pool_size")->GetText());

    // 读取消息请求编号长度：msg_req_len
    if (!root->FirstChildElement("msg_req_len") || !root->FirstChildElement("msg_req_len")->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [msg_req_len] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    msg_req_len_ = std::atoi(root->FirstChildElement("msg_req_len")->GetText());

    // 读取最大连接超时时间（秒），max_connect_timeout，转换为毫秒
    if (!root->FirstChildElement("max_connect_timeout") || !root->FirstChildElement("max_connect_timeout")->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [max_connect_timeout] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    int max_connect_timeout = std::atoi(root->FirstChildElement("max_connect_timeout")->GetText());
    max_connect_timeout_ = max_connect_timeout * 1000;

    // 读取IO线程数量 iothread_num
    if (!root->FirstChildElement("iothread_num") || !root->FirstChildElement("iothread_num")->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [iothread_num] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    iothread_num_ = std::atoi(root->FirstChildElement("iothread_num")->GetText());

    // 读取时间轮配置
    // 桶的数量 bucket_num
    if (!time_wheel_node->FirstChildElement("bucket_num") ||
        !time_wheel_node->FirstChildElement("bucket_num")->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [time_wheel.bucket_num] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    // 清理间隔（秒）
    if (!time_wheel_node->FirstChildElement("interval") || !time_wheel_node->FirstChildElement("interval")->GetText()) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [time_wheel.bucket_num] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    timewheel_bucket_num_ = std::atoi(time_wheel_node->FirstChildElement("bucket_num")->GetText());
    timewheel_interval_ = std::atoi(time_wheel_node->FirstChildElement("interval")->GetText());

    // 读取服务器配置并创建 TcpServer
    TiXmlElement *net_node = root->FirstChildElement("server");
    if (!net_node) {
        printf(
            "start tinyrpc server error! read config file [%s] error, "
            "cannot read [server] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    if (!net_node->FirstChildElement("ip") || !net_node->FirstChildElement("port") ||
        !net_node->FirstChildElement("protocol")) {
        printf(
            "start tinyrpc server error! read config file [%s] error, cannot "
            "read [server.ip] or [server.port] or [server.protocol] xml node\n",
            file_path_.c_str());
        exit(0);
    }
    std::string ip = std::string(net_node->FirstChildElement("ip")->GetText());
    // 如果IP为空，默认使用0.0.0.0
    if (ip.empty()) {
        ip = "0.0.0.0";
    }
    int port = std::atoi(net_node->FirstChildElement("port")->GetText());
    if (port == 0) {
        printf(
            "start tinyrpc server error! read config file [%s] error, read "
            "[server.port] = 0\n",
            file_path_.c_str());
        exit(0);
    }
    std::string protocol = std::string(net_node->FirstChildElement("protocol")->GetText());
    // 将协议字符串转换为大写
    std::transform(protocol.begin(), protocol.end(), protocol.begin(), toupper);

    // 创建IPAddress对象
    tinyrpc::IPAddress::ptr addr = std::make_shared<tinyrpc::IPAddress>(ip, port);

    // 根据协议类型创建对应的TcpServer，这个全局gRpcServer是整个服务端的核心
    if (protocol == "HTTP") {
        gRpcServer = std::make_shared<TcpServer>(addr, Http_Protocol);
    } else {
        gRpcServer = std::make_shared<TcpServer>(addr, TinyPb_Protocol);
    }

    // 输出配置摘要
    std::string info = std::format(
        "read config from file [{}]: [log_path: {}], [log_prefix: {}], [log_max_size: {} MB], [log_level: {}], "
        "[coroutine_stack_size: {} KB], [coroutine_pool_size: {}], [msg_req_len: {}], [max_connect_timeout: {} s], "
        "[iothread_num: {}], [timewheel_bucket_num: {}], [timewheel_interval: {} s], [server_ip: {}], [server_Port: "
        "{}], [server_protocol: {}]\n",
        file_path_, log_path_, log_prefix_, log_max_size_ / 1024 / 1024, levelToString(log_level_), cor_stack_size,
        cor_pool_size_, msg_req_len_, max_connect_timeout, iothread_num_, timewheel_bucket_num_, timewheel_interval_,
        ip, port, protocol);
    InfoLog << info;

    // 读取数据库配置（可选）
    TiXmlElement *database_node = root->FirstChildElement("database");

    if (database_node) {
        readDBConfig(database_node);
    }
}

Config::~Config() {
    if (xml_file_) {
        delete xml_file_;
        xml_file_ = NULL;
    }
}

// 用于获取根节点下的指定子节点。
TiXmlElement *Config::getXmlNode(const std::string &name) {
    return xml_file_->RootElement()->FirstChildElement(name.c_str());
}

}  // namespace tinyrpc