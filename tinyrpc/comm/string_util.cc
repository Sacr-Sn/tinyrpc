// #include <tinyrpc/comm/string_util.h>
// #include <tinyrpc/comm/log.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/comm/string_util.h>

namespace tinyrpc {

void StringUtil::SplitStrToMap(const std::string& str, const std::string& split_str, const std::string& joiner,
                               std::map<std::string, std::string>& res) {
    if (str.empty() || split_str.empty() || joiner.empty()) {
        DebugLog << "str or split_str or joiner_str is empty";
        return;
    }

    std::string tmp = str;
    std::vector<std::string> vec;
    SplitStrToVector(tmp, split_str, vec);
    for (auto kv : vec) {
        if (!kv.empty()) {
            size_t j = kv.find_first_of(joiner);
            if (j != kv.npos && j != 0) {
                std::string key = kv.substr(0, j);
                std::string value = kv.substr(j + joiner.length(), kv.length() - j - joiner.length());
                DebugLog << "insert key = " << key << ", value = " << value;
                res[key.c_str()] = value;
            }
        }
    }
}

void StringUtil::SplitStrToVector(const std::string& str, const std::string& split_str, std::vector<std::string>& res) {
    if (str.empty() || split_str.empty()) {
        return;
    }

    std::string tmp = str;
    // 如果字符串末尾没有分隔符,自动添加
    if (tmp.substr(tmp.length() - split_str.length(), split_str.length()) != split_str) {
        tmp += split_str;
    }

    while (1) {
        size_t i = tmp.find_first_of(split_str);
        if (i == tmp.npos) {
            return;
        }
        int len = tmp.length();
        std::string kv = tmp.substr(0, i);
        tmp = tmp.substr(i + split_str.length(), len - i - split_str.length());
        if (!kv.empty()) {
            res.push_back(std::move(kv));
        }
    }
}

}  // namespace tinyrpc