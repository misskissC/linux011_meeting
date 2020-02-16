/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 *
 * 为了保证调用fork()处栈帧的正确性,此文使用内联函数机制即直接将fork()
 * 的机器码嵌套在其被调用处以避免函数调用对栈的操作。
 *
 * 实际上,只有pause()和fork()函数需要被定义为内联函数, 其他函数只是顺带
 * 这样定义。
 */
/* 宏_syscall*定义在unistd.h中 */
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
/* setup.s通过BIOS调用将扩展内存大小信息存于
 * [0x90002, 0x90003]2字节内存中;
 * 将2个硬盘参数信息存于[0x90080, 0x900a0)32字节内存中。
 * bootsect.s将根文件系统的设备号存于[0x901FC, 0x901FD]2字节内存中。
 *
 * 以下几个宏定义分别从相应内存中读取出这几种信息。
 * 如(*(struct drive_info *)0x90080)
 * 将0x90080转换为指向(struct drive_info)类型的地址,
 * 然后从该地址中取出(struct drive_info)类型值。*/
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 *
 * 好吧,这个宏编写得有些勉强,因为我没找到CMOS RAM如何工作的资料,而这个宏
 * 似乎也恰能正确工作。如果有人有CMOS RAM的资料,  可以爱心式地分享我一份。
 * 这个宏是通过不断调试加阅读一些BIOS手册得来的,不容易~
 */
/* 70h端口用于接收CMOS RAM的内存地址,
 * 71h端口用于读写70h对应的内存单元。*/
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

/* BCD以4位2进制表示一个十进制数,
 * 在一字节中, 高4位对应十进制数的十位,低4位对应个位。*/
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

/* 从RT/CMOS RAM接口芯片中读取时间,若读过程所花时间超过1s则重读。
 * 
 * 因为RT/CMOS RAM中的时间为BCD码,  所以需要将BCD码转换为十进制。
 *
 * CMOS RAM中初始时间为1970.01.01 00:00:00,将当前时间和初始时间
 * 差值转换为秒存入startup_time作为系统的开机时间。
 *
 * RT/CMOS RAM接口芯片由一个时钟和一块RAM组成,其有专门的电池供电,
 * 电脑关机后也能依靠电池继续工作。*/
static void time_init(void)
{
    struct tm time;

    do {
        time.tm_sec = CMOS_READ(0);
        time.tm_min = CMOS_READ(2);
        time.tm_hour = CMOS_READ(4);
        time.tm_mday = CMOS_READ(7);
        time.tm_mon = CMOS_READ(8);
        time.tm_year = CMOS_READ(9); /* 年份后两位 */
    } while (time.tm_sec != CMOS_READ(0));
    BCD_TO_BIN(time.tm_sec);
    BCD_TO_BIN(time.tm_min);
    BCD_TO_BIN(time.tm_hour);
    BCD_TO_BIN(time.tm_mday);
    BCD_TO_BIN(time.tm_mon);
    BCD_TO_BIN(time.tm_year);
    time.tm_mon--;
    startup_time = kernel_mktime(&time);
}
/* (Real Time)RT/CMOS RAM接口芯片可参考
 *《微型机（PC系列）接口控制教程》P123_128。
 * 70h接收CMOS RAM内存地址,71h用于读写70h内存地址对应内存单元。*/

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

/* struct drive_info结构体类型用于描述在setup.s中获取的硬盘参数,
 * drive_info用于保存这些硬盘参数信息,在main开始处被初始化。*/
struct drive_info { char dummy[32]; } drive_info;

/* head.s完成保护模式的初始化工作后,便跳转到此处(见head.s)。*/
void main(void) /* This really IS void, no error here. */
{                  /* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 *
 * 此处main函数的返回类型和参数可以为void,启动程序代码(head.s)也是这样
 * "调用"main函数的。在未完全建立中断机制前,我们曾在setup.s中禁止了CPU
 * 处理中断,再做些必要设置后就可以重新使能CPU处理中断了。
 */

    /* 在bootsect.s偏移508处设置了根文件系统的逻辑设备分区号。此语句
     * 即获取bootsect.s所设置的根文件设备号存储到全局变量ROOT_DEV中。*/
    ROOT_DEV = ORIG_ROOT_DEV;

    /* 将setup.s通过BIOS所获取的硬盘参数信息存于全局变量drive_info中。*/
    drive_info = DRIVE_INFO;

    /* 计算内存总大小: 实模式内存(1Mb) + 扩展内存。
     * 在setup.s中开启页机制后, 内存以4Kb大小对齐,所以
     * 若内存总大小不为4Kb整数倍,则舍弃末尾不足4Kb部分。*/
    memory_end = (1<<20) + (EXT_MEM_K<<10);
    memory_end &= 0xfffff000;

    /* 用全局变量记录linux 0.11按用途所划分的内存段。
     *
     * MAIN_MEMORY-[main_memory_start, memory_end),
     * memory_end为linux 0.11所使用实际物理内存总大小,最大为16Mb。
     * 
     * BUFFER,操作系统内核程序将其用作外设(如硬盘)的缓冲区,
     * 这部分内存范围为[操作系统程序末尾处, buffer_memory_end)。
     *
     * RAM-DISK, 若定义了虚拟磁盘(用一段内存模拟磁盘),则操作系统内
     * 核程序将内存地址空间[buffer_memory_end, main_memory_start)用作虚拟磁盘。*/
    if (memory_end > 16*1024*1024)
        memory_end = 16*1024*1024;
    if (memory_end > 12*1024*1024) 
        buffer_memory_end = 4*1024*1024;
    else if (memory_end > 6*1024*1024)
        buffer_memory_end = 2*1024*1024;
    else
        buffer_memory_end = 1*1024*1024;
    main_memory_start = buffer_memory_end;
#ifdef RAMDISK /* 虚拟硬盘管理初始化 */
    main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif 
/* 经以上划分后, linux 0.11内存地址空间分布大体如下(16Mb为例)。
 * ---------------------------------------------------
 * | OS routines | BUFFER | [RAM-DISK] | MAIN_MEMORY |
 * |-------------|--------|------------|-------------|
 * 0x0           end      4Mb                        16Mb
 * BUFFER 用作硬盘、软盘等外设缓冲区;
 * RAM-DISK 用作虚拟磁盘(若定义);
 * MAIN_MEMORY 为剩余内存,将用作内核数据结构体的内存空间。*/

    /* 初始化主存(MAIN_MEMORY)的管理 */
    mem_init(main_memory_start,memory_end);

    trap_init();    /* 初始设置IDT和PIC */
    blk_dev_init(); /* 块设备请求管理初始化 */
    chr_dev_init();
    tty_init();     /* 字符设备及其请求管理的初始化 */
    time_init();    /* 设置系统开机时间 */
    sched_init();   /* 多任务管理初始化 */
    buffer_init(buffer_memory_end); /* 缓冲区管理初始化 */
    hd_init();      /* 块设备硬盘及其请求管理初始化 */
    floppy_init();  /* 块设备软盘及其请求管理初始化 */
    sti();          /* 允许CPU处理中断 */
    
    /* 从CPU内核模式转移到CPU用户模式以 init_task 所管理任务的名义执行。
     * move_to_user_mode执行完毕后,CPU将首次进入用户态和多任务运行模式。*/
    move_to_user_mode();

    /* 在初始进程中创建子进程(初始进程由结构体 init_task 描述和管理)。
     * 
     * fork定义在本文件的开头处: static inline _syscall0(int,fork),根
     * 据宏_syscall0看看fork的定义和执行轨迹,以理解fork()的两次返回即
     * 需被定义为内联函数的原理吧。*/
    if (!fork()) {  /* we count on this going ok */
        /* 子进程的执行流程开始处 */
        init();
    }
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 *
 * 注,由于task0所管理进程(初始进程)在系统无其他能运行进程时会默认运行,
 * 即会忽略pause()为其设置的就绪状态。所以, 在初始进程中调用pause()时,
 * 相当于仅是检查系统有没有其他可运行的进程, 若有则调度时间片最大的进
 * 程运行,若无则返回到初始进程中。—— 见schedule()。
 *
 * 而在其他进程中调用 pause() 后, 由于pause()会设置当前进程为就绪状态,
 * 所以需要用信号去唤醒该进程。
 */
/* pause()定义在本文件开头处,static inline _syscall0(int,pause),
 * 其对应的内核代码为sched.c/ sys_pause(). */
    for(;;) pause();
}

/* printf,
 * 写标准输出设备的可变参数函数。*/
static int printf(const char *fmt, ...)
{
    va_list args;
    int i;

    /* 让args指向fmt之后一个参数的栈地址 */
    va_start(args, fmt);
    
    /* 将printffmt之后的参数写往标准输出设备显示 */
    write(1,printbuf,i=vsprintf(printbuf, fmt, args));
    va_end(args);
    return i;
}

/* sh程序的命令行参数,环境变量参数 */
static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

/* init,
 * init进程,该进程创建子进程并加载可执行程序sh运行。
 * 
 * 若子进程sh被终止,则init进程会回收sh进程所创建子进
 * 程资源,并会立即再创建子进程并加载sh程序运行。*/
void init(void)
{
    int pid,i;

    /* setup,
     * 根据存储在drive_info中的硬盘参数,获取硬盘分区参数,
     * 并设置根文件系统和RAMDISK(若有)。
     * 
     * 系统调用setup定义在本文件开头处,
     * static inline _syscall1(int, setup, void *, BIOS),
     * 其对应的内核程序为hd.c/int sys_setup(void * BIOS)。*/
    setup((void *) &drive_info);

    /* 以读写属性打开文件/dev/tty0。由于此处是首次打开文件,
     * /dev/tty0的文件描述符将为0,即/dev/tty0即是与控制终端
     * 关联的文件哦。*/
    (void) open("/dev/tty0",O_RDWR,0);
/* 通过open打开或创建/dev/tty0时,根据底层函数实现,目录/dev
 * 应存在。/dev目录(甚至是/dev/tty0)可能是在格式化MINIX文件
 * 系统静态创建的。*/
 
    /* dup(0),
     * 让本进程中后续空闲描述符(1和2)指向文件描述符0所关联
     * 文件/dev/tty0。
     * 
     * dup定义在lib/dup.c中,其对应的内核函数为sys_dup()。*/
    (void) dup(0);
    (void) dup(0);
/* 此处创建文件/dev/tty0与控制终端关联,由文件描述符0,1,2关联,
 * 即系统层面所提到stdin,stdeout,stderr,即对应标准输入,标准输
 * 出,错误输出。*/

    /* 打印缓冲区和主存大小 */
    printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
        NR_BUFFERS*BLOCK_SIZE);
    printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);

    /* 在init进程中创建子进程 */
    if (!(pid=fork())) {
        /* 在init子进程中关闭标准输入。以用文件描述
         * 符0关联以只读方式打开的/etc/rc文件。close
         * 函数定义在lib/close.c文件中。*/
        close(0);
        if (open("/etc/rc",O_RDONLY,0))
            _exit(1);

        /* 加载/bin/sh"覆盖本进程"并执行sh程序,argv_rc
         * 和envp_rc分别充当sh程序的命令行和环境变量参数。
         *
         * execve函数定义在lib/execve.c中。*/
        execve("/bin/sh",argv_rc,envp_rc);
        
        /* 若execve加载可执行程序/bin/sh成功则该语句不会被执行,若
         * 失败则会执行_exit(2)退出本进程。另外,此处传给/bin/sh程
         * 序的参数使得/bin/sh为非交互模式运行,其运行完毕后会退出。*/
        _exit(2);
    }
/* 等待子进程退出返回,定义在lib/wait.c中。
 * 可以不用pid >0 的判断,子进程不会执行以下代码。*/
    if (pid>0)
        while (pid != wait(&i))
            /* nothing */;
    
    /* 非交互式/bin/sh运行退出时,重新创建子进程以交互式执行/bash/sh程序 */
    while (1) {
        if ((pid=fork())<0) {
            printf("Fork failed in init\r\n");
            continue;
        }
        /* 在子进程中关闭标准输入输出和错误输出;
         * 新建会话组;重新打开控制终端并将控制终
         * 端复制给标准输出和错误输出文件描述符;
         * 并重新启动/bin/sh程序以交互模式运行。*/
        if (!pid) {
            close(0);close(1);close(2);
            /* 创建会话组;
             * 定义在lib/setsid.c中,
             * 其内核函数为kernel/sys.c/sys_setsid() */
            setsid();
            (void) open("/dev/tty0",O_RDWR,0);
            (void) dup(0);
            (void) dup(0);
            _exit(execve("/bin/sh",argv,envp));
        }
        /* 等待子进程结束,子进程结束则提示并刷新缓冲区然后
         * 再回到循环开始处再次创建子进程运行/bin/sh程序。*/
        while (1)
            if (pid == wait(&i))
                break;
        printf("\n\rchild %d died with code %04x\n\r",pid,i);
        /* 同步缓冲区内容到设备上。
         * 
         * 其定义在本文件开始部分,
         * static inline _syscall0(int,sync),
         * 其内核函数为fs/buffer.c/sys_sync()。*/
        sync();
    }

    _exit(0); /* NOTE! _exit, not exit() */
}
