/*
 *  linux/kernel/printk.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * When in kernel-mode, we cannot use printf, as fs is liable to
 * point to 'interesting' things. Make a printf with fs-saving, and
 * all is well.
 */
/* 在内核模式中不能使用printf打印函数,由于fs寄存器可用来指向想要指向
 * 的内存段,所以此文将编写一个printk函数, 该函数除了让fs指向内核数据
 * 段以及所调用显示函数有所不同外,其余与printf并没有什么太大不同。*/
#include <stdarg.h>
#include <stddef.h>

#include <linux/kernel.h>

static char buf[1024];

extern int vsprintf(char * buf, const char * fmt, va_list args);

/* printk,
 * */
int printk(const char *fmt, ...)
{
    va_list args;
    int i;

    va_start(args, fmt);
    i=vsprintf(buf,fmt,args);
    va_end(args);

    /* 用户模式下的printf通过系统调用write将将指定字符串写往标准
     * 输出设备;内核模式中的printk调用tty_write显示buf中的内容。
     * write作为系统调用接口,最终会调用tty_write函数。
     *
     * 除此之外,在调用tty_write时fs寄存器已指向内核数据段,在调用
     * 完成后恢复。*/
    __asm__("push %%fs\n\t"
        "push %%ds\n\t"
        "pop %%fs\n\t"
        /* tty_write参数,
         * channel=0(对应终端),buf=buf,count=i.*/
        "pushl %0\n\t"
        "pushl $_buf\n\t"
        "pushl $0\n\t"
        "call _tty_write\n\t"
        "addl $8,%%esp\n\t"
        "popl %0\n\t"
        "pop %%fs"
        ::"r" (i):"ax","cx","dx");
    return i;
}
