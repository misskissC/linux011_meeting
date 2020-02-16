#ifndef _SCHED_H
#define _SCHED_H

#define NR_TASKS 64
#define HZ 100

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

/* 表征任务状态(state),
 * TASK_RUNNING(0) - 标识进程就绪状态,可运行状态;
 * TASK_INTERRUPTIBLE(1)   - 标识进程处于准备就绪状态;
 * TASK_UNINTERRUPTIBLE(2) - 标识进程处于阻塞状态;
 * TASK_ZOMBIE(3)  - 标识进程处于僵尸状态,运行结束但其资源未被回收;
 * TASK_STOPPED(4) - 标识进程处于停止状态。*/
#define TASK_RUNNING            0
#define TASK_INTERRUPTIBLE      1
#define TASK_UNINTERRUPTIBLE    2
#define TASK_ZOMBIE             3
#define TASK_STOPPED            4

#ifndef NULL
#define NULL ((void *) 0)
#endif

extern int copy_page_tables(unsigned long from, unsigned long to, long size);
extern int free_page_tables(unsigned long from, unsigned long size);

extern void sched_init(void);
extern void schedule(void);
extern void trap_init(void);
extern void panic(const char * str);
extern int tty_write(unsigned minor,char * buf,int count);

/* 系统调用函数指针 */
typedef int (*fn_ptr)();

/* struct i387_struct,
 * 记录数学协处理器状态的结构体。
 * 用于保存各进程被切换时在数学协处理器上执行状态。*/
struct i387_struct {
    long cwd; /* 控制字 */
    long swd; /* 状态字 */
    long twd; /* 标记字 */
    long fip; /* 协处理器所执行代码偏移 */
    long fcs; /* 协处理器执行代码段 */
    long foo; /* 内存操作数偏移 */
    long fos; /* 内存操作数数据段 */
    /* 协处理器累加器;每个累加器有10字节 */
    long st_space[20]; /* 8*10 bytes for each FP-reg = 80 bytes */
};

/* struct tss_struct,
 * 描述进程TSS的结构体。*/
struct tss_struct {
    long back_link; /* 前一进程TSS选择符 */

    /* [0..2]特权级下维护栈内存的寄存器 */
    long esp0;
    long ss0;   /* 16 high bits zero */
    long esp1;
    long ss1;   /* 16 high bits zero */
    long esp2;
    long ss2;   /* 16 high bits zero */

    /* 用于备份进程当前各寄存器值 */
    long cr3;    /* 页目录基址 */
    long eip;    /* cs:eip指向当前需执行指令地址 */
    long eflags; /* 标志寄存器 */
    long eax,ecx,edx,ebx;
    long esp;
    long ebp;
    long esi;
    long edi;
    long es;  /* 16 high bits zero */
    long cs;  /* 16 high bits zero */
    long ss;  /* 16 high bits zero */
    long ds;  /* 16 high bits zero */
    long fs;  /* 16 high bits zero */
    long gs;  /* 16 high bits zero */
    
    long ldt; /* LDT段选择符 */
    long trace_bitmap; /* bits: trace 0, bitmap 16-31为I/O位图基址 */
    struct i387_struct i387; /* 用于备份协处理器状态 */
};

/* struct task_struct,
 * 管理任务(进程)的结构体。*/
struct task_struct {
/* these are hardcoded - don't touch */
/* state,标识进程当前运行状态,-1,不可运行未就绪状态,0,可运行状态,>0已停止;
 * counter,进程运行时间片数(1时间片指定时器中断发生周期,此处1时间片约为10ms);
 * priority,标识进程优先级;
 * signal,记录进程当前被施加的信号,每bit对应一种信号;
 * sigaction,处理signal所记录信号的回调函数,可由用户程序设置;
 * blocked,用于屏蔽signal中信号(blocked nr bit位为1则屏蔽signal nr bit位信号) */
    long state;
    long counter;
    long priority;
    long signal;
    struct sigaction sigaction[32];
    long blocked;

/* various fields */
    /* 进程退出运行时值, 由父进程获取 */
    int exit_code;
    /* 进程代码逻辑内存首地址,进程代码段大小,
     * 进程代码+数据段大小,进程所在段总大小,进程栈内存起始地址 */
    unsigned long start_code,end_code,end_data,brk,start_stack;
    /* 进程id,父进程id,进程所属进程组id,
     * 进程所属会话id,进程所属会话中的领导进程id */
    long pid,father,pgrp,session,leader;
    /* 进程用户id,进程有效用户id,
     * 保存的任务id,组id,有效组id,保存组的id。*/
    unsigned short uid,euid,suid;
    unsigned short gid,egid,sgid;
    /* *id标识拥有本进程的用户;
     * 有效*id标识访问进程可执行文件的权限;
     * 保存*id,当进程可执行文件i节点i_mode置位S_ISUID时,
     * 保存*id为有效*id,否则为*id。*/
    
    /* 进程报警超时值,当系统时间达到该值时则会设置SIGALRM信号到signal */
    long alarm;
    /* 进程在用户模式运行时间,内核模式运行时间,
     * 子进程用户运行时间,子进程内核运行时间,进程被创建时间 */
    long utime,stime,cutime,cstime,start_time;
    /* 进程是否使用了协处理器 */
    unsigned short used_math;
    
/* file system info */
    /* 字符设备号 */
    int tty; /* -1 if no tty, so it must be signed */
    unsigned short umask;  /* 文件创建属性屏蔽位 */
    struct m_inode * pwd;  /* 指向进程当前目录的i节点 */
    struct m_inode * root; /* 指向进程根目录的i节点 */
    struct m_inode * executable; /* 指向当前进程可执行文件的i节点 */
    unsigned long close_on_exec; /* 执行时关闭文件描述符位图标志 */
    struct file * filp[NR_OPEN]; /* 进程所打开的文件指针数组 */

/* LDT和TSS是为迎合(CPU)应用程序保护和任务切换的机制。
 * 应用进程的LDT和TSS可以设置在任何可用内存段中(其信
 * 息分别由LDTR和TR存储),此处将其设置在进程管理结构体内。*/
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
    struct desc_struct ldt[3];
/* tss for this task */
    struct tss_struct tss;
};

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */
/* INIT_TASK,
 * 用于初始化管理初始任务结构体。见 init_task 的初始化 */
#define INIT_TASK \
/* state etc */ { 0,15,15, \
/* signals */   0,{{},},0, \
/* ec,brk... */ 0,0,0,0,0,0, \
/* pid etc.. */ 0,-1,0,0,0, \
/* uid etc */   0,0,0,0,0,0, \
/* alarm */ 0,0,0,0,0,0, \
/* math */  0, \
/* fs info */   -1,0022,NULL,NULL,NULL,0, \
/* filp */  {NULL,}, \
    { \
        {0,0}, \
/* ldt */   {0x9f,0xc0fa00}, \
        {0x9f,0xc0f200}, \
    }, \
/*tss*/ {0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir,\
    0,0,0,0,0,0,0,0, \
    0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
    _LDT(0),0x80000000, \
        {} \
    }, \
}

extern struct task_struct *task[NR_TASKS];
extern struct task_struct *last_task_used_math;
extern struct task_struct *current;
extern long volatile jiffies;
extern long startup_time;

/* CURRENT_TIME,
 * 自1970年1月1号0时0分0秒到此时的秒数。*/
#define CURRENT_TIME (startup_time+jiffies/HZ)

extern void add_timer(long jiffies, void (*fn)(void));
extern void sleep_on(struct task_struct ** p);
extern void interruptible_sleep_on(struct task_struct ** p);
extern void wake_up(struct task_struct ** p);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1, 7-LDT1 etc ...
 */
#define FIRST_TSS_ENTRY 4
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)

/* _TSS(n)计算任务n TSS在GDT中的选择符,
 * _LDT(n)计算任务n LDT在GDT中的选择符。*/
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))

/* ltr(n),  将任务n TSS的GDT选择符加载给TR寄存器,
 * lldt(n), 将任务n LDT的GDT选择符加载给LDTR寄存器。*/
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))

/* str(n),
 * 获取当前任务TSS在GDT中的编号, 并赋值给变量n。
 * 
 * 内联汇编输入部分。
 * "a" (0), 将0赋给eax;
 * "i" (FIRST_TSS_ENTRY<<3), 立即数FIRST_TSS_ENTRY<<3,
 * 立即数一般会被编译器保存在某通用空闲寄存器中,
 * FIRST_TSS_ENTRY是第一个TSS在GDT中的索引。
 *
 * 内联汇编指令。
 * str %%ax, 将任务寄存器TR的值赋给ax;
 * subl %2,%%eax, eax=eax - (FIRST_TSS_ENTRY<<3)。
 * shrl $4, %%eax, eax >> 4(算术右移4位), 即得到当前TSS在GDT中编号,
 * TSS在GDT中编号的算法跟其在GDT中设置, 见_TSS(n)宏。
 *
 * 内联汇编输出。
 * n = eax。*/
#define str(n) \
__asm__("str %%ax\n\t" \
	"subl %2,%%eax\n\t" \
	"shrl $4,%%eax" \
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))
/*
 *  switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */
/* switch_to,
 * 切换到任务号为n的进程中运行。
 *
 * 内联汇编输入部分。
 * "d" (_TSS(n)), edx = _TSS(n)即欲切换进程的TSS选择符;
 * "c" ((long) task[n])),ecx = (long) task[n]即管理欲切换进程的结构体地址;
 * "m" (*&__tmp.a), 内存变量__tmp.a;
 * "m" (*&__tmp.b), 内存变量__tmp.b。
 *
 * 内联汇编指令部分。
 * cmpl %%ecx,_current; je 1f
 * 比较欲切换进程是否为当前运行进程,若是则向前跳转到标号1处结束进程切换;
 * 
 * movw %%dx,%1; xchgl %%ecx,_current
 * 若欲切换进程非当前进程,则将dx(TSS选择符)赋给%1即__tmp.b,然后将当前进
 * 程结构体指针current指向欲切换进程结构体;
 *
 * ljmp %0
 * 随后通过长跳转指令ljmp实现任务切换,此处的ljmp指令将__tmp所在内存段内容
 * 作为操作数——低4字节为偏移地址, 接下来的2字节(__tmp.b低2字节)为段选择符。
 * 由于此处的段选择符将索引到描述TSS的GDT表项,这将会引起任务切换。这个过程
 * 大体如下:将本进程运行上下文备份到其TSS中; 将欲切换进程TSS选择符加载到TR
 * 寄存器中并从该TSS种获取欲切换进程运行上下文(CS:EIP SS:ESP LDT选择符等)。
 * (所以没在__tmp.a中指定偏移地址是没有关系的)。
 *
 * cmpl %%ecx,_last_task_used_math; jne 1f; clts
 * 这条语句在本进程再次本调度时执行,该语句 ======= 上次是否使用过协处理器。
 * 若没有则向前跳转标号1处,若使用过则清协处理器使用标志(CR0 TS位)。*/
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,_current\n\t" \
    "je 1f\n\t" \
    "movw %%dx,%1\n\t" \
    "xchgl %%ecx,_current\n\t" \
    "ljmp %0\n\t" \
    "cmpl %%ecx,_last_task_used_math\n\t" \
    "jne 1f\n\t" \
    "clts\n" \
    "1:" \
    ::"m" (*&__tmp.a),"m" (*&__tmp.b), \
    "d" (_TSS(n)),"c" ((long) task[n])); \
}

#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)

/* _set_base(addr,base),
 * 设置addr处段描述符的基址字段。
 * addr - 段描述符地址,
 * base - 段描述符所描述内存段的基址。
 *
 * 将base拆分到段描述符的相应字节中。*/
#define _set_base(addr,base) \
__asm__("movw %%dx,%0\n\t" \
    "rorl $16,%%edx\n\t" \
    "movb %%dl,%1\n\t" \
    "movb %%dh,%2" \
    ::"m" (*((addr)+2)), \
      "m" (*((addr)+4)), \
      "m" (*((addr)+7)), \
      "d" (base) \
    :"dx")

/* _set_limit(addr,limit),
 * 设置addr处段描述符处的限长字段。*/
#define _set_limit(addr,limit) \
__asm__("movw %%dx,%0\n\t" \
    "rorl $16,%%edx\n\t" \
    "movb %1,%%dh\n\t" \
    "andb $0xf0,%%dh\n\t" \
    "orb %%dh,%%dl\n\t" \
    "movb %%dl,%1" \
    ::"m" (*(addr)), \
      "m" (*((addr)+6)), \
      "d" (limit) \
    :"dx")

/* set_base, 用base在ldt处的LDT表项中设置基址字段;
 * set_limit,用limit设置ldt处的LDT表项的限长字段。*/
#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , base )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 )

/* _get_base(addr),
 * 获取addr处描述符的基址字段。
 *
 * 内联汇编输入。
 * %1 - "m" (*((addr)+2)),存于内存(addr + 2)处的C变量。
 *
 * 内联汇编输出。
 * "=d" (__base),内联汇编的输出存于edx寄存器,然后将edx值赋给__base变量。
 *
 * 内联汇编指令。
 * 将%3(base[31..24])赋给dh,
 * 将%2(base[23..16])赋给dl,
 * 将dx左移16位, 将%1(base[15..0])赋给dx
 * 
 * edx --> __base, 作为宏_get_base中表达式最终值。*/
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \
    "movb %2,%%dl\n\t" \
    "shll $16,%%edx\n\t" \
    "movw %1,%%dx" \
    :"=d" (__base) \
    :"m" (*((addr)+2)), \
     "m" (*((addr)+4)), \
     "m" (*((addr)+7))); \
__base;})

/* get_base,
 * 获取ldt处段描述符的基址。*/
#define get_base(ldt) _get_base( ((char *)&(ldt)) )

/* 获取段描述符中的段限长。
 * 
 * 参数segment - 段描述符选择符。
 *
 * 内联汇编输入。
 * %1 - "r"(segment), 将segment存于任何空闲通用寄存器中。
 * 
 * 内联汇编输出。
 * %0 - "=r" (__limit), 内联汇编的运算结果保存在任何空闲
 * 的通用寄存器中,并将该寄存器值赋给__limit。
 *
 * 内联汇编指令。
 * 获取%1(段描述符选择符)对应描述符中的限长到%0(任意空闲
 * 寄存器)中并增1(限长+1段描述符所描述内存段长度)。
 * 
 * __limit为表达式get_limit最终值。*/
#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
