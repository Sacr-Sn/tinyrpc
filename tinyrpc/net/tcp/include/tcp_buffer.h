#pragma once

#include <memory>
#include <vector>

/**
 * TCP缓冲区管理类，用于管理网络连接的读写缓冲区
 *
 * 采用双指针设计：
 *  读指针和写指针之间的区域是可读数据
 *  写指针到缓冲区末尾是可写数据、
 *  通过移动指针而不是移动数据来提高效率
 */

namespace tinyrpc {

class TcpBuffer {
   private:
    int read_index_{0};   // 读索引，指向下一个可读位置
    int write_index_{0};  // 写索引，指向下一个可写位置
    int size_{0};

   public:
    std::vector<char> buffer;  // 存储实际数据

    typedef std::shared_ptr<TcpBuffer> ptr;

    explicit TcpBuffer(int size);

    ~TcpBuffer();

    int readAble();  // 返回可读字节数

    int writeAble();  // 返回可写字节数

    int readIndex() const;

    int writeIndex() const;

    void writeToBuffer(const char* buf, int size);

    void readFromBuffer(std::vector<char>& re, int size);

    void resizeBuffer(int size);

    void clearBuffer();

    int getSize();

    std::vector<char> getBufferVector();

    // 将可读数据转换为字符串
    std::string getBufferString();

    // 将读索引前移 index 个位置
    void recycleRead(int index);

    // 将写索引前移 index 个位置
    void recycleWrite(int index);

    // 当读索引超过缓冲区大小的 1/3 时整理缓冲区，避免缓冲区前部出现大量已读空间,提高空间利用率。
    void adjustBuffer();
};

}  // namespace tinyrpc