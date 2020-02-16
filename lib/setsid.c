/*
 *  linux/lib/setsid.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/* 系统调用setsid原型为
 * pid_t setsid(void);
 * 其对应的内核函数为 sys_setsid() */
_syscall0(pid_t,setsid)
