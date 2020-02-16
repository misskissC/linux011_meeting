/*
 *  linux/lib/dup.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/* 系统调用dup原型
 * int dup(int fd),
 * 其对应内核函数为 sys_dup() */
_syscall1(int,dup,int,fd)
