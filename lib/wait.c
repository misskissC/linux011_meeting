/*
 *  linux/lib/wait.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <sys/wait.h>

/* 该宏所定义的函数原型为
 * pid_t waitpid(pid_t pid, int * wait_stat, int options);
 * waitpid系统调用对应的内核函数为 sys_waitpid(...) */
_syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)

pid_t wait(int * wait_stat)
{
    return waitpid(-1,wait_stat,0);
}
