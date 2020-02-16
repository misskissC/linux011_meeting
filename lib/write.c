/*
 *  linux/lib/write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/* 系统调用write,其原型为
 * int write(int fd, const char *buf, off_t count),
 * 系统调用write对应的内核函数为 sys_write(...) */
_syscall3(int,write,int,fd,const char *,buf,off_t,count)
