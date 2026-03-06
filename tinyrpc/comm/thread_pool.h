#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tinyrpc {
/**
 * 通用线程池（C++11+ 标准库实现）
 */
class ThreadPool {
   public:
    explicit ThreadPool(int size);
    ~ThreadPool();

    void start();  // 启动线程池（可选，也可在构造函数中启动）
    void stop();   // 停止线程池（等待所有任务完成）
    void addTask(std::function<void()> task);

   private:
    void worker();  // 工作线程主循环

    int size_;
    std::vector<std::thread> threads_;  // 使用 std::thread
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};  // 原子布尔，线程安全
};
}  // namespace tinyrpc
