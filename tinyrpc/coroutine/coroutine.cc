#include <stdlib.h>
#include <iostream>
#include <assert.h>
#include <string.h>
#include <atomic>

#include "coroutine.h"
#include "../comm/log.h"  // log模块还没有实现，因此暂时只引入头文件而不使用log

namespace tinyrpc {

/**
 * 定义了几个关键的thread_local全局变量来管理协程状态
*/
// 每个线程的主协程
static thread_local Coroutine* t_main_coroutine = NULL;

// 当前正在运行的协程
static thread_local Coroutine* t_cur_coroutine = NULL;

// 当前协程的运行时上下文
static thread_local RunTime* t_cur_run_time = NULL;

// 协程总数计数器（原子变量）
static std::atomic_int t_coroutine_count {0};

// 协程 ID 生成器（原子变量）
static std::atomic_int t_cur_coroutine_id {1};


/**
 * 三个全局函数用于访问 thread_local 状态
 * 提供了对当前协程 ID 和运行时上下文的访问接口
*/

int getCoroutineIndex() {
    return t_cur_coroutine_id;
}

RunTime* getCurrentRunTime() {
    return t_cur_run_time;
}

void setCurrentRunTime(RunTime* v) {
    t_cur_run_time = v;
}

/**
 * 协程入口函数，是所有协程的统一入口点
 * 工作流程：
 *  1.设置 m_is_in_cofunc 标志位 true
 *  2.执行协程的回调函数 m_call_back()
 *  3.回调执行完毕后设置标志为 false
 *  4.自动调用 Yield() 切换回主协程
*/
void CoFunction(Coroutine* co) {
    if (co != nullptr) {
        co->setIsInCoFunc(true);

        // 执行回调函数
        co->m_call_back();

        co->setIsInCoFunc(false);
    }

    // 切换回主协程
    Coroutine::Yield();
}

/**
 * 构造函数及核心方法实现
*/

// 构造主协程
Coroutine::Coroutine() {
    m_cor_id = 0;  // 主协程 id 固定为0
    t_coroutine_count++;
    memset(&m_coctx, 0, sizeof(m_coctx));  // 主协程上下文填充0
    t_cur_coroutine = this;  // 主协程自动设置为当前协程
}

// 构造普通协程
Coroutine::Coroutine(int size, char* stack_ptr) : m_stack_size(size), m_stack_sp(stack_ptr) {
    assert(stack_ptr);

    if (!t_main_coroutine) {  // 确保主协程存在，否则构造主协程
        t_main_coroutine = new Coroutine();
    }

    m_cor_id = t_cur_coroutine_id++;  // 设置协程id,协程id唯一且自增
    t_coroutine_count++;  // 协程数量+1
}

// 构造普通协程及其回调函数
Coroutine::Coroutine(int size, char* stack_ptr, std::function<void()> cb)
    : m_stack_size(size), m_stack_sp(stack_ptr) {
    
    assert(m_stack_sp);

    if (!t_main_coroutine) {  // 确保主协程存在，否则创建主协程
        t_main_coroutine = new Coroutine();
    }

    setCallBack(cb);  // 给协程设置回调函数
    m_cor_id = t_cur_coroutine_id++;  // 设置协程id,协程id唯一且自增
    t_coroutine_count++;  // 协程数量+1
}

Coroutine::~Coroutine() {
    t_coroutine_count--;  // 协程数量-1
}

// 设置协程回调函数
bool Coroutine::setCallBack(std::function<void()> cb) {

    if (this == t_main_coroutine) {  // 主协程不设置回调函数
        // TOEDIT log模块还未实现，暂时用命令行输出
        std::cout << "main coroutine can't set callback" << std::endl;
        return false;
    }

    if (m_is_in_cofunc) {  // 正在执行的写成不能设置回调函数
        // TOEDIT log模块还未实现，暂时用命令行输出
        std::cout << "this coroutine is in CoFunction, can't set callback" << std::endl;
        return false;
    }

    m_call_back = cb;  // 设置回调

    // 计算栈顶指针并对齐到16字节边界（x86-64 ABI 要求）
    char* top = m_stack_sp + m_stack_size;
    top = reinterpret_cast<char*>((reinterpret_cast<unsigned long>(top)) & -16LL);

    // 协程上下文填充为0
    memset(&m_coctx, 0, sizeof(m_coctx));

    // 设置寄存器
    m_coctx.regs[kRSP] = top;  // 栈指针
    m_coctx.regs[kRBP] = top;  // 基址指针
    m_coctx.regs[kRETAddr] = reinterpret_cast<char*>(CoFunction);  // 返回地址（协程的入口函数）
    m_coctx.regs[kRDI] = reinterpret_cast<char*>(this);  // 第一个参数

    m_can_resume = true;  // 标记协程为可恢复

    return true;
}

// 获取当前执行的协程
Coroutine* Coroutine::GetCurrentCoroutine() {
    // 如果当前协程为空，则创建并返回主协程
    if (t_cur_coroutine == nullptr) {
        t_main_coroutine = new Coroutine();
        t_cur_coroutine = t_main_coroutine;
    }
    return t_cur_coroutine;
}

// 获取主协程
Coroutine* Coroutine::GetMainCoroutine() {
    if (t_main_coroutine) {
        return t_main_coroutine;
    }
    t_main_coroutine = new Coroutine();
    return t_main_coroutine;
}

/**
 * 判断是否在主协程中
 * 当主协程不存在时,整个协程系统还未初始化,此时的执行环境应该被视为"主协程环境",
 * 不应该进行任何协程切换操作。这样可以确保在协程系统初始化之前,所有的系统调用都直接执行,
 * 不会尝试进行协程切换而导致崩溃。
 * 这是一种防御性编程实践，避免潜在的错误
*/
bool Coroutine::IsMainCoroutine() {
    if (t_main_coroutine == nullptr || t_cur_coroutine == t_main_coroutine) {
        return true;
    }
    return false;
}

/**
 * 以下为协程调度机制
 * 上下文切换机制
    coctx_swap() 是用汇编实现的底层上下文切换函数,定义在 coctx_swap.S 中。它负责:
    保存当前协程的寄存器状态到 m_coctx
    从目标协程的 m_coctx 恢复寄存器状态
    跳转到目标协程的执行位置
*/

/**
 * 从当前协程切换回主协程
 * 执行流程：
 *  1.获取当前协程指针
 *  2.将当前协程设置为主协程
 *  3.清空当前运行时上下文
 *  4.调用 coctx_swap() 进行上下文切换（汇编实现）
 * 该函数在 I/O 操作阻塞时被调用
*/
void Coroutine::Yield() {
    if (t_main_coroutine == nullptr) {  // 主协程为空，无法返回主协程
        // TOEDIT log模块尚未实现，暂时用cout代替
        std::cout << "main coroutine is nullptr" << std::endl;
        return;
    }

    if (t_cur_coroutine == t_main_coroutine) {  // 当前协程就是主协程，不需要让出
        // TOEDIT log模块尚未实现，暂时用cout代替
        std::cout << "current coroutine is main coroutine, no need to yield" << std::endl;
        return;
    }

    Coroutine* co = t_cur_coroutine;  // 当前协程指针
    t_cur_coroutine = t_main_coroutine;
    t_cur_run_time = NULL;
    coctx_swap(&(co->m_coctx), &(t_main_coroutine->m_coctx));
}

/**
 * 恢复协程 -- 从主协程切换到指定协程
 * 执行流程：
 *  1.检查是否在主协程中（只能从主协程Resume，体现了非对称）
 *  2.检查目标协程是否可以被恢复
 *  3.将目标协程设置为当前协程
 *  4.调用 coctx_swap() 设置当前运行上下文，切换到目标协程 
 * 在Reactor事件循环中被调用，用于恢复等待 I/O 事件的协程
*/
void Coroutine::Resume(Coroutine* co) {
    if (!t_main_coroutine) {
        // TOEDIT log模块尚未实现，暂时用cout代替
        std::cout << "swap error, main coroutine is nullter" << std::endl;
    }

    if (t_cur_coroutine != t_main_coroutine) {  // 只能从主协程恢复到普通协程
        // TOEDIT log模块尚未实现，暂时用cout代替
        std::cout << "swap error, current coroutine must be main coroutine" << std::endl;
        return;
    }

    if (!co || !co->m_can_resume) {  // 要恢复的协程不存在或不可恢复
        // TOEDIT log模块尚未实现，暂时用cout代替
        std::cout << "swap error, the coroutine to resume is nullptr or can't resume" << std::endl;
        return;
    }   

    if (t_cur_coroutine == co) {
        // TOEDIT log模块尚未实现，暂时用cout代替
        std::cout << "current coroutine is already the coroutine to resume, no need to resume" << std::endl;
        return;
    }

    t_cur_coroutine = co;
    t_cur_run_time = co->getRunTime();
    // 切换寄存器状态
    coctx_swap(&(t_main_coroutine->m_coctx), &(co->m_coctx));
}

}