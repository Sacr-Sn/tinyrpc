#include <iostream>
#include <sys/time.h>

using namespace std;

int64_t getNowMs() {
    timeval val;
    gettimeofday(&val, nullptr);  // 获取当前时间
    // 将秒转换为毫秒并加上微秒转换的部分（在32位系统上要使用注释掉的写法，否则将溢出）
    // int64_t re = static_cast<int64_t>(val.tv_sec) * 1000 + val.tv_usec / 1000;
    int64_t re = val.tv_sec * 1000 + val.tv_usec / 1000;
    return re;  // 返回总毫秒数
}

int main() {
    int64_t ms = getNowMs();
    cout << "毫秒：" << ms << endl;
}
