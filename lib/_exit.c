/*
 *  linux/lib/_exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

/* _exit,
 * 用于退出当前进程。
 *
 * volatile用于修饰函数时告知编译器该函数不会
 * 返回,可以省略_exit函数返回相关的栈帧信息。
 *
 * volatile用于修饰内联汇编时则告知编译器不要优化内联汇编代码,如见inb_p。*/
volatile void _exit(int exit_code)
{
   /* eax=__NR_exit, ebx=exit_code; 
    * 即调用系统调用号为__NR_exit的系统调用,ebx携带了参数exi_code,
    * 其内核函数对应为 fs/sys_exit()。*/
    __asm__("int $0x80"::"a" (__NR_exit),"b" (exit_code));
}
