/*
 *  linux/kernel/floppy.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 02.12.91 - Changed to static variables to indicate need for reset
 * and recalibrate. This makes some things easier (output_byte reset
 * checking etc), and means less interrupt jumping in case of errors,
 * so the code is hopefully easier to understand.
 */
/* 增加静态变量用于记录重置和校准。使用静态变量可以让编码稍简单一些,
 * 能减少错误发生时的跳转,能让代码的阅读性好一点。*/

/*
 * This file is certainly a mess. I've tried my best to get it working,
 * but I don't like programming floppies, and I have only one anyway.
 * Urgel. I should check for more errors, and do more graceful error
 * recovery. Seems there are problems with several drives. I've tried to
 * correct them. No promises. 
 */
/* 本文件有些乱。此文已经尽最大努力让其正确运行了,此文承认不太喜欢编写软盘
 * 模块的程序,但此文别无选择,额......此文应该再增加些对错误的检查并做相应
 * 错误修复的功能。在多驱动器下,似乎还有些问题,此文已经尽力修复了他们,但并
 * 不保证没有问题。*/

/*
 * As with hd.c, all routines within this file can (and will) be called
 * by interrupts, so extreme caution is needed. A hardware interrupt
 * handler may not sleep, or a kernel panic will happen. Thus I cannot
 * call "floppy-on" directly, but have to set a special timer interrupt
 * etc.
 *
 * Also, I'm not certain this works on more than 1 floppy. Bugs may
 * abund.
 */
/* 同hd.c,本文件中的程序也需能被中断处理程序调用,所以在编写此文件中函数时
 * 需额外谨慎。如中断处理程序可能不适合睡眠,内核可能会调用 painc 。所以此
 * 文不能直接编写 "floppy-on* 似的函数,而需要为软盘设置一个定时器及回调函
 * 数。另外,在超过1个软盘时,此文不确保是否还能正常工作,可能会有潜在bug。*/
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 2 /* 软盘主设备号 */
#include "blk.h"

static int recalibrate = 0;
static int reset = 0;
static int seek = 0;

extern unsigned char current_DOR;

/* immoutb_p(val, port),
 * 将val低字节内容写往端口port,并用jmp指令适当延时。*/
#define immoutb_p(val,port) \
__asm__("outb %0,%1\n\tjmp 1f\n1:\tjmp 1f\n1:"::"a" ((char) (val)),"i" (port))

#define TYPE(x) ((x)>>2)
#define DRIVE(x) ((x)&0x03)
/*
 * Note that MAX_ERRORS=8 doesn't imply that we retry every bad read
 * max 8 times - some types of errors increase the errorcount by 2,
 * so we might actually retry only 5-6 times before giving up.
 */
/* 注,MAX_ERRORS=8并不意味着所有操作在出错时都会操作8次,因为有些操作
 * 出错时错误计数步长为2,所以在放弃操作前可能会重试5-6次。*/
#define MAX_ERRORS 8

/*
 * globals used by 'result()'
 */
#define MAX_REPLIES 7
static unsigned char reply_buffer[MAX_REPLIES];
#define ST0 (reply_buffer[0])
#define ST1 (reply_buffer[1])
#define ST2 (reply_buffer[2])
#define ST3 (reply_buffer[3])

/*
 * This struct defines the different floppy types. Unlike minix
 * linux doesn't have a "search for right type"-type, as the code
 * for that is convoluted and weird. I've got enough problems with
 * this driver as it is.
 *
 * The 'stretch' tells if the tracks need to be boubled for some
 * types (ie 360kB diskette in 1.2MB drive etc). Others should
 * be self-explanatory.
 */
/* struct floppy_struct,
 * 软盘信息结构体类型。*/
static struct floppy_struct {
    /* size,扇区总数;sect,每磁道扇区数;head,磁头数;
     * track,磁道数;stretch,磁道特殊处理标志;
     * gap,扇区间隙长度;rate,数据传输速率;
     */
    unsigned int size, sect, head, track, stretch;
    unsigned char gap,rate,spec1;
} floppy_type[] = {
    {    0, 0,0, 0,0,0x00,0x00,0x00 }, /* no testing */
    {  720, 9,2,40,0,0x2A,0x02,0xDF }, /* 360kB PC diskettes */
    { 2400,15,2,80,0,0x1B,0x00,0xDF }, /* 1.2 MB AT-diskettes */
    {  720, 9,2,40,1,0x2A,0x02,0xDF }, /* 360kB in 720kB drive */
    { 1440, 9,2,80,0,0x2A,0x02,0xDF }, /* 3.5" 720kB diskette */
    {  720, 9,2,40,1,0x23,0x01,0xDF }, /* 360kB in 1.2MB drive */
    { 1440, 9,2,80,0,0x23,0x01,0xDF }, /* 720kB in 1.2MB drive */
    { 2880,18,2,80,0,0x1B,0x00,0xCF }, /* 1.44MB diskette */
};
/*
 * Rate is 0 for 500kb/s, 2 for 300kbps, 1 for 250kbps
 * Spec1 is 0xSH, where S is stepping rate (F=1ms, E=2ms, D=3ms etc),
 * H is head unload time (1=16ms, 2=32ms, etc)
 *
 * Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms etc)
 * and ND is set means no DMA. Hardcoded to 6 (HLD=6ms, use DMA).
 */

extern void floppy_interrupt(void);
extern char tmp_floppy_area[1024];

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
/* 软盘请求相关参数 */
static int cur_spec1 = -1; /* 软盘参数 */
static int cur_rate = -1;  /* 软盘转速 */
static struct floppy_struct * floppy = floppy_type; /* 软盘类型数组地址 */
static unsigned char current_drive = 0; /* 当前软盘驱动号 */
static unsigned char sector = 0; /* 当前扇区号 */
static unsigned char head = 0;   /* 当前磁头号 */
static unsigned char track = 0;  /* 当前磁道号 */
static unsigned char seek_track = 0; /* 寻道磁道号 */
static unsigned char current_track = 255; /* 当前磁头所在磁道号 */
static unsigned char command = 0; /* 当前访问软盘的操作命令 */
unsigned char selected = 0; /* 软驱是否已选择的标志 */
struct task_struct * wait_on_floppy_select = NULL; /* 用于进程等待某软驱的任务指针 */

/* floppy_deselect,
 * 复位软驱已选择标志,唤醒等在当前软驱上的进程。*/
void floppy_deselect(unsigned int nr)
{
    if (nr != (current_DOR & 3))
        printk("floppy_deselect: drive not selected\n\r");
    selected = 0;
    wake_up(&wait_on_floppy_select);
}

/*
 * floppy-change is never called from an interrupt, so we can relax a bit
 * here, sleep etc. Note that floppy-on tries to set current_DOR to point
 * to the desired drive, but it will probably not survive the sleep if
 * several floppies are used at the same time: thus the loop.
 */
/* floppy_change,
 * 读取软盘控制器数字输入寄存器,判断该软
 * 驱动器下是否更换过软盘,若更换过则返回
 * 1,否则返回0。*/
int floppy_change(unsigned int nr)
{
repeat:
    floppy_on(nr);/* 等待nr对应软驱下软盘转速正常 */

    /* 等待当前软驱为nr对应的软驱 */
    while ((current_DOR & 3) != nr && selected)
        interruptible_sleep_on(&wait_on_floppy_select);
    if ((current_DOR & 3) != nr)
        goto repeat;

    /* 读输入寄存器,bit[7]=1表示nr软驱下的软盘已更换 */
    if (inb(FD_DIR) & 0x80) {
        floppy_off(nr);
        return 1;
    }
    floppy_off(nr);
    return 0;
}

/* copy_buffer(from, to),
 * 从from内存段拷贝1024字节内容到to所指内存段。
 * 
 * 内联汇编输入。
 * "c" (BLOCK_SIZE/4), ecx = BLOCK_SIZE / 4;
 * "S" ((long)(from)), ESI = form;
 * "D" ((long)(to)),   EDI = to;
 *
 * 内联汇编指令。
 * cld; rep; movsl 相当于
 * while (ecx--) 
 *    movl ds:esi, es:edi
 *    esi += 4
 *    edi += 4
 * 即从from内存段拷贝BLOCK_SIZE字节内容到to内存段。*/
#define copy_buffer(from,to) \
__asm__("cld ; rep ; movsl" \
    ::"c" (BLOCK_SIZE/4),"S" ((long)(from)),"D" ((long)(to)) \
    :"cx","di","si")

/* setup_DMA,
 * 设置DMA芯片用于软驱传输数据的通道2,
 * 以使得软盘数据的读写以DMA方式进行。*/
static void setup_DMA(void)
{
    /* 当前请求中用于存储数据的缓冲区首地址 */
    long addr = (long) CURRENT->buffer;

/* 因为DMA芯片8237A只能寻址1Mb以内内存地址空间,
 * 所以当addr地址超过1Mb时则使用软盘临时缓冲区
 * 承载访问软盘中的数据。*/
    cli();
    if (addr >= 0x100000) {
    addr = (long) tmp_floppy_area;
    if (command == FD_WRITE)
        copy_buffer(CURRENT->buffer,tmp_floppy_area);
    }
/* mask DMA 2 */
    /* DMA单通道屏蔽端口地址为0x0A,
     * bit[1..0]用于指定通道x,bit[2]=1则屏蔽通道x。*/
    immoutb_p(4|2,10);
/* output command byte. I don't know why, but everyone (minix, */
/* sanches & canton) output this twice, first to 12 then to 11 */
/* 根据当前命令command向DMA端口0x0B和0x0C写方式字(读或写) */
__asm__("outb %%al,$12\n\tjmp 1f\n1:\tjmp 1f\n1:\t"
    "outb %%al,$11\n\tjmp 1f\n1:\tjmp 1f\n1:"::
    "a" ((char) ((command == FD_READ)?DMA_READ:DMA_WRITE)));

/* 向DMA端口地址0x04中写入CPU侧访问数据内存首地址addr */
/* 8 low bits of addr */
    immoutb_p(addr,4);
    addr >>= 8;
/* bits 8-15 of addr */
    immoutb_p(addr,4);
    addr >>= 8;
/* bits 16-19 of addr */
    immoutb_p(addr,0x81);
/* 向DMA端口地址0x05写入需传输的字节数 */
/* low 8 bits of count-1 (1024-1=0x3ff) */
    immoutb_p(0xff,5);
/* high 8 bits of count-1 */
    immoutb_p(3,5);
/* activate DMA 2 */
/* 使能DMA通道2 */
    immoutb_p(0|2,10);
    sti();
}

/* output_byte,
 * 向软盘输出指定指定字节byte。*/
static void output_byte(char byte)
{
    int counter;
    unsigned char status;

    if (reset)
        return;
    /* 获取软盘主状态控制器0x3f4状态,若已就绪且方向位置位(CPU->FDC)则
     * 向数据端口输出指定字节数据byte。*/
    for(counter = 0 ; counter < 10000 ; counter++) {
        status = inb_p(FD_STATUS) & (STATUS_READY | STATUS_DIR);
        if (status == STATUS_READY) {
            outb(byte,FD_DATA);
            return;
        }
    }
    /* 若10000次后还未写成功则置位软盘复位标志 */
    reset = 1;
    printk("Unable to send byte to FDC\n\r");
}

/* result,
 * 读取FDC执行结果于全局数组reply_buffer中。*/
static int result(void)
{
    int i = 0, counter, status;

    if (reset)
        return -1;
    for (counter = 0 ; counter < 10000 ; counter++) {
        status = inb_p(FD_STATUS)&(STATUS_DIR|STATUS_READY|STATUS_BUSY);
        if (status == STATUS_READY)
            return i; /* 若控制器状态为READY则表示数据已读完 */
        /* 若控制器状态方向标志置位(CPU<-FDC),已准备好,忙则表示有数据 */
        if (status == (STATUS_DIR|STATUS_READY|STATUS_BUSY)) {
            if (i >= MAX_REPLIES)
                break;
            reply_buffer[i++] = inb_p(FD_DATA);
        }
    }
    /* 若10000次还未获取完成则表超时,则置位复位软盘 */
    reset = 1;
    printk("Getstatus times out\n\r");
    return -1;
}

/* bad_flp_intr,
 * 记录软盘访问失败次数,并根据失败次数做一些处理,
 * 若访问次数超过最大允许次数则放弃当前请求,若当前
 * 访问失败次数超过最大失败次数的一半则置软盘复位标
 * 志。若访问失败还未超过最大失败数一半则置软盘重置
 * 标志。*/
static void bad_flp_intr(void)
{
    CURRENT->errors++;
    if (CURRENT->errors > MAX_ERRORS) {
        floppy_deselect(current_drive);
        end_request(0);
    }
    if (CURRENT->errors > MAX_ERRORS/2)
        reset = 1;
    else
        recalibrate = 1;
}

/*
 * Ok, this interrupt is called after a DMA read/write has succeeded,
 * so we check the results, and copy any buffers.
 */
/* rw_interrupt,
 * 用于检查DMA读写请求结果并拷贝所读数据到缓冲区块中。*/
static void rw_interrupt(void)
{
/* 获取FDC并检查相关位状态,若出错则做些处理后重新请求 */
    if (result() != 7 || (ST0 & 0xf8) || (ST1 & 0xbf) || (ST2 & 0x73)) {
        if (ST1 & 0x02) {
            printk("Drive %d is write protected\n\r",current_drive);
            floppy_deselect(current_drive);
            end_request(0);
        } else
            bad_flp_intr();
        do_fd_request();
        return;
    }
    /* 若当前读软盘且原缓冲区块在1Mb以外则拷贝软盘临时缓冲区块的数据到目的
     * 缓冲区块中并结束本次软盘请求,并继续调度下一个软盘请求。*/
    if (command == FD_READ && (unsigned long)(CURRENT->buffer) >= 0x100000)
        copy_buffer(tmp_floppy_area,CURRENT->buffer);
    floppy_deselect(current_drive);
    end_request(1);
    do_fd_request();
}

/* setup_rw_floppy,
 * 设置DMA通道2用于传输软盘数据,并向软盘控制器
 * 下发读写相关命令和参数。*/
inline void setup_rw_floppy(void)
{
    /* 设置DMA 2通道用于当前软盘传输数据 */
    setup_DMA();

    /* 设置完成DMA数据传输后的中断函数指针 */
    do_floppy = rw_interrupt;
    output_byte(command); /* 向软盘发送访问的命令字节 */
    output_byte(head<<2 | current_drive); /* 磁头和驱动器号 */
    output_byte(track); /* 磁道号 */
    output_byte(head);  /* 磁头号 */
    output_byte(sector); /* 起始扇区号 */
    output_byte(2); /* sector size = 512 */
    output_byte(floppy->sect); /* 每磁道扇区数 */
    output_byte(floppy->gap);  /* 扇区间隔长度 */
    output_byte(0xFF);  /* sector size (0xff when n!=0 ?) */
    if (reset) /* 若以上设置有出错则调用do_fd_request复位软盘 */
        do_fd_request();
}

/*
 * This is the routine called after every seek (or recalibrate) interrupt
 * from the floppy controller. Note that the "unexpected interrupt" routine
 * also does a recalibrate, but doesn't come here.
 */
/* seek_interrupt,
 * 在软盘校正后调用该函数检查软盘校正执行结果。*/
static void seek_interrupt(void)
{
/* sense drive status */
/* 获取寻道操作执行结果,若发生错误则记录访问软盘出错次数 */
    output_byte(FD_SENSEI);
    if (result() != 2 || (ST0 & 0xF8) != 0x20 || ST1 != seek_track) {
        bad_flp_intr();
        do_fd_request();
        return;
    }
    /* 若寻道操作成功则继续当前软盘访问请求,
     * 向软盘发送访问命令和参数。*/
    current_track = ST1;
    setup_rw_floppy();
}

/*
 * This routine is called when everything should be correctly set up
 * for the transfer (ie floppy motor is on and the correct floppy is
 * selected).
 */
/* transfer,
 * 处理软盘数据传输。*/
static void transfer(void)
{
/* 检查当前软驱是否为指定驱动器参数,若
 * 不是则设置当前软驱参数。*/
    if (cur_spec1 != floppy->spec1) {
        cur_spec1 = floppy->spec1;
        output_byte(FD_SPECIFY);
        output_byte(cur_spec1); /* hut etc */
        output_byte(6); /* Head load time =6ms, DMA */
    }
    if (cur_rate != floppy->rate)
        outb_p(cur_rate = floppy->rate,FD_DCR);
    if (reset) {
        do_fd_request();
        return;
    }
    /* 不需要寻道则设置DMA并下发对应命令和参数 */
    if (!seek) {
        setup_rw_floppy();
        return;
    }
    /* 执行寻道处理,下发相关的寻道函数 */
    do_floppy = seek_interrupt;
    if (seek_track) {
        output_byte(FD_SEEK);
        output_byte(head<<2 | current_drive);
        output_byte(seek_track);
    } else {
        output_byte(FD_RECALIBRATE);
        output_byte(head<<2 | current_drive);
    }
    /* 若下发命令失败导致置位软盘复位标志则立即执行软盘复位 */
    if (reset)
        do_fd_request();
}

/*
 * Special case - used after a unexpected interrupt (or reset)
 */
/* recal_interrupt,
 * 软驱校正中断C处理函数。*/
static void recal_interrupt(void)
{
    /* 检测中断状态,若异常则置软盘复位标志;
     * 最后调度软盘下一请求。*/
    output_byte(FD_SENSEI);
    if (result()!=2 || (ST0 & 0xE0) == 0x60)
        reset = 1;
    else
        recalibrate = 0;
    do_fd_request();
}

/* unexpected_floppy_interrupt,
 * 检测中断状态,若异常则置位软盘复位标志。*/
void unexpected_floppy_interrupt(void)
{
    output_byte(FD_SENSEI);
    if (result()!=2 || (ST0 & 0xE0) == 0x60)
        reset = 1;
    else
        recalibrate = 1;
}

/* recalibrate_floppy,
 * 向软驱下发校正命令和参数进行软盘校正,
 * 复位校正相关标志,设置软驱校正中断函数。*/
static void recalibrate_floppy(void)
{
    recalibrate = 0;
    current_track = 0;
    do_floppy = recal_interrupt;
    output_byte(FD_RECALIBRATE);
    output_byte(head<<2 | current_drive);
    if (reset)
        do_fd_request();
}

/* reset_interrupt,
 * 复位软盘中断C处理函数,在向软盘下发复位命令后,
 * 软盘接收命令并作复位操作后将引发软件中断从而
 * 调用本函数。*/
static void reset_interrupt(void)
{
    output_byte(FD_SENSEI);
    (void) result();
    output_byte(FD_SPECIFY);
    output_byte(cur_spec1); /* hut etc */
    output_byte(6); /* Head load time =6ms, DMA */
    do_fd_request();
}

/*
 * reset is done by pulling bit 2 of DOR low for a while.
 */
/* reset_floppy,
 * 下发软盘复位命令和参数到软盘以复位软盘控制器。
 * 软盘复位后将输出软盘中断。*/
static void reset_floppy(void)
{
    int i;

    reset = 0;
    cur_spec1 = -1;
    cur_rate = -1;
    recalibrate = 1;
    printk("Reset-floppy called\n\r");
    cli();
    do_floppy = reset_interrupt;
    outb_p(current_DOR & ~0x04,FD_DOR);
    for (i=0 ; i<100 ; i++)
        __asm__("nop");
    outb(current_DOR,FD_DOR);
    sti();
}

/* floppy_on_interrupt,
 * 软盘定时超时回调函数。*/
static void floppy_on_interrupt(void)
{
/* We cannot do a floppy-select, as that might sleep. We just force it */
    selected = 1;
    if (current_drive != (current_DOR & 3)) {
        current_DOR &= 0xFC;
        current_DOR |= current_drive;
        outb(current_DOR,FD_DOR);
        add_timer(2,&transfer);
    } else
        transfer();
}

/* do_fd_request,
 * 软盘(读写)请求函数。
 * 重置软盘或校正软盘,或启用定时器定时读写软盘。*/
void do_fd_request(void)
{
    unsigned int block;

    seek = 0;
    /* 软盘重置或校正标志置位则重置或校正软盘 */
    if (reset) {
        reset_floppy();
        return;
    }
    if (recalibrate) {
        recalibrate_floppy();
        return;
    }
    
    /* 检查当前对硬盘的请求是否合理 */
    INIT_REQUEST;

    /* 根据软盘次设备号映射到具体类型的软盘,
     * 若请求不合理则放弃当前请求并调度下一请
     * 求。若合理则将请求换算为软盘磁头,磁道,
     * 柱面,扇区等参数。*/
    floppy = (MINOR(CURRENT->dev)>>2) + floppy_type;
    if (current_drive != CURRENT_DEV)
        seek = 1;
    current_drive = CURRENT_DEV;
    block = CURRENT->sector;
    if (block+2 > floppy->size) {
        end_request(0);
        goto repeat;
    }
    sector = block % floppy->sect;
    block /= floppy->sect;
    head = block % floppy->head;
    track = block / floppy->head;
    seek_track = track << floppy->stretch;
    if (seek_track != current_track)
        seek = 1; /* 若当前磁道和欲访问磁道不同则置寻道标志 */
    sector++;
    if (CURRENT->cmd == READ)
        command = FD_READ;
    else if (CURRENT->cmd == WRITE)
        command = FD_WRITE;
    else
        panic("do_fd_request: unknown command");
    /* 访问软驱时,软驱启动并达设定转速需一定时间。ticks_to_floppy_on
     * 函数计算该事件,当定时器超时该事件时则调用floppy_on_interrupt
     * 调度软盘请求。add_timer和ticks_to_floppy_on在sched.c中,届时阅读。*/
    add_timer(ticks_to_floppy_on(current_drive),&floppy_on_interrupt);
}

/* floppy_init,
 * 设置软盘读写函数,
 * 在IDT[0x26]中设置软盘中断处理函数,设置PIC允许软盘中断IRQ6。*/
void floppy_init(void)
{
    blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
    set_trap_gate(0x26,&floppy_interrupt);
    outb(inb_p(0x21)&~0x40,0x21);
}
/* floppy.c读得比hd.c粗糙o... ^_^ */
