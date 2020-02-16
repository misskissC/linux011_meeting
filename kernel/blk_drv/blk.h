#ifndef _BLK_H
#define _BLK_H

#define NR_BLK_DEV 7
/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */
/* NR_REQUEST 是块设备请求队列大小。为保证读优先级,只使用队列前2/3用作
 * 写(和读)请求。
 *
 * 32是一个合理的值:大小适合电梯调度算法,只是在队列中的缓冲区块被锁住时
 * 显得有些不太够数。64似乎太大了一点,当有许多写/同步操作时容易造成读请
 * 求的暂停感。*/
#define NR_REQUEST 32

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and 'waiting' is used to wait for
 * read/write completion.
 */
/* 注,为了在实现页请求功能后也能用该结构体进行页请求,此文对该
 * 结构体类型做了扩展。当用于页请求时, 成员 bh 赋值为NULL, 成
 * 员 waiting 用于等待读/写操作完成。*/
struct request {
    int dev; /* -1 if no request */
    int cmd; /* READ or WRITE */
    int errors;
    unsigned long sector;     /* 当前设备分区中的当前扇区号 */
    unsigned long nr_sectors; /* 欲读/写扇区数 */
    char * buffer; /* 用于缓存访问设备数据的内存段 */
    struct task_struct * waiting; /* 用于进程等待当前请求元素 */
    struct buffer_head * bh; /* 管理缓冲区块的节点 */
    struct request * next;   /* 同一设备上的下一个请求 */
};

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */
/* 该宏由电梯算法调用。注,读操作比写操作优先级更高。
 * 读操作所体现出来的实时性(如需显示)比写操作高很多。*/
 
/* 在C语言中,算术运算符优先级高于逻辑运算符,
 * 逻辑与运算符的优先级高于逻辑或的优先级。
 *
 * IN_ORDER(s1, s2)宏代表的表达式相当于
 * ( (s1)->cmd < (s2)->cmd ) ||
 * ( (s1)->cmd==(s2)->cmd && (...) )
 * (...)中内容同外层。
 *
 * 在IN_ORDER(s1,s2)代表的表达式中,
 * 若已表达式最终值来代表s1和s2的优
 * 先级,如1代表s1的优先级大于s2优先级,
 * 则
 * cmd值(请求访问设备的操作:读/写..)
 * 越小优先级越高;
 * 在cmd相同时(如都为读),dev值(设备分
 * 区号)越小优先级越高;
 * 在dev相同时(如都为第2硬盘第2分区号)
 * sector(访问的起始扇区号)越小优先级越高。
 *
 * 在add_request中安排设备的请求调度时,并
 * 不是严格按照这个优先级安排的调度,而是按
 * 照电梯(升梯)算法安排设备请求调度,以减少
 * 磁盘在相邻请求间的大范围移动,从而节约一
 * 些磁盘请求时间。*/
#define IN_ORDER(s1,s2) \
((s1)->cmd<(s2)->cmd || (s1)->cmd==(s2)->cmd && \
((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector)))

/* struct blk_dev_struct,
 * 块设备当前(读写)请求结构体类型。*/
struct blk_dev_struct {
    void (*request_fn)(void); /* 指向块设备当前(读写)请求函数 */
    struct request * current_request; /* 块设备当前(读写)请求 */
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV];
extern struct request request[NR_REQUEST];
extern struct task_struct * wait_for_request;

#ifdef MAJOR_NR

/*
 * Add entries as needed. Currently the only block devices
 * supported are hard-disks and floppies.
 */
#if (MAJOR_NR == 1)
/* ram disk */
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST do_rd_request
#define DEVICE_NR(device) ((device) & 7)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == 2)
/* floppy */
#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ((device) & 3)
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device))
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == 3)
/* harddisk */
#define DEVICE_NAME "harddisk"
#define DEVICE_INTR do_hd
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (MINOR(device)/5)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif
/* unknown blk device */
#error "unknown blk device"

#endif

/* 主设备号MAJOR_NR块设备的当前请求及请求的设备分区号 */
#define CURRENT (blk_dev[MAJOR_NR].current_request)
#define CURRENT_DEV DEVICE_NR(CURRENT->dev)

#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
static void (DEVICE_REQUEST)(void);

/* unlock_buffer,
 * 复位bh所指缓冲区块节点并唤醒等在该缓冲区块节点的进程。*/
extern inline void unlock_buffer(struct buffer_head * bh)
{
    if (!bh->b_lock)
        printk(DEVICE_NAME ": free buffer being unlocked\n");
    bh->b_lock=0;
    wake_up(&bh->b_wait);
}

/* end_request,
 * 结束块设备当前请求,调用当前请求的下一个请求。
 * 
 * uptodate=0,块设备请求失败;
 * uptodate=1,块设备请求成功。
 * 
 * 结束块设备当前请求时,会复位缓冲区块锁状态,
 * 会置位缓冲区块读数据的状态;会唤醒等在当前请
 * 求请求元素的进程;会唤醒在等空闲请求元素的进程。*/
extern inline void end_request(int uptodate)
{
    /* 置缓冲区块数据是否已读标志,复位缓冲区块锁状态 */
    DEVICE_OFF(CURRENT->dev);
    if (CURRENT->bh) {
        CURRENT->bh->b_uptodate = uptodate;
        unlock_buffer(CURRENT->bh);
    }
    /* uptodate=0时,表示请求设备失败则提示 */
    if (!uptodate) {
        printk(DEVICE_NAME " I/O error\n\r");
        printk("dev %04x, block %d\n\r",CURRENT->dev,
            CURRENT->bh->b_blocknr);
    }
    /* 唤醒在等当前请求元素的进程;
     * 唤醒在等待空闲请求元素的进程;
     * 复位当前请求元素并调用下一个块设备请求。*/
    wake_up(&CURRENT->waiting);
    wake_up(&wait_for_request);
    CURRENT->dev = -1;
    CURRENT = CURRENT->next;
}

/* 检查当前请求是否为NULL,
 * 检查设备分区号对应的主设备号,
 * 请求读写设备前对应缓冲区块是否存在并已置位锁状态。*/
#define INIT_REQUEST \
repeat: \
    if (!CURRENT) \
        return; \
    if (MAJOR(CURRENT->dev) != MAJOR_NR) \
        panic(DEVICE_NAME ": request list destroyed"); \
    if (CURRENT->bh) { \
        if (!CURRENT->bh->b_lock) \
            panic(DEVICE_NAME ": block not locked"); \
    }

#endif

#endif
