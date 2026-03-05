#include "thread_pool.h"

namespace tinyrpc {

ThreadPool::ThreadPool(int size) : size_(size) {}

ThreadPool::~ThreadPool() {
    stop();  // 确保析构时清理资源
}

void ThreadPool::start() {
    if (!threads_.empty()) return;  // 防止重复启动

    stop_ = false;
    threads_.reserve(size_);
    for (int i = 0; i < size_; ++i) {
        threads_.emplace_back(&ThreadPool::worker, this);  // 启动工作线程
    }
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_ = true;
    }
    cv_.notify_all();  // 唤醒所有线程，使stop_stop

    // 等待所有线程结束
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
}

void ThreadPool::addTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stop_) return;  // 如果已停止，拒绝新任务
        tasks_.push(std::move(task));
    }
    cv_.notify_one();  // 唤醒一个等待线程
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mtx_);

            // 等待：任务非空 或 线程池停止
            cv_.wait(lock, [this] { return !tasks_.empty() || stop_.load(); });

            // 如果线程池已停止且无任务，退出
            if (stop_ && tasks_.empty()) {
                return;
            }

            // 取出任务
            task = std::move(tasks_.front());
            tasks_.pop();
        }  // 锁在此处释放

        // 在锁外执行任务（关键！避免阻塞其他线程）
        if (task) {
            task();
        }
    }
}

}  // namespace tinyrpc