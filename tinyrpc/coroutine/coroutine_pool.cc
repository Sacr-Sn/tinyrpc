#include <tinyrpc/comm/config.h>
#include <tinyrpc/comm/log.h>
#include <tinyrpc/coroutine/coroutine_pool.h>

namespace tinyrpc {

// 用于获取配置
extern tinyrpc::Config::ptr gRpcConfig;

// 全局协程池单例实例
static CoroutinePool* t_coroutine_container_ptr = nullptr;

CoroutinePool* GetCoroutinePool() {
    if (!t_coroutine_container_ptr) {
        t_coroutine_container_ptr = new CoroutinePool(gRpcConfig->cor_pool_size_, gRpcConfig->cor_stack_size_);
    }
    return t_coroutine_container_ptr;
}

CoroutinePool::CoroutinePool(int pool_size, int stack_size) : pool_size_(pool_size), stack_size_(stack_size) {
    Coroutine::GetCurrentCoroutine();

    // 创建第一个Memory对象 （有pool_size个内存块，每个块大小为stack_size）
    memory_pool_.push_back(std::make_shared<Memory>(stack_size, pool_size));

    Memory::ptr tmp = memory_pool_[0];

    // 预分配内存块
    for (int i = 0; i < pool_size; i++) {  // 循环创建pool_size个协程
        // 协程构造函数参数：栈大小、栈指针
        Coroutine::ptr cor =
            std::make_shared<Coroutine>(stack_size, tmp->getBlock());  // 每个协程从Memory对象获取一个内存块作为栈
        cor->setIndex(i);                                              // 设置协程的索引（不是id）
        free_cors_.push_back(std::make_pair(cor, false));  // 将协程和false标志加入 free_cors_，表示该协程可分配
    }
}

CoroutinePool::~CoroutinePool() {}

/**
 * 获取协程实例
 * 优先复用已经使用过的协程
 * 已使用的协程栈已经写入了物理内存,而未使用的协程只有虚拟地址。首次写入时会触发页错误(page fault)中断,影响性能。
 */
Coroutine::ptr CoroutinePool::getCoroutineInstance() {
    std::unique_lock<std::mutex> lock(mtx_);
    for (int i = 0; i < pool_size_; i++) {
        // !getIsInCoFunc() - 协程不在执行中
        // !free_cors_[i].second - 标记为分配
        if (!free_cors_[i].first->getIsInCoFunc() && !free_cors_[i].second) {
            free_cors_[i].second = true;  // 标记为不可分配
            Coroutine::ptr cor = free_cors_[i].first;
            lock.unlock();
            return cor;
        }
    }

    // 如果初始池中没有可用协程，从扩展的Memory池获取
    for (size_t i = 1; i < memory_pool_.size(); i++) {
        char* tmp = memory_pool_[i]->getBlock();
        if (tmp) {
            Coroutine::ptr cor = std::make_shared<Coroutine>(stack_size_, tmp);
            return cor;
        }
    }

    // 如果所有Memory对象都用完了，扩展Memory池
    // 创建新的Memory对象，大小为 stack_size_ * pool_size_
    memory_pool_.push_back(std::make_shared<Memory>(stack_size_, pool_size_));
    // 从新创建的Memory对象获取块并创建协程
    return std::make_shared<Coroutine>(stack_size_, memory_pool_[memory_pool_.size() - 1]->getBlock());
}

// 归还协程
void CoroutinePool::returnCoroutine(Coroutine::ptr cor) {
    int i = cor->getIndex();           // 获取协程索引（并非id）
    if (i >= 0 && i < pool_size_) {    // 协程在协程池中
        free_cors_[i].second = false;  // 标记为可分配
    } else {                           // 处理扩展池中的协程
        for (size_t i = 1; i < memory_pool_.size(); i++) {
            // 根据内存块、协程的栈指针来判断是否在某个memory中
            if (memory_pool_[i]->hasBlock(cor->getStackPtr())) {
                // 回收对应的内存块即可，因为扩展池中的协程不回收，由智能指针自动释放
                memory_pool_[i]->backBlock(cor->getStackPtr());
            }
        }
    }
}

}  // namespace tinyrpc