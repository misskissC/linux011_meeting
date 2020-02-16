/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

volatile void do_exit(int error_code);

/* sys_sgetmask,
 * 获取当前进程信号屏蔽码。*/
int sys_sgetmask()
{
    return current->blocked;
}

/* sys_ssetmask,
 * 设置当前进程信号屏蔽码,返回旧信号屏蔽码。*/
int sys_ssetmask(int newmask)
{
    int old=current->blocked;
    /* SIGKILL不可屏蔽 */
    current->blocked = newmask & ~(1<<(SIGKILL-1));
    return old;
}

/* save_old,
 * 将from所指内核内存中(struct sigaction)类型信息拷贝
 * 到to所指用户内存中。*/
static inline void save_old(char * from,char * to)
{
    int i;

    verify_area(to, sizeof(struct sigaction));
    for (i=0 ; i< sizeof(struct sigaction) ; i++) {
        put_fs_byte(*from,to);
        from++;
        to++;
    }
}

/* get_new,
 * 将from所指用户内存段中的(struct sigaction)类型信息
 * 拷贝到to所指向的内核内存段中。*/
static inline void get_new(char * from,char * to)
{
    int i;

    for (i=0 ; i< sizeof(struct sigaction) ; i++)
        *(to++) = get_fs_byte(from++);
}

/* sys_signal,sys_sigaction是用户程序设置信号处理函数的两
 * 种方式,分别对应于OS所提供的signal()和sigaction系统调用。*/

/* sys_signal,
 * 设置当前进程signum信号的处理结构体(含信号处理函数)。*/
int sys_signal(int signum, long handler, long restorer)
{
    struct sigaction tmp;

    if (signum<1 || signum>32 || signum==SIGKILL)
        return -1;
    /* 用户程序信号处理函数 */
    tmp.sa_handler = (void (*)(int)) handler;
    tmp.sa_mask = 0; /* 无屏蔽信号 */
    /* 见宏定义处 */
    tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
    /* 见do_signal */
    tmp.sa_restorer = (void (*)(void)) restorer;
    /* 原信号处理函数地址 */
    handler = (long) current->sigaction[signum-1].sa_handler;
    /* 设置进程signum信号处理函数 */
    current->sigaction[signum-1] = tmp;
    return handler;
}

/* sys_sigaction,
 * 获取signum信号原处理信息,设置signum信号的处理信息。*/
int sys_sigaction(int signum, const struct sigaction * action,
    struct sigaction * oldaction)
{
    struct sigaction tmp;

    if (signum<1 || signum>32 || signum==SIGKILL)
        return -1;

    /* 将action设置到signum信号处理结构体中,并
     * 将signum信号原处理结构体拷贝到出参中。*/
    tmp = current->sigaction[signum-1];
    get_new((char *) action,
        (char *) (signum-1+current->sigaction));
    if (oldaction)
        save_old((char *) &tmp,(char *) oldaction);
    
    /* 若无SA_NOMASK则置屏蔽signum信号位 */
    if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
        current->sigaction[signum-1].sa_mask = 0;
    else
        current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
    return 0;
}

/* do_signal,
 * 信号处理C程序。
 * 
 * 由 ret_from_sys_call 调用(系统调用等结束后将跳转ret_from_sys_call)。
 * ss esp eflags cs eip由用户程序在系统调用时CPU入栈;ds es fs edx ecx
 * ebx eax为系统调用入口处理程序_system_call入栈;signr在ret_from_sys_call
 * 程序段中入栈,为需处理的信号。*/
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
    long fs, long es, long ds,
    long eip, long cs, long eflags,
    unsigned long * esp, long ss)
{
    unsigned long sa_handler;
    long old_eip=eip; /* 系统调用时用户程序中的eip */
    /* 获取当前进程信号处理结构体 */
    struct sigaction * sa = current->sigaction + signr - 1;
    int longs;
    unsigned long * tmp_esp;

    /* 信号处理函数,1-不处理即忽略信号,0-默认方式处理信号。*/
    sa_handler = (unsigned long) sa->sa_handler;
    if (sa_handler==1)
        return;
    if (!sa_handler) {
        if (signr==SIGCHLD)
            return;
        /* 对于非来自子进程的信号则调用do_exit处理该信号 */
        else
            do_exit(1<<(signr-1));
    }
    
    /* 若用户程序曾设置过信号处理函数于sa_handler,
     * 则调用用户设置的信号处理函数处理信号。*/
    if (sa->sa_flags & SA_ONESHOT)
        sa->sa_handler = NULL;
      
    /* 将信号处理函数地址写入eip寄存器所在栈内存中,以在系
     * 统调用返回时(IRET)执行用户程序所指定的信号处理函数。*/
    *(&eip) = sa_handler;

    /* 在用户栈中写入以下值是为了在执行完用户所设置信号处理
     * 函数后执行sa_restorer函数且再执行系统调用后续语句。*/
    longs = (sa->sa_flags & SA_NOMASK)?7:8;
    /* 将用户栈顶向下移以留出longs个元素位置 */
    *(&esp) -= longs;
    /* 写时拷贝用户程序栈顶所对应内存页 */
    verify_area(esp,longs*4);
    tmp_esp=esp;
    /* 将sa_restorer函数及其所需参数压入栈顶 */
    put_fs_long((long) sa->sa_restorer,tmp_esp++);
    put_fs_long(signr,tmp_esp++);
    if (!(sa->sa_flags & SA_NOMASK))
        put_fs_long(current->blocked,tmp_esp++);
    put_fs_long(eax,tmp_esp++);
    put_fs_long(ecx,tmp_esp++);
    put_fs_long(edx,tmp_esp++);
    put_fs_long(eflags,tmp_esp++);
    
    /* 在sa_restorer处理函数中执行return指令时将弹
     * 出old_eip给eip即继续执行系统调用后续语句。*/
    put_fs_long(old_eip,tmp_esp++);

    /* 设定当前进程将被屏蔽的信号 */
    current->blocked |= sa->sa_mask;
    
/* 将内核扩展用户栈顶所压入各参数粗略介绍下。
 * |-----------|
 * |old_eip    |
 * |-----------|
 * |eflags     |
 * |-----------|
 * |edx        |
 * |-----------|
 * |ecx        |
 * |-----------|
 * |eax        |
 * |-----------|
 * |(blocked)  |
 * |-----------|
 * |signr      |
 * |-----------|
 * |sa_restorer|
 * |-----------|
 * 由于在do_signal函数中将eip所在栈内存中的值
 * 更改为信号处理函数地址,  当do_signal返回到
 * ret_from_sys_call 由ret_from_sys_call 执行
 * IRET恢复系统调用现场时将跳转执行信号处理函
 * 数,信号处理函数执行RET指令时会将栈顶的函数
 * 地址sa_restorer弹出到eip寄存器(局部RET),该
 * 函数将会处理并回收栈中signr-eflags参数, 待
 * sa_restorer执行RET后会弹出栈顶old_eip给eip
 * 从而执行用户程序中发生系统调用处的后续语句。*/
}
