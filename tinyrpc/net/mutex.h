#pragma once

#include <pthread.h>
#include <memory>
#include <queue>
#include "../coroutine/coroutine.h"

namespace tinyrpc {

/**
 * 局部锁
*/
template <class T>
struct ScopedLockImpl {

private:
    // 互斥锁
    T &m_mutex;

    // 标识是否已上锁
    bool m_locked;

public:
    /**
     * 加锁与解锁由对象的构造与析构自动管理，不需要手动加锁解锁
    */
    ScopedLockImpl(T &mutex) : m_mutex(mutex) {
        m_mutex.lock();
        m_locked = true;
    }

    ~ScopedLockImpl() {
        unlock();
    }

    // 加锁
    void lock() {
        if (!m_locked) {
            m_mutex.lock();
            m_locked = true;
        }
    }

    // 解锁
    void unlock() {
        if (m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }

};


/**
 * 局部读锁
*/
template <class T>
struct ReadScopedLockImpl {

private:
    // 互斥锁
    T &m_mutex;

    // 标识是否已加锁
    bool m_locked;

public:

    /**
     * 加锁与解锁由对象的构造与析构自动管理，不需要手动加锁解锁
    */
    ReadScopedLockImpl(T &mutex) : m_mutex(mutex) {
        m_mutex.rdlock();
        m_locked = true;
    }

    ~ReadScopedLockImpl() {
        unlock();
    }

    void lock() {
        if (!m_locked) {
            m_mutex.rdlock();
            m_locked = true;
        }
    }

    void unlock() {
        if (m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }

};

/**
 * 局部写锁
*/
template <class T>
struct WriteScopedLockImpl {

private:
    T &m_mutex;
    bool m_locked;

public:
    WriteScopedLockImpl(T &mutex) : m_mutex(mutex) {
        m_mutex.wrlock();
        m_locked = true;
    }

    ~WriteScopedLockImpl() {
        unlock();
    }

    void lock() {
        if (!m_locked) {
            m_mutex.wrlock();
            m_locked = true;
        }
    }

    void unlock() {
        if (m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }

};


class Mutex {

private:
    // 互斥锁变量
    pthread_mutex_t m_mutex;

public:
    // 局部锁
    typedef ScopedLockImpl<Mutex> Lock;

    Mutex() {
        pthread_mutex_init(&m_mutex, nullptr);
    }

    ~Mutex() {
        pthread_mutex_destroy(&m_mutex);
    }

    void lock() {
        pthread_mutex_lock(&m_mutex);
    }

    void unlock() {
        pthread_mutex_unlock(&m_mutex);
    }

    pthread_mutex_t *getMutex() {
        return &m_mutex;
    }

};

// 读写锁
class RWMutex {

private:
    // 读写锁变量
    pthread_rwlock_t m_lock;

public:
    // 局部读锁
    typedef ReadScopedLockImpl<RWMutex> ReadLock;

    // 局部写锁
    typedef WriteScopedLockImpl<RWMutex> WriteLock;

    RWMutex() {
        pthread_rwlock_init(&m_lock, nullptr);
    }

    ~RWMutex() {
        pthread_rwlock_destroy(&m_lock);
    }

    // 加读锁
    void rdlock() {
        pthread_rwlock_rdlock(&m_lock);
    }

    // 加写锁
    void wrlock() {
        pthread_rwlock_wrlock(&m_lock);
    }

    void unlock() {
        pthread_rwlock_unlock(&m_lock);
    }

};


/**
 * 专门为协程设计的互斥锁
 * 与传统的线程互斥锁不同，能够在锁被占用时让出协程而不是阻塞线程
 * 设计目标:
 *  1.协程友好：在等待锁时，yield协程而不是阻塞线程
 *  2.公平调度：使用FIFO队列管理等待的协程
 *  3.与Reactor集成：通过Reactor的任务队列唤醒协程
*/
class CoroutineMutex {

private:
    bool m_lock {false};  // 标识锁是否被占用
    Mutex m_mutex;  // 封装的pthread互斥锁，用于保护内部状态
    std::queue<Coroutine*> m_sleep_cors;  // 等待队列，存储等待获取锁的协程。天生支持FIFO

public:
    typedef ScopedLockImpl<CoroutineMutex> Lock;

    CoroutineMutex();

    ~CoroutineMutex();

    void lock();

    void unlock();

};

}