/*
 * linux/kernel/math/math_emulate.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This directory should contain the math-emulation code.
 * Currently only results in a signal.
 */
/* 该目录(math)下应该包含模拟数学运算的代码,当前只有产生信号的代码。*/
#include <signal.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

/* math_emulate,
 * 数学协处理器模拟器程序。
 * 
 * 当协处理器不存在触发IDT[7]中断时由中断入口
 * 处理程序_device_not_available调用该函数。*/
void math_emulate(long edi, long esi, long ebp, long sys_call_ret,
    long eax,long ebx,long ecx,long edx,
    unsigned short fs,unsigned short es,unsigned short ds,
    unsigned long eip,unsigned short cs,unsigned long eflags,
    unsigned short ss, unsigned long esp)
{
    unsigned char first, second;

/* 0x0007 means user code space */
    /* 结合段选择符的位格式判断该语句含义
     * |15              3| 2|  0|
     * --------------------------
     * |      index      |TI|RPL|
     * --------------------------
     * 0xf表示选择用户进程ldt[1]即用户进程代码段
     * 若cs不为用户进程代码段选择符则表明cs指向内核代码段,
     * 如此则进行错误提示并死机。*/
    if (cs != 0x000F) {
        printk("math_emulate: %04x:%08x\n\r",cs,eip);
        panic("Math emulation needed in kernel");
    }

    /* 取用户进程触发IDT[7]中断的机器指令以提示触发该中
     * 断的指令,然后为当前进程设置协处理器出错的信号。*/
    first = get_fs_byte((char *)((*&eip)++));
    second = get_fs_byte((char *)((*&eip)++));
    printk("%04x:%08x %02x %02x\n\r",cs,eip-2,first,second);
    current->signal |= 1<<(SIGFPE-1);
}

/* math_error,
 * 协处理器出错中断C处理函数。
 * 该函数由_coprocessor_error调用。*/
void math_error(void)
{
    /* 清协处理器出错标志状态 */
    __asm__("fnclex");

    /* 向正使用协处理器的进程发送协处理器出错信号 */
    if (last_task_used_math)
        last_task_used_math->signal |= 1<<(SIGFPE-1);
}
