#pragma once

#include <atomic>
#include <iostream>
#include <memory>
#include <vector>

/**
 * 内存块管理器，专门用于管理协程栈的内存分配，是协程池的重要组成部分
 */

namespace tinyrpc {

class Memory {
   private:
    int block_size_{0};   // 每个内存块的大小（字节）
    int block_count_{0};  // 内存块的数量

    int size_{0};        // 总内存大小 (block_size_ * block_count_)
    char* start_{NULL};  // 内存区域的起始地址
    char* end_{NULL};    // 内存区域的结束地址

    std::atomic<int> ref_counts_{0};  // 原子引用计数，记录当前被使用的块数
    std::vector<bool> blocks_;        // 布尔向量，标记每个块是否被使用
    std::mutex mtx_;                  // 互斥锁，保护并发访问

   public:
    typedef std::shared_ptr<Memory> ptr;

    // 构造函数，分配内存
    Memory(int block_size, int block_count);

    // 析构函数，释放内存
    ~Memory();

    int getRefCount();  // 获取状态信息
    char* getStart();   // 获取状态信息
    char* getEnd();     // 获取状态信息

    char* getBlock();  // 获取一个空闲的内存块

    void backBlock(char* s);  // 归还一个内存块

    bool hasBlock(char* s);  // 检查指针是否属于这个Memory对象
};

}  // namespace tinyrpc