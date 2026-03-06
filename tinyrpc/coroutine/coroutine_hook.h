#pragma once

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

/**
 * 主要作用：coroutine_hook.h 定义了 TinyRPC 协程系统与系统调用集成的接口。
 * 通过函数指针类型定义、hook 函数声明和 C 接口声明,它建立了一个完整的系统调用拦截机制。
 * 这种设计使得开发者可以用同步的方式编写代码,而底层自动实现了异步非阻塞的 I/O 操作,
 * 大大简化了异步编程的复杂度。配合 coroutine_hook.cc 的实现,
 * 形成了 TinyRPC 高性能协程网络库的核心基础。
 * 
应用代码调用 read()  
  ↓  
extern "C" 中的 read() 被调用  
  ↓  
检查 g_hook 标志  
  ↓  
如果启用 hook,调用 tinyrpc::read_hook()  
  ↓  
read_hook() 尝试非阻塞读取  
  ↓  
如果会阻塞,注册事件并 Yield 协程  
  ↓  
事件就绪后 Resume 协程  
  ↓  
再次调用原始系统调用 g_sys_read_fun() 
*/


/**
 * 定义了5种系统调用的函数指针类型，用于保存原始系统调用的函数指针，
 * 在hook函数中需要调用原始系统调用时使用
*/
// read 系统调用的函数指针类型
typedef ssize_t (*read_fun_ptr_t)(int fd, void *buf, size_t count);

// write 系统调用的函数指针类型
typedef ssize_t (*write_fun_ptr_t)(int fd, const void *buf, size_t count);

// connect 系统调用的函数指针类型
typedef int (*connect_fun_ptr_t)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

// accept 系统调用的函数指针类型
typedef int (*accept_fun_ptr_t)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

// socket 系统调用的函数指针类型
typedef int (*socket_fun_ptr_t)(int domain, int type, int protocol);

// sleep 系统调用的函数指针类型
typedef int (*sleep_fun_ptr_t)(unsigned int seconds);


namespace tinyrpc {

/**
 * 声明了5个hook函数
 * 这些函数是实际的hook实现，它们会在检测到需要阻塞时让出协程，等待事件就绪后恢复执行
 * 所有hook函数都会检查是否在主协程中。如果在主协程中，直接调用原始系统调用，不进行协程切换，因为主协程yield后无法被恢复
*/
// 读取数据的hook函数
ssize_t read_hook(int fd, void *buf, size_t count);

// 写入数据的hook函数
ssize_t write_hook(int fd, const void *buf, size_t count);

// 建立连接的hook函数
int connect_hook(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

// 接受连接的hook函数
int accept_hook(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

// 睡眠的hook函数
unsigned int sleep_hook(unsigned int seconds);

// 用于全局开启或关闭hook机制
void SetHook(bool);

}


/**
 * 声明了标准的系统调用接口
 * 这些是替换标准系统调用接口的接口，
 * 当程序调用read()、write()等函数时，实际上会调用这里声明的函数，而不是真正的系统调用
*/
extern "C" {

    ssize_t read(int fd, void *buf, size_t count);

    ssize_t write(int fd, const void *buf, size_t count);

    int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

    // int accept(int sockfd, struct sockaddr* addr, socklen_t (addrlen));
    int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
    
    unsigned int sleep(unsigned int seconds);
}