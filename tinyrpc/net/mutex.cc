// TOIMPL

#include "mutex.h"
#include "reactor.h"
#include "../comm/log.h"
#include "../coroutine/coroutine.h"


namespace tinyrpc {

CoroutineMutex::CoroutineMutex() {}

CoroutineMutex::~CoroutineMutex() {
    if (m_lock) {  // 表示锁被占用  
        unlock();
    }
}

void CoroutineMutex::lock() {
    if (Coroutine::IsMainCoroutine()) {
        // TOEDIT
        std::cerr << "main coroutine can't user coroutine mutex" << std::endl;
        return;
    }

    Coroutine* cor = Coroutine::GetCurrentCoroutine();  // 当前协程

    Mutex::Lock lock(m_mutex);
    if (!m_lock) {  // 锁没被lock
        m_lock = true;
        // TOEDIT
        std::cout << "[CoroutineMutex::lock] : coroutine succ get coroutine mutex" << std::endl;
        lock.unlock();
    } else {  // 锁已经被lock
        m_sleep_cors.push(cor);  // 当前协程加入等待队列
        auto size = m_sleep_cors.size();
        lock.unlock();
        // TOEDIT
        std::cout << "[CoroutineMutex::lock] : coroutine yield, pending coroutine mutex, current sleep queue exist ["
        << size << "] coroutines";
        
        Coroutine::Yield();  // 当前协程让出
    }
}

void CoroutineMutex::unlock() {
    if (Coroutine::IsMainCoroutine()) {
        // TOEDIT
        std::cerr << "[CoroutineMutex::unlock] : main coroutine can't use coroutine mutex" << std::endl;
        return;
    }

    Mutex::Lock lock(m_mutex);
    if (m_lock) {
        m_lock = false;
        if (m_sleep_cors.empty()) {
            return;
        }

        Coroutine* cor = m_sleep_cors.front();  // 唤醒一个协程
        m_sleep_cors.pop();
        lock.unlock();

        if (cor) {
            // TOEDIT
            std::cout << "[CoroutineMutex::unlock] : coroutine unlock, now resume coroutine[" << cor->getCorId() << "]" << std::endl;

            tinyrpc::Reactor::GetReactor()->addTask([cor]() {
                tinyrpc::Coroutine::Resume(cor);
            }, true);
        }
    }
}

}