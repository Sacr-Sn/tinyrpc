#ifndef TINYRPC_COROUTINE_COROUTINE_H
#define TINYRPC_COROUTINE_COROUTINE_H

#include <functional>
#include <iostream>
#include <memory>

#include "coctx.h"
#include "run_time.h"

namespace tinyrpc {

int getCoroutineIndex();

RunTime* getCurrentRunTime();

void setCurrentRunTime(RunTime* v);

class Coroutine {
   public:
    typedef std::shared_ptr<Coroutine> ptr;

   private:
    Coroutine();

   public:
    Coroutine(int size, char* stack_ptr);

    Coroutine(int size, char* stack_ptr, std::function<void()> cb);

    ~Coroutine();

    bool setCallBack(std::function<void()> cb);

    int getCorId() const { return cor_id_; }

    void setIsInCoFunc(const bool v) { is_in_cofunc_ = v; }

    bool getIsInCoFunc() const { return is_in_cofunc_; }

    std::string getMsgNo() { return msg_no_; }

    RunTime* getRunTime() { return &run_time_; }

    void setMsgNo(const std::string& msg_no) { msg_no_ = msg_no; }

    void setIndex(int index) { index_ = index; }

    int getIndex() { return index_; }

    char* getStackPtr() { return stack_sp_; }

    int getStackSize() { return stack_size_; }

    void setCanResume(bool v) { can_resume_ = v; }

   public:
    static void Yield();

    static void Resume(Coroutine* cor);

    static Coroutine* GetCurrentCoroutine();

    static Coroutine* GetMainCoroutine();

    static bool IsMainCoroutine();

    // static void SetCoroutineSwapFlag(bool value);

    // static bool GetCoroutineSwapFlag();

   private:
    int cor_id_{0};      // coroutine' id
    coctx coctx_;        // coroutine regs
    int stack_size_{0};  // size of stack memory space
    char* stack_sp_{
        nullptr};  // coroutine's stack memory space, you can malloc or mmap get some memory to init this value
    bool is_in_cofunc_{false};  // true when call CoFunction, false when CoFunction finished
    std::string msg_no_;
    RunTime run_time_;

    bool can_resume_{true};

    int index_{-1};  // index in coroutine pool

   public:
    std::function<void()> m_call_back;
};

}  // namespace tinyrpc

#endif
