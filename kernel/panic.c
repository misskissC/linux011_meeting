/*
 *  linux/kernel/panic.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (includeinh mm and fs)
 * to indicate a major problem.
 */
/* panic()用于提示不可恢复的错误,其贯穿整个内核(包括内存管理模块mm和文件管理模块fs)。*/
#include <linux/kernel.h>
#include <linux/sched.h>

/* sys_sync函数原型为 int sys_sync(void),
 * 由于这里不需要使用其返回值,所以将其声明为无返回值类型。*/
void sys_sync(void);    /* it's really int */

/* panic,
 * 往终端输出s所指的错误提示,同步当前进程资源如文件
 * 系统i节点,缓冲区块中的内容到外设,然后进入死循环。*/
volatile void panic(const char * s)
{
    printk("Kernel panic: %s\n\r",s);
    if (current == task[0])
        printk("In swapper task - not syncing\n\r");
    else
        sys_sync();
    for(;;);
}
