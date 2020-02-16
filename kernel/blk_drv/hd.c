/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */
/* 此文包含了硬盘中断的底层代码。此文件函数接口将遍历请求列表,根据
 * 中断类型调用相应函数。因为所有的函数都将在中断中调用,所以这些函
 * 数中都不包含睡眠机制。在编写这些代码时,应考虑全面,特别谨慎。*/

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 3 /* 硬盘主设备号 */
#include "blk.h"

/* 70h端口用于接收CMOS RAM的内存地址,
 * 71h端口用于读写70h对应的内存单元。*/
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

/* Max read/write errors/sector */
#define MAX_ERRORS 7 /* 访问硬盘允许的最大次数 */
#define MAX_HD     2 /* 硬盘数 */

static void recal_intr(void);

/* 硬盘校正和复位标志 */
static int recalibrate = 1;
static int reset = 1;

/*
 *  This struct defines the HD's and their types.
 */
/* 各字段分别是磁头数、每磁道扇区数、柱面数(磁道数)、
 * 写前预补偿柱面号、磁头着陆区柱面号、控制字节。*/
/* struct hd_i_struct,
 * 硬盘参数结构体类型。*/
struct hd_i_struct {
    /* 磁头数;每磁道扇区数;磁道数;
     * 预补偿柱面号,...*/
    int head,sect,cyl,wpcom,lzone,ctl;
};
/* 硬盘参数结构体数组 */
#ifdef HD_TYPE /* 程序指定具体的硬盘参数(见config.h) */
struct hd_i_struct hd_info[] = { HD_TYPE };
#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct)))
#else /* 使用BIOS获取到的硬盘参数 */
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif

/* struct hd_struct,
 * 硬盘分区信息结构体类型及硬盘分区信息全局数组。*/
static struct hd_struct {
    long start_sect; /* 分区起始扇区号 */
    long nr_sects;   /* 分区总扇区数 */
} hd[5*MAX_HD]={{0,0},};
/* hd[0..4]用于第一个硬盘,
 * h[0]用于记录整个硬盘起始扇区和总扇区数,
 * h[1..4]用于记录硬盘各个分区的起始扇区和总扇区数;
 *
 * hd[5..9]用于第二个硬盘,
 * h[5]用于记录整个硬盘起始扇区和总扇区数,
 * h[6..9]用于记录硬盘各个分区的起始扇区和总扇区数。*/

/* port_read(port, buf, nr),
 * 从端口地址port处读取nr*2字节内容到buf内存段。
 * 
 * 内联汇编输入。
 * "d" (port), edx = port;
 * "D" (buf),  edi = buf;
 * "c" (nr),   ecx = nr;
 * 
 * 内联汇编指令。
 * cld;rep;insw 相当于
 * while (ecx--)
 *    inw es:edi,edx
 *    edi += 2
 * 即从端口地址port处读取ecx*2字节到buf内存段。*/
#define port_read(port,buf,nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr):"cx","di")

/* port_write(port,buf,nr),
 * 将buf内存段中的nr*2字节内容写往端口port。
 * 
 * 内联输入。
 * "d" (port)-端口存于edx寄存器中。
 * "S" (buf) - 将buf赋给esi寄存器。
 * "c" (nr) - 将2字节数存入cdx寄存器中。
 *
 * cld;rep;outsw指令:
 * while (ecx--)
 *  outw ds:si, edx 
 *  si += 2 */
#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr):"cx","si")

extern void hd_interrupt(void);
extern void rd_load(void);

/* This may be used only once, enforced by 'static int callable' */
/* [1] sys_setup,
 * 将硬盘参数信息保存到全局变量hd_info中,
 * 硬盘参数信息来自BIOS所指内存段或程序中额外指定(HD_TYPE)。
 *
 * 通过硬盘参数信息读取硬盘分区表信息于hd数组中,
 * 然后挂载根文件系统并加载虚拟硬盘。
 *
 * 根文件系统位于第二个硬盘的第一个分区中,分区设备号为0x306。*/
int sys_setup(void * BIOS)
{
    static int callable = 1;
    int i,drive;
    unsigned char cmos_disks;
    struct partition *p;
    struct buffer_head * bh;

    /* sys_setup只供调用一次即可 */
    if (!callable)
        return -1;
    callable = 0;

    /* 将硬盘参数信息保存到全局变量hd_info中 */
#ifndef HD_TYPE
    for (drive=0 ; drive<2 ; drive++) {
        hd_info[drive].cyl = *(unsigned short *) BIOS;
        hd_info[drive].head = *(unsigned char *) (2+BIOS);
        hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
        hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
        hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
        hd_info[drive].sect = *(unsigned char *) (14+BIOS);
        BIOS += 16;
    }
    /* bootsect.s曾检查第2个硬盘是否存在,
     * 不存在时其参数信息会被清0.*/
    if (hd_info[1].cyl)
        NR_HD=2;
    else
        NR_HD=1;
#endif
    for (i=0 ; i<NR_HD ; i++) {
        hd[i*5].start_sect = 0;
        hd[i*5].nr_sects = hd_info[i].head*
                hd_info[i].sect*hd_info[i].cyl;
    }

    /*
    We querry CMOS about hard disks : it could be that 
    we have a SCSI/ESDI/etc controller that is BIOS
    compatable with ST-506, and thus showing up in our
    BIOS table, but not register compatable, and therefore
    not present in CMOS.

    Furthurmore, we will assume that our ST-506 drives
    <if any> are the primary drives in the system, and 
    the ones reflected as drive 1 or 2.

    The first drive is stored in the high nibble of CMOS
    byte 0x12, the second in the low nibble.  This will be
    either a 4 bit drive type or 0xf indicating use byte 0x19 
    for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

    Needless to say, a non-zero value means we have 
    an AT controller hard disk for that drive.
    
    */

    /* 从CMOS中获取硬盘驱动器类型的信息。
     * CMOS RAM 0x12地址处的bit[7..4]不为0表含驱动器0,
     * CMOS RAM 0x12地址处的bit[3..0]不为0表含驱动器1。*/
    if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
        if (cmos_disks & 0x0f)
            NR_HD = 2; /* 在定义了HD_TYPE的情况下有编译错误吧 */
        else
            NR_HD = 1;
    else
        NR_HD = 0;
    /* 将驱动器不支持的硬盘的总信息清0 */
    for (i = NR_HD ; i < 2 ; i++) {
        hd[i*5].start_sect = 0;
        hd[i*5].nr_sects = 0;
    }
    /* 读取硬盘分区信息(被包含在引导区中)到hd数组中保存 */
    for (drive=0 ; drive<NR_HD ; drive++) {
        if (!(bh = bread(0x300 + drive*5,0))) {
            printk("Unable to read partition table of drive %d\n\r",
                drive);
            panic("");
        }
        if (bh->b_data[510] != 0x55 || (unsigned char)
            bh->b_data[511] != 0xAA) {
            printk("Bad partition table on drive %d\n\r",drive);
            panic("");
        }
        /* 硬盘分区信息开始于引导区的0x1BE偏移处,
         * 将硬盘各个分区信息依次读取到hd中。*/
        p = 0x1BE + (void *)bh->b_data;
        for (i=1;i<5;i++,p++) {
            hd[i+5*drive].start_sect = p->start_sect;
            hd[i+5*drive].nr_sects = p->nr_sects;
        }
        brelse(bh);
    }
    if (NR_HD)
        printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
    
    /* 加载软盘文件系统到虚拟硬盘内存中; 挂载根文件系统。*/
    rd_load();
    mount_root();
    return (0);
}

/* controller_redy,
 * 等待硬盘驱动器就绪。
 * 若硬盘驱动器就绪则返回非0值,否则返回0.*/
static int controller_ready(void)
{
    int retries=10000;
    
    /* 最多读10000次硬盘状态寄存器,若驱动器就绪则立即退出循环 */
    while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
    return (retries);
}

/* win_result,
 * 读硬盘主状态寄存器,看其是否处于就绪或寻道结束状态,
 * 若是则返回0,否则读取硬盘错误码后返回1。*/
static int win_result(void)
{
    int i=inb_p(HD_STATUS);

    if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
        == (READY_STAT | SEEK_STAT))
        return(0); /* ok */
    if (i&1) i=inb(HD_ERROR);
    return (1);
}

/* [4] hd_out,
 * 给drive对应硬盘下发读写命令并挂硬盘读写回调函数。
 *
 * drive=0,硬盘1;drive=1,硬盘2。
 * nsect,欲读写扇区数;sect,读写的起始扇区;head,读写的起始磁头;
 * cyl,读写的起始柱面;cmd,读写命令;intr_addr,硬盘读写中断处理函数。*/
static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
        unsigned int head,unsigned int cyl,unsigned int cmd,
        void (*intr_addr)(void))
{
    register int port asm("dx");

    /* 硬盘只有2个(0,1);硬盘的柱面数为15;
     * 检查硬盘控制器是否就绪。*/
    if (drive>1 || head>15)
        panic("Trying to write bad sector");
    if (!controller_ready())
        panic("HD controller not ready");

    /* 将硬盘读或写中断的C处理函数赋给do_hd,
     * 疑惑:在linux0.11中并没有看到do_hd的定义。*/
    do_hd = intr_addr;

/* AT硬盘控制器命令编程方法。
 * |--------------------------------------------
 * |0|1f7h|读检测控制器空闲状态controller_ready|
 * |--------------------------------------------
 * |1|3f6h|写硬盘控制寄存器********************|
 * |-------------------------------------------|
 * |2|1f1h|写预补偿起始柱面号******************|
 * |-------------------------------------------|
 * |3|1f2h|写扇区数量**************************|
 * |-------------------------------------------|
 * |4|1f3h|写(起始)扇区号**********************|
 * |-------------------------------------------|
 * |5|1f4h|写柱面号低8位***********************|
 * |-------------------------------------------|
 * |6|1f5h|写柱面号高2位***********************|
 * |-------------------------------------------|
 * |7|1f6h|写驱动号 磁头号*********************|
 * |-------------------------------------------|
 * |8|1f7h|写HDC命令码*************************|
 * |-------------------------------------------|
 * 写硬盘控制器和写预补偿起始柱面号用硬盘
 * 参数信息中对应字段即可。page420. */

    outb_p(hd_info[drive].ctl,HD_CMD);
    port=HD_DATA; /* 0x1f0 */
    outb_p(hd_info[drive].wpcom>>2,++port);
    outb_p(nsect,++port);
    outb_p(sect,++port);
    outb_p(cyl,++port);
    outb_p(cyl>>8,++port);
    outb_p(0xA0|(drive<<4)|head,++port);
    outb(cmd,++port); /* 如cmd=30h,写 */
}

/* drive_busy,
 * 多次获取硬盘主状态控制器的状态,
 * 若在规定次数中主状态控制器状态
 * 就绪则返回0,否则返回1表硬盘驱动器忙。*/
static int drive_busy(void)
{
    unsigned int i;

/* 见读0x1f7时硬盘主状态控制器位(hdreg.h中) */
    for (i = 0; i < 10000; i++)
        if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT)))
            break;
    i = inb(HD_STATUS);
    i &= BUSY_STAT | READY_STAT | SEEK_STAT;
    if (i == READY_STAT | SEEK_STAT)
        return(0);
    printk("HD controller times out\n\r");
    return(1);
}

/* reset_controller,
 * 复位硬盘驱动器。*/
static void reset_controller(void)
{
    int i;

    /* 置位硬盘复位并稍作等待 */
    outb(4,HD_CMD);
    for(i = 0; i < 100; i++) nop();

    /* 将硬盘1参数控制字节写入0x3f6指定硬盘运作模式 */
    outb(hd_info[0].ctl & 0x0f ,HD_CMD);

    /* 若硬盘忙或复位硬盘失败则提示 */
    if (drive_busy())
        printk("HD-controller still busy\n\r");
    if ((i = inb(HD_ERROR)) != 1)
        printk("HD-controller reset failed: %02x\n\r",i);
}

/* reset_hd,
 * 以指定硬盘参数表设置硬盘。*/
static void reset_hd(int nr)
{
    /* 复位硬盘驱动器,下发以硬盘参数表信息重新建立控制器参数HDC命令 */
    reset_controller();
    hd_out(nr,hd_info[nr].sect,hd_info[nr].sect,hd_info[nr].head-1,
        hd_info[nr].cyl,WIN_SPECIFY,&recal_intr);
}

void unexpected_hd_interrupt(void)
{
    printk("Unexpected HD interrupt\n\r");
}

/* bad_rw_intr,
 * 访问硬盘设备失败处理。
 * 当访问失败次数超最大允许次数时,则放弃当前请求并置位复位硬盘标志;
 * 若访问超过所允许最大次数一半时,则置位复位硬盘标志。*/
static void bad_rw_intr(void)
{
    if (++CURRENT->errors >= MAX_ERRORS)
        end_request(0);
    if (CURRENT->errors > MAX_ERRORS/2)
        reset = 1;
}

/* read_intr,
 * 读硬盘中断C处理函数。
 * 当前读块设备请求完成时调度下一请求,
 * 各请求以电梯升梯顺序形成链表,见add_request。
 *
 * 读硬盘请求->向硬盘下发读硬盘命令并设置do_hd指向写硬盘中断处理read_intr;
 * 硬盘可读时输出IRQ14中断从而让CPU执行IDT[0x2e]中的处理函数_hd_interrupt,
 * _hd_interrupt->do_hd即调用此处的read_intr。*/
static void read_intr(void)
{
    /* 检查硬盘状态,若硬盘不正常则记录失败次数
     * 并进行相应设置,然后继续调度硬盘访问请求。*/
    if (win_result()) {
        bad_rw_intr();
        do_hd_request();
        return;
    }
    /* 从硬盘数据寄存器中读取一扇区内容到buffer中,
     * 读成功后复位读设备失败次数,存储硬盘数据缓冲
     * 区后移一扇区大小,更新当前所在扇区以及未读扇
     * 区数。当未读扇区数不为0时,中断请求函数仍设置
     * 为硬盘中断读函数并返回。*/
    port_read(HD_DATA,CURRENT->buffer,256);
    CURRENT->errors = 0;
    CURRENT->buffer += 512;
    CURRENT->sector++;
    if (--CURRENT->nr_sectors) {
        do_hd = &read_intr;
        return;
    }
    /* 执行到这里时,当前请求未读扇区已为0,
     * 所以正常结束当前请求,并设置当前块设备
     * 请求指向下一请求并调用do_hd_request调
     * 度块设备下一请求。*/
    end_request(1);
    do_hd_request();
}

/* write_intr,
 * 写硬盘中断C处理函数。
 * 当前写块设备请求完成时调度下一请求,
 * 各请求以电梯升梯顺序形成链表,见add_request。
 *
 * 写硬盘请求->向硬盘下发写硬盘命令并设置do_hd指向写硬盘中断处理write_intr;
 * 硬盘可写时输出IRQ14中断从而让CPU执行IDT[0x2e]中的处理函数_hd_interrupt,
 * _hd_interrupt->do_hd即调用此处的write_intr完成一次写操作。*/
static void write_intr(void)
{
    /* 检查硬盘状态,若硬盘不正常则记录失败次数
     * 并进行相应设置,然后继续调度硬盘访问请求。*/
    if (win_result()) {
        bad_rw_intr();
        do_hd_request();
        return;
    }

    /* 因为在do_hd_request中已写过一扇区,所以此处
     * 先将未写扇区数减1后再判断是否为0。若不为0则
     * 更新已读硬盘当前所在扇区号,更新缓冲区块位置,
     * 继续设置do_hd为写中断处理函数write_intr,并再
     * 写块设备一扇区内容并返回。*/
    if (--CURRENT->nr_sectors) {
        CURRENT->sector++;
        CURRENT->buffer += 512;
        do_hd = &write_intr;
        port_write(HD_DATA,CURRENT->buffer,256);
        return;
    }
    /* 执行到此处表明当前写硬盘请求已完成,
     * 所以正常结束当前写块设备请求并调整当
     * 前硬盘请求指向下一请求;然后调用do_hd_request继续调度。*/
    end_request(1);
    do_hd_request();
}

/* recal_intr,
 * 判断硬盘状态并记录硬盘状态非
 * 就绪次数,若没有超过设定值则重
 * 新调度当前请求,若超过设定次数
 * 则复位硬盘或放弃当前请求继续调
 * 度硬盘下一请求。*/
static void recal_intr(void)
{
    /* 判断硬盘状态,若状态不可访问
     * 则进入bad_rw_intr中记录,若记
     * 录次数超过指定数bad_rw_intr会
     * 调用end_request(0)放弃当前请求
     * 而设置当前请求指向下一请求或置
     * 位硬盘复位标志。*/
    if (win_result())
        bad_rw_intr();
    /* 调度行为取决于bad_rw_intr中的设
     * 置,若bad_rw_intr置位硬盘复位标志,
     * 则do_hd_request将执行硬盘复位函数等,
     * 否则调度当前请求继续请求硬盘。*/
    do_hd_request();
}

/* [3] do_hd_request,
 * 硬盘读写请求函数,向硬盘下发读写命令及相关信息。
 * 硬盘成功收到该命令并准备好后就会向PIC输出中断
 * 信息,从而让CPU执行IDT[2eh]中的硬盘中断处理函数
 * _hd_interrupt,该函数会调用通过hd_out挂载在do_hd
 * 上的硬盘中断C处理函数,如read_intr, write_intr,
 * recal_intr。_hd_interrupt定义在kernel/system_call.s中。*/
void do_hd_request(void)
{
    int i,r;
    unsigned int block,dev;
    unsigned int sec,head,cyl;
    unsigned int nsect;

    /* 检查当前对硬盘的请求是否合理 */
    INIT_REQUEST;

    /* 获取次设备号和预访问设备的起始扇区号,
     * 并判断他们是否在合理范围内,若不合理则
     * 放弃当前请求并调度下一请求。*/
    dev = MINOR(CURRENT->dev);
    block = CURRENT->sector;
    if (dev >= 5*NR_HD || block+2 > hd[dev].nr_sects) {
        end_request(0);
        goto repeat;
    }
    /* 获取基于整个硬盘的分区号,
     * dev/5计算得到硬盘0还是硬盘1。*/
    block += hd[dev].start_sect;
    dev /= 5;
    /* 计算逻辑扇区号block对应的磁道和在该磁道上的扇区号,
     * 计算结果为block=磁道号,sect=在block磁道上的扇区号。
     *
     * 计算磁道号block对应的柱面号和磁头号,
     * 计算结果为cyl=柱面号,head=磁头号。*/
    __asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),
        "r" (hd_info[dev].sect));
    __asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
        "r" (hd_info[dev].head));
    sec++; /* 扇区号从1开始 */
    nsect = CURRENT->nr_sectors; /* 获取欲读写扇区数 */

    /* 若硬盘重置信号置位则重置硬盘,
     * 并将硬盘重新校正置位。*/
    if (reset) {
        reset = 0;
        recalibrate = 1;
        reset_hd(CURRENT_DEV);
        return;
    }
    /* 若硬盘校正标志置位则校正硬盘 */
    if (recalibrate) {
        recalibrate = 0;
        hd_out(dev,hd_info[CURRENT_DEV].sect,0,0,0,
            WIN_RESTORE,&recal_intr);
        return;
    }
    /* 若当前请求是请求写设备, */
    if (CURRENT->cmd == WRITE) {
        /* 则向硬盘下发写命令块并传入写硬盘中断的C处理回调函数write_intr */
        hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
        /* 检查向硬盘下发的写命令是否成功,若失败则回到INIT_REQUEST中repeat处 */
        for(i=0 ; i<3000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)
            /* nothing */ ;
        if (!r) {
            bad_rw_intr();
            goto repeat;
        }
        /* 若向硬盘下发的写命令成功,则先写入一扇区数据 */
        port_write(HD_DATA,CURRENT->buffer,256);

    /* 若当前请求是请求读设备, */
    } else if (CURRENT->cmd == READ) {
        /* 则向硬盘下发读命令块并传入读硬盘中断的C处理函数read_intr */
        hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
    } else
        panic("unknown hd-command");
}

/* [2] hd_init,
 * 设置硬盘的读写请求函数,
 * 设置硬盘中断处理函数,使能硬盘中断。*/
void hd_init(void)
{
    /* MAJOR_NR, 硬盘主设备号为3,
     * 为块设备硬盘挂载(读写)请求函数DEVICE_REQUEST。
     * 在块设备硬盘中,读写硬盘的请求函数DEVICE_REQUEST
     * 为do_hd_request函数。*/
    blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;

    /* 在IDT[0x2E]中设定硬盘中断处理函数,
     * 8259A-2 IRQ7对应硬盘中断,IRQ7中断
     * 号在setup.s中被设置为0x2e。*/
    set_intr_gate(0x2E,&hd_interrupt);

    /* 设置主片8259A-1接收从片8259A-2中断,
     * 设置使能从片8259A-2 IRQ7即硬盘中断。*/
    outb_p(inb_p(0x21)&0xfb,0x21);
    outb(inb_p(0xA1)&0xbf,0xA1);
}

/* 粗略总结读写硬盘请求的大体流程,
 * 文件内容转换到硬盘逻辑块,
 * 硬盘逻辑块内容转换为硬盘设备请求,
 * 根据硬盘设备请求向硬盘发起请求命令,
 * 硬盘收到命令并就绪后向PIC输出中断,
 * CPU执行硬盘中断处理函数完成硬盘访问请求并调度硬盘设备下一请求。
 *
 * 以函数描述这个过程的大体流程
 * 文件操作系统调用(如open,read,write,sys_read,sys_write),
 * 文件内容到硬盘逻辑块的转换(如block_read,breada),
 * 硬盘逻辑块访问转换为一个硬盘请求(make_request,add_request),
 * 调度硬盘访问请求向硬盘发起访问请求并设置硬盘中断函数
 * (如do_hd_request,hd_out,do_hd),
 * 硬盘收到访问命令并就绪后则发起硬盘访问中断从而让CPU执行硬盘
 * 中断函数do_hd以完成硬盘访问。
 *
 * read -> sys_read -> block_read -> make_request -> add_request
 * -> do_hd_request -> hd_out... -> do_hd(do_hd_request) */
