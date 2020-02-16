/*
 *  linux/lib/close.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/* 定义系统调用接口 int close(int fd)通过int 80h
 * 指令将调用内核函数 fs/open.c/ sys_close 。*/
_syscall1(int,close,int,fd)
