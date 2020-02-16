/*
 *  linux/lib/execve.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/* 定义以下系统调用接口
 * int execve(const char *file, char **argv, char **envp),
 * execve函数对应system_call.s/_sys_execve程序段。*/
_syscall3(int,execve,const char *,file,char **,argv,char **,envp)
