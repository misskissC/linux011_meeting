/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
/* sched.c 是主要的内核文件。其包含了任务调度的基础函数(sleep_on,wake_up,schedule等)
 * 和一些简单的系统调用函数(如 getpid()),这些系统调用差不多仅从管理进程结构体中获取某
 * 成员状态并返回。*/

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

/* _S(nr),将nr转换为位号;
 * _BLOCKABLE,将SIGKILL和SIGSTOP位号置为0。*/
#define _S(nr) (1<<((nr)-1))
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

/* show_task,
 * 显式任务号为nr进程相关信息。*/
void show_task(int nr,struct task_struct * p)
{
    int i,j = 4096-sizeof(struct task_struct);

    /* 打印任务号, 进程id, 进程当前状态 */
    printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state);

    /* (p + 1)使得p指向进程内核栈底,由此计数并打印内核栈空闲字节数。*/
    i=0;
    while (i<j && !((char *)(p+1))[i])
        i++;
    printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}

/* show_stat,
 * 打印当前所有进程的运行状态,内核栈空闲字节数。*/
void show_stat(void)
{
    int i;

    for (i=0;i<NR_TASKS;i++)
        if (task[i])
            show_task(i,task[i]);
}

/* 1193180Hz为定时器工作频率 */
#define LATCH (1193180/HZ)

extern void mem_use(void);

/* 以int (func)(void)函数类型声明以下两
 * 个标识符, 二者在system_call.s中定义。*/
extern int timer_interrupt(void);
extern int system_call(void);

/* union task_union,
 * 任务(进程)管理联合体。
 * 
 * stack[PAGE_SIZE]使得union task_union联合体占一页内存,
 * 低地址内存段用于进程管理结构体struct task_struct,高地
 * 址内存段用作进程进入内核调用内核函数时的栈内存。 回忆
 * 下函数局部变量在栈中的存储及函数调用栈帧吧,这可能对理
 * 解sleep_on和wake_up函数对有帮助哦。*/
union task_union {
    struct task_struct task;
    char stack[PAGE_SIZE];
};


/* init_task
 * 用INIT_TASK初始化 管理初始进程的结构体后,各成员初始状态为
 * union task_union init_task = {
 *     .state      = 0,       // 置进程为可运行状态
 *     .counter    = 15,      // 进程运行时间片为15(150ms)
 *     .priority   = 15,      // 进程(在时间片上的)优先级为15
 *     .signal     = 0,       // 进程当前无任何需处理的信号
 *     .sigaction  = {{},},   // 处理信号的回调函数为NULL
 *     .blocked    = 0,       // 无屏蔽信号位
 *     
 *     .exit_code  = 0,       // 进程退出码
 *     .start_code = 0,       // 进程代码段(和数据段)起始地址
 *     .end_code   = 0,       // 进程代码段大小
 *     .end_data   = 0,       // 进程代码段+数据段大小
 *     .brk        = 0,       // 进程总大小
 *     .start_stack= 0,       // 进程栈顶地址
 *     .pid        = 0,       // 进程id
 *     .father     = -1,      // 父进程id(无父进程)
 *     .pgrp       = 0,       // 进程组id
 *     .session    = 0,       // 会话id
 *     .leader     = 0,       // 会话组领导进程id(自己)
 *     .uid        = 0,       // 用户id
 *     .euid       = 0,       // 用户有效id
 *     .suid       = 0,       // ========
 *     .gid        = 0,       // 进程组id
 *     .egid       = 0,       // 进程有效组id
 *     .sgid       = 0,       // ======
 *     .alarm      = 0,       // 超时报警时间片
 *     .utime      = 0,       // 进程用户态运行时间片
 *     .stime      = 0,       // 进程内核态运行时间片
 *     .cutime     = 0,       // 子进程用户态运行时间片
 *     .cstime     = 0,       // 子进程内核态运行时间片
 *     .start_time = 0,       // 进程开始运行时间片
 *     .used_math  = 0,       // 刚没有使用协处理器
 *     
 *     .tty        = -1,      // 无关联的控制终端
 *     .umask      = 0022,    // 文件创建属性屏蔽位(8进制)
 *     .pwd        = NULL,    // 当前目录i节点
 *     .root       = NULL,    // 根目录i节点
 *     .executable = NULL,    // 进程可执行文件i节点
 *     .close_on_exec = 0,    // 执行时关闭文件位标志
 *     .filp       = {NULL,}, // 进程无任何打开文件
 *     
 *     .ldt = {    // 初始进程LDT,特权级DPL=3(用户程序)
 *         {0,0},  // LDT[0]置0保留
 *         {0x9f, 0xc0fa00}, // LDT[1]描述[0x0, 0x9ffff]为初始进程代码段
 *         {0x9f, 0xc0f200}, // LDT[2]描述[0x0, 0x9ffff]为初始进程数据段
 *     },
 *     .tss = { // 指定初始进程的TSS
 *         0,   // 前一个进程的TSS选择符
 *         
 *         // 进程TSS的ss0:esp0成员维护为内核态栈空间,
 *         // 即进程内核态栈顶为任务管理结构体所在内存页末端
 *         PAGE_SIZE+(long)&init_task,
 *         0x10,
 *         
 *         // ss1:esp1=ss2:esp2=0
 *         0,0,0,0,
 *         
 *         (long)&pg_dir, // 页目录基址加载给进程cr3字段
 *         
 *         // eip=eflags=eax=ecx=edx=ebx=esp=ebp=esi=edi=0
 *         0,0,0,0,0,0,0,0,0,0,
 *         
 *         // es=cs=ss=ds=fs=gs=0x17
 *         0x17,0x17,0x17,0x17,0x17,0x17,
 *         
 *         // 初始任务0的LDT选择符。
 *         // 初始任务的TSS和LDT在sched_init中被设置在GDT中。
 *         LDT(0),
 *
 *         0x80000000, // I/O位图偏移地址
 *         {},
 *     }
 * }
 * linux0.11从内核态切换到用户态时会执行init_task所描述和管理的初始进程。
 * CPU --> TR --> GDT[TR] --> TSS;
 * TSS.ldt --> GDT[TSS.ldt] --> LDT */
static union task_union init_task = {INIT_TASK,};

/* jiffies用于记录系统开机所运行的时间片。jiffies在定时器中断处理函数
 * 中会被增1,定时器中断约每10ms发生一次,即jiffies * 10ms即为开机时常。*/
long volatile jiffies=0;

/* 用于记录系统开机时间(见main.c/time_init) */
long startup_time=0;

/* current始终指向 管理当前运行进程的结构体,初始任务最先被运行 */
struct task_struct *current = &(init_task.task);
/* 指向刚使用协处理器的进程 */
struct task_struct *last_task_used_math = NULL;

/* 指向进程管理结构体的全局指针数组。
 * task[0] = &init_task.task即指向管理初始进程的结构体。
 * linux0.11最多能支持64个进程的管理(NR_TASKS=64)。
 * task数组的下标充当了任务号,比如初始任务的任务号为0,依次类推。*/
struct task_struct * task[NR_TASKS] = {&(init_task.task), };

/* head.s中层用stack_start初始化ss:esp以将user_stack内存段作为内存栈内存 */
long user_stack [ PAGE_SIZE>>2 ] ;
struct {
    long * a;
    short b;
} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */
/* math_state_restore,
 * 备份上一个进程在协处理器上的运
 * 行状态,以让当前进程使用协处理器。*/
void math_state_restore()
{
    if (last_task_used_math == current)
        return;
    /* 在向协处理器发送命令前需先发送fwait命令 */
    __asm__("fwait");
    /* 在当前进程使用协处理器之前,先为上一个进程备份其协处理器状态 */
    if (last_task_used_math) {
        __asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
    }

    /* 若当前进程使用过协处理器,则将当前进程在协处理器上
     * 的运行状态写入协处理器,若当前进程首次使用协处理器
     * 则初始设置协处理器并标识当前进程使用协处理器标志。*/
    last_task_used_math=current;
    if (current->used_math) {
        __asm__("frstor %0"::"m" (current->tss.i387));
    } else {
        __asm__("fninit"::);
        current->used_math=1;
    }
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
/* schedule,
 * 任务(进程)调度函数。*/
void schedule(void)
{
    int i,next,c;
    struct task_struct ** p;

/* check alarm, wake up any interruptible tasks that have got a signal */
/* 遍历管理进程的结构体, 检查为各进程所设置的报警是否超时, 若超时则为当
 * 前进程设置报警超时信号。若当前进程被设置了不可屏蔽信号或未被屏蔽信号
 * 且当前进程的状态为准备就绪则为当前进程设置可运行状态, 让其加入可被调
 * 度进程的行列中。
 *
 * jiffies在系统开机时为0,在定时器中断处理函数中每约10ms自增1。在为进程
 * 设置报警超时值alarm时是基于jiffies来设置的, 比如为当前进程设置30ms后
 * 报警超时,alarm=jiffies + 3。所以当alarm < jiffes时表示报警已超时。
 *
 * 为进程设置的超时信号将在系统调用完成后被处理(见ret_from_sys_call)。
 *
 * 在进程信号中,有两个信号不可被blocked屏蔽(SIGKILL和SIGSTOP),所以此处
 * 做了判断: 若当前进程需要处理不可屏蔽信号和需要处理未被blocked屏蔽信
 * 号时则将处于就绪状态的进程置于可运行状态,好让该进程在后续处理信号。*/
    for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
        if (*p) {
            if ((*p)->alarm && (*p)->alarm < jiffies) {
                (*p)->signal |= (1<<(SIGALRM-1));
                (*p)->alarm = 0;
            }
            if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
                (*p)->state==TASK_INTERRUPTIBLE)
                (*p)->state=TASK_RUNNING;
        }

/* this is the scheduler proper: */
/* 调度处于运行状态的进程。遍历处于运行状态的进程,从当前进程切换到时间片
 * 最大的进程。若当前除初始进程外无其他进程运行,则next将保持原本指向初始
 * 进程索引0。若除初始进程外的其余就绪进程的时间片皆为0,则用进程的优先级
 * 设置进程的时间片,然后切换到首次遍历到的时间片为0的进程中运行,该运行进
 * 程的时间片数等于其优先级数。*/
    while (1) {
        c = -1;
        next = 0;
        i = NR_TASKS;
        p = &task[NR_TASKS];
        while (--i) {
            if (!*--p)
                continue;
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
                c = (*p)->counter, next = i;
        }
        if (c) break;
        for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
            if (*p)
                (*p)->counter = ((*p)->counter >> 1) + (*p)->priority;
    }
    switch_to(next); /* 从当前进程切换到时间片最大的进程中运行 */
}

/* sys_pause,
 * 将当前进程置于准备就绪状态,
 * 调用进程调度函数调度时间片最大的进程运行。*/
int sys_pause(void)
{
    current->state = TASK_INTERRUPTIBLE;
    schedule();
    return 0;
}

/* sleep_on,
 * 让当前进程进入睡眠,*p最初指向NULL。
 *
 * 将当前进程结构体地址赋给p,然后将当前进
 * 程设置为未就绪状态。直到其他进程调用函
 * 数wake_up(p)重新将当前进程设置就绪状态。*/
void sleep_on(struct task_struct **p)
{
    struct task_struct *tmp;

    if (!p)
        return;
    if (current == &(init_task.task))
        panic("task[0] trying to sleep");

    /* 用tmp指向实参p指向的进程结构体,
     * 并让实参p指向当前进程结构体。*/
    tmp = *p;
    *p = current;

    /* 将当前进程设置为未就绪状态然后调度
     * 时间片最大的其他进程运行。*/
    current->state = TASK_UNINTERRUPTIBLE;
    schedule();

    /* 调用schedule()函数切换其他进程运行后,
     * 本进程不会再被CPU运行。本进程运行上下
     * 文将被备份到其TSS中, 包括本函数栈帧状
     * 态。在本进程被其他进程调用wake_up设置
     * 为就绪状态且被函数 schedule()切换运行
     * 后,本进程才会被重新执行即从 switch_to
     * 中ljmp之后的指令开始执行, 然后从函数
     * schedule()函数返回到此处从而执行判断
     * 语句if (tmp)。
     *
     * tmp->state=0即将tmp所指向进程即上一个
     * 调用sleep_on()的进程设置为就绪状态。*/
    if (tmp)
        tmp->state=0;
}

/* interruptible_sleep_on,
 * 让当前进程进入睡眠。
 * 与sleep_on不同的是,经本函数进入睡眠进程的状态为准备就绪
 * 状态,该状态除可由wake_up唤醒后,还可由信号唤醒即置可运行
 * 状态。如1->2->3->4四个进程依次调用本函数进入睡眠,假设进
 * 程2收到信号,则进程被置就绪状态的顺序为4->3->2->1。*/
void interruptible_sleep_on(struct task_struct **p)
{
    struct task_struct *tmp;

    if (!p)
        return;
    if (current == &(init_task.task))
        panic("task[0] trying to sleep");

    /* 用tmp指向实参p指向的进程结构体,
     * 并让实参p指向当前进程结构体。*/
    tmp=*p;
    *p=current;

    /* 将当前进程设置为准备就绪状态然后调度
     * 时间片最大的其他进程运行。准备就绪状
     * 态除了能被其他进程调用wake_up唤醒外,
     * 其他进程通过为本进程发送信号的方式将
     * 本进程再次设置为就绪状态,见schdule()。*/
repeat:	current->state = TASK_INTERRUPTIBLE;
    schedule();

    /* 当本进程被唤醒且再次切换到本进程
     * 中时, 后续语句才会被执行。因为调
     * 用本函数进入睡眠的进程可被信号唤
     * 醒(见schedule)。所以被唤醒进程有
     * 可能不是最后一次调用本函数进入睡
     * 眠的进程, 所以在此处将最后调用本
     * 函数的进程唤醒并再次将本进程置为
     * 准备就绪状态后进行进程调度。
     *
     * struct task_struct *tmp = NULL;
     * 假设进程1,2,3,4通过实参tmp 依次
     * 调用了本函数 1 -> 2 -> 3 -> 4即
     * interruptible_sleep_on(&tmp);
     * 
     * 若通过wake_up(&tmp)陆续唤醒4个进
     * 程, 该过程同wake_up 唤醒通过调用
     * sleep_on进入睡眠的进程一样。
     *
     * 看看通过信号如何唤醒这4个进程的,
     * 假设进程2 收到某信号被唤醒后执行
     * 到此处, 此时*p即tmp指向进程4结构
     * 体,此处则将最后调用本函数即进程4
     * 置为就绪状态并重新进入睡眠。待进
     * 程被运行后, 进程3,2,1 会被依次置
     * 就绪状态(同调用wake_up唤醒)。*/
    if (*p && *p != current) {
        (**p).state=0;
        goto repeat;
    }

    /* 最后调用本函数的进程被唤醒后, 将唤
     * 醒上一个调用本函数的进程, 依次类推
     * 直到tmp = NULL即第一个调用本函数的
     * 进程为止。*/
    *p=NULL; /* 复位实参在本进程中所指向的上一个进入睡眠的进程 */
    if (tmp)
        tmp->state=0;
}

/* wake_up,
 * 唤醒由参数*p指向结构体所管理进程,即
 * 为该进程设置为已就绪状态。由wake_up
 * 唤醒的进程为最后调用sleep_on(p)的进
 * 程,该进程将会在sleep_on()函数中唤醒
 * 在他之前调用sleep_on的进程,依次类推
 * 直到唤醒所有调用过sleep_on(p)的进程。*/
void wake_up(struct task_struct **p)
{
    if (p && *p) {
        (**p).state=0;
        /* 复位实参在本进程中所指向的上一个进入睡眠的进程 */
        *p=NULL;
    }
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
/* 不懂点软盘相关专业知识,这些程序读起来还真是一头雾水,携带一下参考下书籍吧。*/

/* 等待软驱A-D马达启动并到达正常转速的进程指针数组 */
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
/* 存储A-D软驱启动所需时间片数的数组 */
static int  mon_timer[4]={0,0,0,0};
/* 存储A-D软驱停止所需时间片数的数组 */
static int moff_timer[4]={0,0,0,0};
/* A-D软驱控制器数字输出寄存器值,
 * bit[7..4]: 标识D-A马达是否启动,1-启动;0-关闭。
 * bit[3]: 1/0 - 允许DMA和中断请求/禁止DMA和中断请求。
 * bit[2]: 1/0 - 启动/复位软盘控制器。
 * bit[1..0]: 标识当前选择的软驱,[(00)2..(11)2]对应A-D。*/
unsigned char current_DOR = 0x0C;

/* ticks_to_floppy_on,
 * 指定软驱nr(0-3对应A-D)启动和停止所需等待时间。*/
int ticks_to_floppy_on(unsigned int nr)
{
    extern unsigned char selected;
    unsigned char mask = 0x10 << nr;

    if (nr>3)
        panic("floppy_on: nr>3");
    
    moff_timer[nr]=10000;   /* 100 s = very big :-) */
    cli();  /* use floppy_off to turn it off */
    mask |= current_DOR;
    if (!selected) {
        mask &= 0xFC;
        mask |= nr;
    }
    /* 若软盘输出寄存器当前值与要求值不同,
     * 若软驱没有启动则置0.5s的等待启动时
     * 间,若软驱已启动则再置20ms等待时间。*/
    if (mask != current_DOR) {
        outb(mask,FD_DOR);
        if ((mask ^ current_DOR) & 0xf0)
            mon_timer[nr] = HZ/2;
        else if (mon_timer[nr] < 2)
            mon_timer[nr] = 2;
        current_DOR = mask;
    }
    sti();
    /* 软盘定时值在do_floppy_timer中会被递减 */
    return mon_timer[nr];
}

/* floppy_on,
 * 等待软盘启动。*/
void floppy_on(unsigned int nr)
{
    cli();
    /* 当等待软盘启动时间未完毕时,
     * 则等待相应软驱的进程睡眠等待。*/
    while (ticks_to_floppy_on(nr))
        sleep_on(nr+wait_motor);
    sti();
}

/* floppy_off,
 * 设置停止软盘马达的等待时间。*/
void floppy_off(unsigned int nr)
{
    moff_timer[nr]=3*HZ;
}

/* do_floppy_timer,
 * 软盘定时器超时C处理函数,该函数被
 * do_timer函数调用,即约每10ms就会被调用一次。*/
void do_floppy_timer(void)
{
    int i;
    unsigned char mask = 0x10;

    for (i=0 ; i<4 ; i++,mask <<= 1) {
        if (!(mask & current_DOR))
            continue;
        /* 若等待马达启动定时到达则唤醒等待马达启动的进程 */
        if (mon_timer[i]) {
            if (!--mon_timer[i])
                wake_up(i+wait_motor);
        /* 若等待软盘马达停止超时则复位马达启动
         * 位并更新记录软盘数字输出寄存器的变量 */
        } else if (!moff_timer[i]) {
            current_DOR &= ~mask;
            outb(current_DOR,FD_DOR);
        } else
            moff_timer[i]--; /* 等待软盘马达停止时间片递减 */
    }
}

#define TIME_REQUESTS 64
/* static struct timer_list,
 * 定时器结构体。*/
static struct timer_list {
    long jiffies; /* 定时器时间片 */
    void (*fn)(); /* 定时器超时回调函数 */
    struct timer_list * next; /* 指向timer_list数组中的上一元素 */
} timer_list[TIME_REQUESTS], * next_timer = NULL;
 
/* add_timer,
 * 往定时器数组timer_list中加入超时时间片为jiffies
 * 超时回调函数为fn的定时器。
 *
 * 如往timer_list数组中依次加入3个时间片分别为
 * 10,3,33的定时器的过程
 * |——————————|
 * |jiffies=10|
 * |fn10      |
 * |next=NULL |
 * |——————————|
 * timer_list[0]
 * next_timer = &timer_list[0]
 * 
 * 添加3个时间片超时的定时器
 * |——————————|  |———————————————————|
 * |jiffies=10|  |jiffies=3          |
 * |fn10      |  |fn3                |
 * |next=NULL |  |next=&timer_list[0]|
 * |——————————|  |———————————————————|
 * timer_list[0] timer_list[1]        
 *               next_timer = &timer_list[1]
 * add_timer在处理加入时间片较小定时器时似乎是
 * 有问题的, 应该将timer_list[0]设置为定时器标
 * 兵,该定时器标兵时间片保持为0.
 * 
 * 添加33个时间片超时的定时器
 * |——————————|  |——————————|  |———————————————————|
 * |jiffies=20|  |jiffies=10|  |jiffies=3          |
 * |fn33      |  |fn10      |  |fn10               |
 * |next=NULL |  |next=NULL |  |next=&timer_list[1]|
 * |——————————|  |——————————|  |———————————————————|
 * timer_list[0] timer_list[1] timer_list[2]        
 *                             next_timer = &timer_list[2] */
void add_timer(long jiffies, void (*fn)(void))
{
    struct timer_list * p;

    if (!fn)
        return;
    
    cli(); /* 禁止中断 */
    /* 若时间片小于等于0则理解执行定时器的超时回调函数 */
    if (jiffies <= 0)
        (fn)();
    /* 否则将定时器加入到定时器数组timer_list中的合理位置上 */
    else {
        /* 遍历一个空闲的timer_list元素,将该定时器记录在该空闲元素中 */
        for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
            if (!p->fn)
                break;
        if (p >= timer_list + TIME_REQUESTS)
            panic("No more time requests free");
        p->fn = fn;
        p->jiffies = jiffies;
        p->next = next_timer; /* 指向链表头 */
        next_timer = p;
        
        /* 调整新加入定时器在timer_list中的位置,将
         * timer_list[0]设置为标兵后以下表达式中的"<"应为"<=" */
        while (p->next && p->next->jiffies < p->jiffies) {

            /* 将插入定时器时间片减去当前定时器时间片
             * 并交换两个定时器, 直到插入定时器时间片
             * 比下一个定时器时间片小为止。*/
            p->jiffies -= p->next->jiffies;
            fn = p->fn;
            p->fn = p->next->fn;
            p->next->fn = fn;
            jiffies = p->jiffies;
            p->jiffies = p->next->jiffies;
            p->next->jiffies = jiffies;

            p = p->next;
        }
    }
    sti();
}

/* do_timer,
 * 定时中断C处理函数。由定时器中断入口处理函
 * 数_timer_interrupt调用,即约每10ms调用一次。*/
void do_timer(long cpl)
{
    extern int beepcount;
    extern void sysbeepstop(void);

    /* 当beepcount计数为0时关闭扬声器 */
    if (beepcount)
        if (!--beepcount)
            sysbeepstop();

    /* 更新当前进程在用户态和内核态运行时间片数 */
    if (cpl)
        current->utime++;
    else
        current->stime++;

    if (next_timer) {
        next_timer->jiffies--;
        /* 调用时间片为已0的定时器的回调函数, 并将当前定
         * 时器的回调函数指针清空,以表该数组元素空闲状态。*/
        while (next_timer && next_timer->jiffies <= 0) {
            void (*fn)(void);
            /* next_timer指向下一个时间片最小的定时器 */
            fn = next_timer->fn;
            next_timer->fn = NULL;
            next_timer = next_timer->next;
            (fn)();
        }
    }
    /* 软盘马达启动位置位则执行软盘定时程序 */
    if (current_DOR & 0xf0)
        do_floppy_timer();

    /* 递减当前进程运行时间片,若时间片未完则不进行进程调度 */
    if ((--current->counter)>0) return;

    /* 若当前进程时间片运行完毕且不是在
     * 内核态下则调度时间片最大的进程运行 */
    current->counter=0;
    if (!cpl) return;
    schedule();
/* 调用schedule()切换到其他进程中运行后,本进程
 * 将阻塞在switch_to()中ljmp之后的一条语句处。
 * 直到所有进程时间片都运行完毕, 各进程在调度函
 * 数schedule()中重新根据各自优先级获得时间片后,
 * 且本进程再被调度运行时才会从阻塞处返回到此处,
 * 从而才结束本进程的定时器中断, 并依次返回到应
 * 用程序发生中断处继续运行。*/
}

/* sys_*, 进程系统调用系列 */

/* sys_alarm,
 * 设置当前进程seconds后报警。*/
int sys_alarm(long seconds)
{
    int old = current->alarm;

    /* 计算原超时值还有多少秒 */
    if (old)
        old = (old - jiffies) / HZ;
    
    /* 将seconds换算成毫秒后基于当前系统时间片将seconds赋值
     * alarm以实现当前进程在seconds后超时报警(见schedule())。*/
    current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
    return (old);
}

/* sys_getpid,
 * 获取当前进程id。*/
int sys_getpid(void)
{
    return current->pid;
}

/* sys_getppid,
 * 获取当前进程父进程id。*/
int sys_getppid(void)
{
    return current->father;
}

/* sys_getuid,
 * 获取当前进程用户id。*/
int sys_getuid(void)
{
    return current->uid;
}

/* sys_geteuid,
 * 获取当前进程有效用户id。*/
int sys_geteuid(void)
{
    return current->euid;
}

/* sys_getgid,
 * 获取当前进程组id。*/
int sys_getgid(void)
{
    return current->gid;
}

/* sys_getegid,
 * 获取当前进程有效组id */
int sys_getegid(void)
{
    return current->egid;
}

/* sys_nice,
 * 将当前进程优先级降低increment。*/
int sys_nice(long increment)
{
    if (current->priority-increment>0)
        current->priority -= increment;
    return 0;
}

/* sched_init,
 * 任务调度初始化。
 * 
 * 开启定时器中断并设置用于进程调度定时器的中断处理入口程序,
 * 在GDT中设置初始进程的TSS和LDT,将初始任务的TSS和LDT分别加
 * 载到TR和LDTR寄存器中。顺便设置系统调用处理入口程序(int 80h)。*/
void sched_init(void)
{
    int i;
    struct desc_struct * p;

    if (sizeof(struct sigaction) != 16)
        panic("Struct sigaction MUST be 16 bytes");

    /* 在GDT中设置初始进程init_task的TSS和LDT。
     * LDT用于保护应用程序内存段;TSS用于保存进程运行上下文。*/
    set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss));
    set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));

    /* 初始化GDT未用表项;初始化task数组 */
    p = gdt+2+FIRST_TSS_ENTRY;
    for(i=1;i<NR_TASKS;i++) {
        task[i] = NULL;
        p->a=p->b=0;
        p++;
        p->a=p->b=0;
        p++;
    }
/* Clear NT, so that we won't have troubles with that later on */
    /* 复位标志寄存器NT位(若NT置位,执行IRET时会进行任务切换 80386_P7.5) */
    __asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");

    /* 分别加载初始进程TSS和LDT到TR和LDTR寄存器,
     * 在进入用户态时,CPU会执行TR所指TSS所描述的进程。*/
    ltr(0);
    lldt(0);
/* 粗略理解用户模式下的多任务切换过程。
 * CPU --> TR-->GDT[TR] --> TSS;
 * GDT[TSS.ldt]-->LDT[TSS.cs+TSS.ds]-->CS:EIP(DS:ESI) */

    /* 配置定时器控制器8254,分配给定时器控制器的端口地
     * 址范围[0x40, 0x5f],8254使用端口地址最低两位用于
     * 选择3通道和1寄存器,所以只用到了前三个端口地址。
     *
     * 0x43地址低2位为1, 使用out指令时写方式控制寄存器,
     * 0x36: 选择通道0计数器, 计数器读写操作:先读/写LSB后MSB;
     * 方式3方波计数, 16位二进制;
     * 
     * 0x40地址低两位为1,out指令时表示装入通道0计数器,先低8位后高8位。
     * 
     * 对通道0计数器中赋予初始值(计数器产生方波序列到8259A-1 IRQ0
     * 8259A配置的中断方式是沿上升沿触发中断,减法计数器减1计数,中
     * 断定时器频率为1.1931816)。此处配置通道0计数器初始值为频率值/100,
     * 即约0.01s(10ms)发生一次定时器中断。
     *
     * 定时器计数频率为1.1931816MHz, 即1s约1193182次计数,
     * 当设定计数器初值为fs/100时, 从fs减到0所花时间约为10ms。*/
    outb_p(0x36,0x43);              /* binary, mode 3, LSB/MSB, ch 0 */
    outb_p(LATCH & 0xff , 0x40);    /* LSB */
    outb(LATCH >> 8 , 0x40);        /* MSB */

    /* 设置定时器中断处理函数,
     * 允许8259A-1的IRQ0中断,即开启定时器中断。
     * 
     * 粗略走一下timer_interrupt函数后返回到这里,
     * 最好浏览到调度函数为止。*/
    set_intr_gate(0x20,&timer_interrupt);
    outb(inb_p(0x21)&~0x01,0x21);

    /* 设置系统调用中断处理函数,即CPU执行
     * "int 80h"指令时会跳转执行system_call */
    set_system_gate(0x80,&system_call);
    
/* 依次粗略地跟着timer_interrupt和system_call函数的流程走一圈吧;
 * 粗略了解任务(进程)管理数据结构体类型和管理初始任务结构体吧。
 * 了解这两个流程对后续多进程管理程序的理解有铺垫。
 *
 * 粗略跟踪timer_interrupt和system_call后,此文了解到
 * [1] 在linux0.11内核态,除显式调用schedule()进行任务调度外,不会发生任务调度切换;
 * [2] 在用户态,除通过系统调用(如pause)显式调用schedule()进行任务调度外,只有以下
 * 两种情况会调用schedule()进行任务切换,
 * [2-1] 在当前任务时间片运行完毕后在定时器中断(约每10ms发生一次)处理程序中;
 * [2-2] 在系统调用完毕检查到当前进程处于非运行状态(见_system_call)。*/
}
