#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <random>

#include <tinyrpc/comm/config.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/comm/msg_req.h>

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

// 当前线程的消息编号
static thread_local std::string t_msg_req_nu;

// 最大消息编号（全9字符串）
static thread_local std::string t_max_msg_req_nu;

// /dev/urandom 的文件描述符，用于生成随机数
static int g_random_fd = -1;

std::string MsgReqUtil::genMsgNumber() {
    /* 获取配置的编号长度 */
    int t_msg_req_len = 20;
    if (gRpcConfig) {
        // 从全局配置读取消息编号长度
        t_msg_req_len = gRpcConfig->msg_req_len_;
    }

    /* 判断是否需要重新生成随机数 */
    // 需要重新生成随机数
    if (t_msg_req_nu.empty() || t_msg_req_nu == t_max_msg_req_nu) {
        if (g_random_fd == -1) {
            g_random_fd = open("/dev/urandom", O_RDONLY);
        }
        std::string res(t_msg_req_len, 0);
        // 从 /dev/urandom生成新的随机数
        if ((read(g_random_fd, &res[0], t_msg_req_len)) != t_msg_req_len) {
            ErrorLog << "read /dev/urandom data less " << t_msg_req_len << "bytes";
            return "";
        }
        t_max_msg_req_nu = "";
        for (int i = 0; i < t_msg_req_len; i++) {
            uint8_t x = ((uint8_t)(res[i])) % 10;
            res[i] = x + '0';         // 将每个字节转换为0-9的数字字符
            t_max_msg_req_nu += "9";  // 同时生成最大值字符串（全9）
        }
        t_msg_req_nu = res;
    } else {  // 不需要重新生成字符串
        // 从字符串末尾开始查找第一个非'9'字符，将该字符加1，将其后的所有字符重置为'0'
        int i = t_msg_req_nu.length() - 1;
        while (t_msg_req_nu[i] == '9' && i >= 0) {
            i--;
        }
        if (i >= 0) {
            t_msg_req_nu[i] += 1;
            for (size_t j = i + 1; j < t_msg_req_nu.length(); j++) {
                t_msg_req_nu[j] = '0';
            }
        }
    }
    return t_msg_req_nu;
}

}  // namespace tinyrpc