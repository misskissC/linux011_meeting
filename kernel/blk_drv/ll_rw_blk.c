/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
/* 本文件包含处理块设备读/写请求代码 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
/* 管理块设备读写请求结构体类型的全局数组,struct request
 * 结构体类型中包含了加载 nr 扇区到内存的必要成员。*/
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */
/* 用于进程等待空闲的 request 结构体元素 */
/* 在块设备请求数组request无空闲元素时,
 * 用于等待直到request中出现空闲元素。*/
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *  do_request-address
 *  next-request
 */
 /* 块设备(读写)请求结构体数组,
  * 目前只将软盘(2)和硬盘(3)当做块设备管理。
  * 他们分别在floppy.c和hd.c中被赋值。*/
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
    { NULL, NULL }, /* no_dev */
    { NULL, NULL }, /* dev mem */
    { NULL, NULL }, /* dev fd */
    { NULL, NULL }, /* dev hd */
    { NULL, NULL }, /* dev ttyx */
    { NULL, NULL }, /* dev tty */
    { NULL, NULL }  /* dev lp */
};

/* lock_buffer,
 * 为bh所指管理缓冲区块的节点置位锁状态,
 * 间接为bh所管理的缓冲区块上锁。*/
static inline void lock_buffer(struct buffer_head * bh)
{
/* 原理同lock_super,除之前描述,再额外粗略理解一点吧^_^。
 *
 * 在为某共享内存上锁过程中禁止CPU处理中断是为了防止中断程序
 * 与当前进程冲突访问bh指向节点的b_lock成员。一旦确保b_lock
 * 成员的互斥访问后,只要在访问bh所指节点其他成员前"判断该节点
 * 锁状态是否已置位,若置位则等待"的约定,就可以实现共享内存段的
 * 互斥访问了。这是只有在写b_lock成员时才禁止CPU处理中断的原因。*/
    cli();
    while (bh->b_lock)
        sleep_on(&bh->b_wait);
    bh->b_lock=1;
    sti();
}

/* unlock_buffer,
 * 为bh所指管理缓冲区块的节点复位锁状态。*/
static inline void unlock_buffer(struct buffer_head * bh)
{
    if (!bh->b_lock)
        printk("ll_rw_block.c: buffer not locked\n\r");

    /* 按照锁状态访问共享内存的约定,
     * 在本进程获取到锁后其余进程或中断就不会写访问bh中的成员,
     * 所以此处不用再禁止CPU处理本进程中断。*/
    bh->b_lock = 0;

    /* 唤醒调用sleep_on(&bh->b_wait)进入睡眠的进程们 */
    wake_up(&bh->b_wait);
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */
/* add_request,添加一个请求到请求链表中。
 * 该函数失能中断是为了能正确地添加请求(避免中断程序的竞争)。*/
/* add_request,
 * 以电梯升梯调度算法管理调度某个块设备的读写请求。
 * 若设备当前无其他请求,则直接用当前设备读写请求函数(dev->request_fn)读写设备。
 * 若设备已有其他请求,则以电梯升梯算法将当前读写请求加入到该设备已有请求的链表
 * 中,以待调度执行。*/
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
    struct request * tmp;

    req->next = NULL;
    cli(); /* 禁止中断 */
    /* 无中断+内核无抢占模式 将使得从此处到sti()之间的程序会一直执行 */
    if (req->bh)
        req->bh->b_dirt = 0;
    if (!(tmp = dev->current_request)) {
        /* 若设备无其他请求则将当前请求设置为req指向的请求,并调
         * 用挂载在request_fn上的回调函数读写设备。该函数将会向
         * 对应的设备下发读写命令,对应设备收到读写命令后,在准备
         * 好被读或被写的相关状态时会就向CPU申请被读写的中断,从
         * 而让CPU指向对应的中断函数以完成设备读写。*/
        dev->current_request = req;
        sti();
        (dev->request_fn)();
        return;
    }
    /* 若设备有其他请求,则按照电梯升梯算
     * 法将req指向的请求加入到该设备的所
     * 有请求中形成一个各楼层等待乘梯上楼
     * 式的链表。*/
    for ( ; tmp->next ; tmp=tmp->next)
        /* 以IN_ORDER指定优先级判定,若新
         * 加入的req请求的优先级比tmp请求
         * 优先级低,则以优先级降序顺序寻找
         * req指向请求的位置,即满足优先级 
         * tmp > req > tmp->next。
         * 
         * 若新加入的req请求优先级高于或等于
         * tmp请求,则req会加入到优先级都比tmp
         * 请求高的链表序列中,在这个序列中仍
         * 以IN_ORDER所指定优先级的降序方式将
         * req插入其中,待tmp所在序列中的所有序
         * 列都调度完毕后再调度比tmp优先级高的
         * 序列。这就像电梯到了3楼(tmp),所有高
         * 于3楼的要乘梯继续往上的都可以乘梯,所
         * 有低于3楼的要乘梯往上的只有当电梯重新
         * 回到有人往上的最低楼层后才又逐层往上走。
         * 
         * 使用电梯调度算法是为了在调度各读写磁盘的
         * 请求时,每次都尽可能少地移动磁盘(磁头等)。
         * linux0.11只使用了电梯升梯调度算法。*/
        if ((IN_ORDER(tmp,req) ||
            !IN_ORDER(tmp,tmp->next)) &&
            IN_ORDER(req,tmp->next))
            break;
    /* 将新请求插入到链表中的合适位置以待调度请求读写设备 */
    req->next=tmp->next;
    tmp->next=req;
    sti();
}

/* make_request,
 * 用描述各类请求的结构体数组元素request管理major对应块设备的读写请求。
 * 即将bh中携带的块设备读写信息拷贝到request元素中,好专门管理。
 *
 * rw=读或写请求时,make_request会不惜睡眠等待可用的请求数组元素来记录
 * 读写请求;rw=欲读或预写时,遇到需睡眠等待的情形则放弃本次请求。*/
static void make_request(int major,int rw, struct buffer_head * bh)
{
    struct request * req;
    int rw_ahead;

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */
    /* 当参数rw为预写/预读标志时,若此时缓冲区锁状态置位,则放弃写/读,
     * 否则按照正常的写/读操作来写/读对应设备。*/
    if (rw_ahead = (rw == READA || rw == WRITEA)) {
        if (bh->b_lock)
            return;
        if (rw == READA)
            rw = READ;
        else
            rw = WRITE;
    }
    if (rw!=READ && rw!=WRITE)
        panic("Bad block dev command, must be R/W/RA/WA");

    /* 置位bh所指缓冲区块的锁状态;
     * 写设备时,若缓冲区块中的数据还未准备好则复位缓冲区块锁状态并返回;
     * 读设备时,若数据已读入缓冲区块中则复位缓冲区块锁状态并返回。*/
    lock_buffer(bh);
    if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
        unlock_buffer(bh);
        return;
    }
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */
/* 对于设备读写请求的设计是,读请求的优先级会更高一些,全局数组requests
 * 最后三分之一专用于读请求,剩余三分之二用于读写请求。*/
    if (rw == READ)
        req = request+NR_REQUEST;
    else
        req = request+((NR_REQUEST*2)/3);
/* find an empty request */
    while (--req >= request)
        if (req->dev<0)
            break;
/* if none found, sleep on new requests: check for rw_ahead */
    /* 如果管理设备读写请求的数组中已无空闲元素时,若是预读则
     * 直接返回,否则睡眠等待直到有某个元素被释放时重新遍历。*/
    if (req < request) {
        if (rw_ahead) {
            unlock_buffer(bh);
            return;
        }
        sleep_on(&wait_for_request);
        goto repeat;
    }
/* fill up the request-info, and add it to the queue */
/* 在遍历到请求全局数组元素后,从bh所指管理缓冲区块的节点
 * 中将请求设备的详细信息拷贝到所遍历到的请求数组元素中,
 * 以完成缓冲区块节点结构体向请求结构体类型的过渡。*/
    req->dev = bh->b_dev;
    req->cmd = rw;
    req->errors=0;
    req->sector = bh->b_blocknr<<1; /* 2扇区为一逻辑块 */
    req->nr_sectors = 2; /* 读写两扇区即一个逻辑块 */
    req->buffer = bh->b_data;
    req->waiting = NULL;
    req->bh = bh;
    req->next = NULL;
    add_request(major+blk_dev,req);
}

/* ll_rw_block,
 * 请求读或写块设备的底层函数。
 * rw参数为读(预读)或写(预写)设备标识,
 * bh所指管理缓冲区块的节点中包含了欲读写设备的详细信
 * 息,如设备分区号,欲读写逻辑块号,欲读扇区数等。参见
 * struct buffer_head结构体类型。*/
void ll_rw_block(int rw, struct buffer_head * bh)
{
    unsigned int major;

    /* 从bh所指管理缓冲区块的节点中获取主设备号,
     * 并判断该主设备号和其对应的读写设备函数是否存在。*/
    if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
    !(blk_dev[major].request_fn)) {
        printk("Trying to read nonexistent block-device\n\r");
        return;
    }
    
    /* 请求读写块设备,主设备号major用于映射其对应的读写函数 */
    make_request(major,rw,bh);
}

/* blk_dev_init,
 * 初始化管理块设备读写请求的全局数组。*/
void blk_dev_init(void)
{
    int i;

    for (i=0 ; i<NR_REQUEST ; i++) {
        request[i].dev = -1;
        request[i].next = NULL;
    }
}
/* 粗略总结块设备请求管理。
 *       |--------------|
 *       | file request |
 *       |--------------|
 *              ∧
 *              | |------------------------------------|
 *              | |filesystem structure buffer(mapping)|
 *              | |------------------------------------|
 *              V
 * |----------------------------|
 * |block device request(manage)|
 * |----------------------------|
 *              ∧
 *              | |------------------------|
 *              | |file data buffer(manage)|
 *              | |------------------------|
 *              V
 *  ..............................
 *  . logical filesystem mapping .
 *  ..............................
 *              ∧
 *              | I/O指令和中断机制
 *              V
 *         |----------|
 *         | controler|
 *         |——————————|
 *         | physical |
 *         |  device  |
 *         |——————————| */

