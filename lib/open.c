/*
 *  linux/lib/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <stdarg.h>

/* open,
 * 以flag标识属性打开文件filename,若文件filename
 * 不存在时则以第三个参数所指标识属性创建文件。
 * 
 * 为什么定义成可变参函数而不定义成含三个参数的函数。*/
int open(const char * filename, int flag, ...)
{
    register int res;
    va_list arg;

    va_start(arg,flag);
    /* 内联函数输入。
     * "0" (__NR_open), eax=__NR_open;
     * "b" (filename), ebx=filename;
     * "c" (flag), ecx=flag;
     * "d" (va_arg(arg,int)), edx=open第三个整型参数。
     *
     * 内联函数输出。
     * res=eax。
     *
     * int 80h执行后CPU将调用fs/open.c/sys_open系统调用,
     * 该函数以flag所标识属性将打开或创建名为filename的
     * 文件(若filename不存在被创建则将文件属性设置为open
     * 第三个参数所标识属性)。*/
    __asm__("int $0x80"
        :"=a" (res)
        :"0" (__NR_open),"b" (filename),"c" (flag),
        "d" (va_arg(arg,int)));

    /* 漏了以下语句? va_end(arg); */

    if (res>=0)
        return res;
    errno = -res;
    return -1;
}
