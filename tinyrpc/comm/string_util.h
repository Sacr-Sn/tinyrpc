#pragma once

#include <string>
#include <map>
#include <vector>

/**
 * 字符串工具类，提供了字符串分割和解析
 * 主要用于解析HTTP请求中的查询参数和请求头
*/

namespace tinyrpc {

class StringUtil {

public:
    /**
     * 将字符串按照分隔符和连接符解析为键值对map
     * str：待解析的字符串  -- "a=1&tt=2&cc=3"
     * split_str：分隔符，用于分割不同的键值对  --  "&"
     * joiner：连接符，用于分割键和值  -- "="
     * res：输出参数，存储解析结果的map  --   {"a":"1", "tt":"2", "cc":"3"} 
    */
    static void SplitStrToMap(const std::string& str, const std::string& split_str,
        const std::string& joiner, std::map<std::string, std::string>& res);

    /**
     * 将字符串按照分隔符分割为字符串数组
     * str：待解析的字符串  --  "a=1&tt=2&cc=3"
     * split_str：分隔符  --  "&"
     * res：输出参数，存储分割后的字符串数组  --  ["a=1", "tt=2", "cc=3"]
    */
    static void SplitStrToVector(const std::string& str, const std::string& split_str,
        std::vector<std::string>& res);

};

}