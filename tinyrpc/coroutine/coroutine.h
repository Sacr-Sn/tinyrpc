#pragma once

/**
 * 协程的封装
 * 有栈非对称协程
*/

#include <memory>
#include <functional>

#include "./coctx.h"
#include "../comm/run_time.h"

namespace tinyrpc {

/**
 * 三个全局函数用于访问协程相关的全局状态
*/
// 获取当前协程 ID
int getCoroutineIndex();

// 获取当前协程的运行时上下文
RunTime* getCurrentRunTime();

// 设置当前协程的运行时上下文
void setCurrentRunTime(RunTime* v);

// 定义了三个不同的构造函数，用于构造不同的协程
class Coroutine {

private:
    int m_cor_id {0};  // 协程唯一 ID
    coctx m_coctx;  // 协程上下文，保存寄存器状态
    int m_stack_size {0};  // 协程栈的大小和指针
    char* m_stack_sp {NULL};  // 协程栈的指针
    bool m_is_in_cofunc {false};  // 标记是否正在执行协程函数
    std::string m_msg_no;  // 消息编号（已废弃，现在使用m_run_time）
    RunTime m_run_time;  // 运行时上下文，包含消息编号和接口名
    bool m_can_resume {true};  // 标记协程是否可以被恢复
    int m_index {-1};  // 在协程池中的索引

    // 私有默认构造函数 - 用于创建主协程(main coroutine)
    // 主协程在每个线程中自动创建,ID 为 0
    Coroutine();

public:
    typedef std::shared_ptr<Coroutine> ptr;

    // 协程要执行的回调函数,在协程被 Resume 时会执行
    std::function<void()> m_call_back;

    // 带栈空间的构造函数 - 创建普通协程,需要提供栈大小和栈指针
    Coroutine(int size, char* stack_ptr);

    // 带回调函数的构造函数 - 创建协程并设置执行的回调函数
    Coroutine(int size, char* stack_ptr, std::function<void()> cb);

    ~Coroutine();

    // 设置协程的回调函数，这是协程要执行的业务逻辑
    bool setCallBack(std::function<void()> cb);

    // 获取协程 ID
    int getCorId() const {
        return m_cor_id;
    }

    // 设置协程是否正在执行回调函数
    void setIsInCoFunc(const bool v) {
        m_is_in_cofunc = v;
    }

    // 获取协程是否正在执行回调函数
    bool getIsInCoFunc() const {
        return m_is_in_cofunc;
    }

    // 设置消息编号
    void setMsgNo(const std::string& msg_no) {
        m_msg_no = msg_no;
    }

    // 获取消息编号
    std::string getMsgNo() {
        return m_msg_no;
    }

    // 获取协程的运行时上下文
    RunTime* getRunTime() {
        return &m_run_time;
    }

    // 设置在协程池中的索引
    void setIndex(int index) {
        m_index = index;
    }

    // 获取在协程池中的索引
    int getIndex() {
        return m_index;
    }

    // 获取协程栈大小
    int getStackSize() {
        return m_stack_size;
    }

    // 获取协程栈指针
    char* getStackPtr() {
        return m_stack_sp;
    }

    // 设置协程是否可以被恢复
    void setCanResume(bool v) {
        m_can_resume = v;
    }

    /**
     * 以下静态方法是协程调度的核心
    */
    // 让出当前协程，切换回主协程
    static void Yield();

    // 从主协程恢复指定协程的执行
    static void Resume(Coroutine* cor);

    // 获取当前正在执行的协程
    static Coroutine* GetCurrentCoroutine();

    // 获取主协程
    static Coroutine* GetMainCoroutine();

    // 判断当前是否在主协程中
    static bool IsMainCoroutine();
};

}