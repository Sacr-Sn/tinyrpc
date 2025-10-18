#pragma once

/**
 * 协程上下文结构
*/

namespace tinyrpc {

/**
 * 协程切换不过就是寄存器的值变化罢了，
 * 当寄存器的内容改变（如RSP、RBP、RIP等）以后，自然就跳转到新的栈空间上，执行新的代码了
*/
enum {
  kRBP = 6,   // 栈底指针
  kRDI = 7,   // rdi,函数调用时传递的第一个参数
  kRSI = 8,   // rsi,函数调用时传递的第二个参数
  kRETAddr = 9,   // 赋值给rip寄存器,即下一条执行指令的地址（协程切换时，需要恢复的执行地址）
  kRSP = 13,   // 栈顶指针
};

// 协程上下文如结构体coctx，即使用数组 regs 保存主要的寄存器状态
// regs[6] 至 regs[13] 分别保存了关键寄存器的值
// 这些寄存器值帮助保存当前协程的执行状态，以便下次恢复时能够继续执行
struct coctx {
    void* regs[14];  // 寄存器数组
};

// extern "C" 指示编译器使用 C 的方式来处理函数的链接，以便使 C++ 代码能够和 C 语言代码进行正确的链接和交互
extern "C" {
    // 外部声明，表示 coctx_swap 函数是用 汇编语言 实现的
    // 保存当前协程的寄存器状态到第一个 coctx 结构中（coctx *）  当前->第一个
    // 从第二个 coctx 结构中提取寄存器状态，并恢复到当前的寄存器中  第二个->当前
    // asm("coctx_swap") 这一部分表示该函数是通过汇编语言实现的
    extern void coctx_swap(coctx *, coctx *) asm("coctx_swap");
};

}