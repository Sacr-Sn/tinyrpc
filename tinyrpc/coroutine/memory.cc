#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "log.h"
#include "memory.h"

namespace tinyrpc {

// 构造函数 -- 分配内存
Memory::Memory(int block_size, int block_count) : block_size_(block_size), block_count_(block_count) {
    size_ = block_count_ * block_size_;  // 计算内存总大小
    start_ = (char*)malloc(size_);       // 分配内存，返回起始地址
    assert(start_ != (void*)-1);
    InfoLog << "succ mmap " << size_ << "bytes memory";
    end_ = start_ + size_ - 1;  // 计算结束地址
    blocks_.resize(block_count_);
    for (size_t i = 0; i < blocks_.size(); i++) {  // 初始化块状态
        blocks_[i] = false;                        // 表示块未使用
    }
    ref_counts_ = 0;  // 初始化引用计数
}

// 析构函数 -- 释放内存
Memory::~Memory() {
    if (!start_ || start_ == (void*)-1) {  // 检查内存指针是否有效
        return;
    }
    free(start_);  // 释放内存
    InfoLog << "~succ free munmap " << size_ << " bytes memory";
    start_ = NULL;    // 清空指针
    ref_counts_ = 0;  // 清空引用计数
}

// 获取起始地址
char* Memory::getStart() { return start_; }

// 获取结束地址
char* Memory::getEnd() { return end_; }

// 获取引用计数
int Memory::getRefCount() { return ref_counts_; }

// 获取空闲块
char* Memory::getBlock() {
    int t = -1;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (size_t i = 0; i < blocks_.size(); i++) {
            if (blocks_[i] == false) {
                blocks_[i] = true;  // 将该空闲块标记为占用
                t = i;
                break;
            }
        }
    }

    if (t == -1) {  // 表示没有找到
        return NULL;
    }
    ref_counts_++;                      // 增加引用计数
    return start_ + (t * block_size_);  // 返回内存块起始地址
}

// 归还空闲块
void Memory::backBlock(char* s) {
    if (s > end_ || s < start_) {  // 检查指针是否在范围内
        ErrorLog << "error, this block is not belong to this Memory";
        return;
    }
    int i = (s - start_) / block_size_;  // 计算块索引
    {
        std::lock_guard<std::mutex> lock(mtx_);
        blocks_[i] = false;  // 该块标记为空闲
    }
    ref_counts_--;  // 引用计数减少
}

// 检查指针是否属于这个Memory对象
bool Memory::hasBlock(char* s) { return ((s >= start_) && (s <= end_)); }

};  // namespace tinyrpc