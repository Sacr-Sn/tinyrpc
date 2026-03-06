#pragma once

#include <vector>

// #include <tinyrpc/coroutine/coroutine.h>
// #include <tinyrpc/coroutine/memory.h>
#include <tinyrpc/coroutine/coroutine.h>
#include <tinyrpc/coroutine/memory.h>

/**
 * 协程池管理器
 * 负责预分配、管理和复用协程对象，以减少协程创建和销毁的开销
 */

namespace tinyrpc {

class CoroutinePool {
   private:
    int pool_size_{0};   // 协程池大小
    int stack_size_{0};  // 每个协程的栈大小

    /**
     * 协程列表，存储协程对象和可用状态
     *  - first : 协程只能指针
     *  - second : 布尔值，false表示可分配，true表示不可分配
     */
    std::vector<std::pair<Coroutine::ptr, bool>> free_cors_;

    std::mutex mtx_;  // 互斥锁

    std::vector<Memory::ptr> memory_pool_;  // 内存池向量，管理多个Memory对象

   public:
    // 初始化协程池，默认栈大小为128KB
    CoroutinePool(int pool_size, int stack_size = 1024 * 128);
    ~CoroutinePool();

    // 获取一个可用的协程实例
    Coroutine::ptr getCoroutineInstance();

    // 归还协程到池中
    void returnCoroutine(Coroutine::ptr cor);
};

// 全局函数，用于获取协程池的单例实例
CoroutinePool* GetCoroutinePool();

}  // namespace tinyrpc