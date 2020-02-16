/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */
/* buffer.c 实现了外设缓存接口函数。为避免冲突使用buffer,会用竞争条件避免中断
 * 改变管理buffer的数据结构体。注,因为中断可能会唤醒一个进程,所以在睡眠等待接
 * 口或指令段需加上cli-sti指令以禁止CPU处理当前进程中断。我希望被加cli-sti代码
 * 段能尽快被执行完。
 *
 * 注,这里有一个与本文件主题不相关的函数:检查软盘状态是否改变。因为当软盘不存
 * 在时应理解释放软盘缓冲区,所以我觉得把他放在本文件中最为合适。*/

#include <stdarg.h>
 
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>

/* 链接器预定义变量。
 * end变量在(操作系统内核)程序最后,
 * 其地址可代表操作系统程序的结束地址。*/
extern int end;

/* 操作系统程序结束处为buffer开始地址,
 * struct buffer_head 定义在include/linux/fs.h中。*/
struct buffer_head * start_buffer = (struct buffer_head *) &end;

/* hash指针数组, 每一个hash指针元素指向一个
 * 节点类型为(struct buffer_head *)的双向循环链表。
 * hash_table[0] --> free_list + i <--> free_list + j ...
 * ...
 * hash_table[NR_HASH - 1] --> free_list + m <--> free_list + k ...
 * 每一个hash_table各元素指向由哪些节点构成的双向循环链表取决于
 * 节点与hash_table之间如何映射(见 _hashfun)。*/
struct buffer_head * hash_table[NR_HASH];

/* 指向管理buffer双向链表的头节点 */
static struct buffer_head * free_list;

/* 用于当前任务等待缓冲区块使用,
 * 其等待机制需结合任务管理的sleep_on和wake_up两个函数进行理解。
 * struct task_struct 定义在include/linux/sched.h中,
 * 是用来管理进程的结构体。*/
static struct task_struct * buffer_wait = NULL;

/* linux0.11将buffer分成1Kb大小的缓冲区块,
 * NR_BUFFERS用于记录缓冲区块数。*/
int NR_BUFFERS = 0;

/* [7] wait_on_buffer,
 * 等待缓冲区管理节点bh被解锁。*/
static inline void wait_on_buffer(struct buffer_head * bh)
{
/* 在等待bh所指缓冲区块锁状态复位的过程中,
 * 本进程可能会进入睡眠状态(TASK_UNINTERRUPTIBLE)
 * 即不可中断状态,所以需要关闭本进程响应中断。*/

    cli();
    while (bh->b_lock)
        /* sleep_on,
         * 置本进程状态为不可运行状态,
         * 调用进程调度函数调度其他进程运行。
         * 
         * 在其他进程运行过程中,直到有进程调
         * 用wake_up(&bh->b_wait)时才会唤醒
         * 本进程在sleep_on中的睡眠(阻塞),因为
         * 本进程还处在内核态又禁止了中断响应,
         * 所以本进程会一直往后执行,直到本进程
         * 再显式调用进程调度函数或从内核返回到
         * 用户态时或在用户态发生诸定时器中断时
         * 才可能(当前进程时间片完)会进行进程切换。
         *
         * 所以while(bh->b_lock)的执行过程中,没有
         * 其他进程或中断程序参与,即没有bh所指缓冲
         * 区块被冲突访问的风险。*/
        sleep_on(&bh->b_wait);
    /* 使用while的原因是可能有多个进程在等待bh所指
     * 缓冲区块,在其他进程中调用wake_up(&bh->b_wait)
     * 唤醒等待bh所指缓冲区块的进程为可运行状态后,谁
     * 先被调度是进程的时间片决定的,若是另一个等待
     * bh所指缓冲区块的进程的时间片更大即先被调度而给
     * bh所指超级块的锁状态置了位又因等待其他资源而
     * 显式调用进程调度函数而切换到本进程运行从sleep_on
     * 返回时,因bh所指缓冲区块锁状态呈置位状态,所以需继
     * 续睡眠等待。*/
    sti();
}

/* [8] sys_sync,
 * 将内存中修改过的i节点同步到缓冲区中,
 * 将所有被改写过的缓冲区块内容同步到相应设备中。
 * 
 * 根据个人阅读buffer.c源码, [1] - [7]有一定的先后顺序,
 * 自[8]开始, 所有函数的阅读仅跟在源文件中定义位置有关了。*/
int sys_sync(void)
{
    int i;
    struct buffer_head * bh;

    /* 将内存中修改过的i节点(文件相关)同步到相应缓冲区中。
     * sync_inodes定义在fs/inodes.c中,
     * 可了解文件系统后再阅读该函数。*/
    sync_inodes();      /* write out inodes into buffers */

    /* 将所有被改写过的缓冲区块的内容同步到相应设备中 */
    bh = start_buffer;
    for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
        wait_on_buffer(bh);
        if (bh->b_dirt)
            ll_rw_block(WRITE,bh);
    }
    return 0;
}

/* [9] sync_dev,
 * 同步指定设备dev的i节点和缓冲区块内容。*/
int sync_dev(int dev)
{
    int i;
    struct buffer_head * bh;

    /* 将dev被修改缓冲区块的内容同步到设备dev中 */
    bh = start_buffer;
    for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
    if (bh->b_dev != dev)
        continue;
    wait_on_buffer(bh);
    if (bh->b_dev == dev && bh->b_dirt)
        ll_rw_block(WRITE,bh);
    }

    /* 将内存中修改过的i节点同步到缓冲区中,
     * 再将缓冲区的i节点同步到dev外设中。*/
    sync_inodes();
    bh = start_buffer;
    for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
    if (bh->b_dev != dev) /* 仅同步dev设备 */
        continue;
    wait_on_buffer(bh);
    if (bh->b_dev == dev && bh->b_dirt)
        ll_rw_block(WRITE,bh);
    }
    return 0;
}

/* [10] invalidate_buffers,
 * 将设备dev缓冲区块的数据有效状态复位。*/
void inline invalidate_buffers(int dev)
{
    int i;
    struct buffer_head * bh;

    bh = start_buffer;
    for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
    if (bh->b_dev != dev)
        continue;
    wait_on_buffer(bh);
    if (bh->b_dev == dev)
        /* 复位缓冲区块的数据状态, 即数据无效也没有被修改过 */
        bh->b_uptodate = bh->b_dirt = 0;
    }
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 */
/* [11] check_disk_change,
 * 检查软盘是否已无效, 若无效则清理其所占有的缓冲区块,
 * 包括i节点和数据的缓冲区块。*/
void check_disk_change(int dev)
{
    int i;

    /* 检查dev是否为软盘,
     * 软盘的主设备号为2, 硬盘为3,
     * 可在外设管理程序进一步了解。
     * MAJOR宏在include/linux/fs.h中定义。*/
    if (MAJOR(dev) != 2)
        return;
    /* 检查软盘是否拔出, 若没有拔出则返回。
     * floppy_change定义在kernel/blk_dev/floppy.c中。*/
    if (!floppy_change(dev & 0x03))
        return;

    /* 若软盘已拔出, 则释放i节点位图等数据结构所占缓冲区,
     * super_block是跟文件系统相关的数据结构,
     * 可在阅读文件系统时再了解。*/
    for (i=0 ; i<NR_SUPER ; i++)
        if (super_block[i].s_dev == dev)
            put_super(super_block[i].s_dev);
        
    /* 释放软盘设备dev i节点和数据所占缓冲区块 */
    invalidate_inodes(dev);
    invalidate_buffers(dev);
}

/* 管理缓冲区块节点与hash_table之间的映射关系。
 * 由节点成员设备分区号dev和数据块号block与hash_table产生映射关系,
 * dev ^ block结果相同的节点将被映射到同一个hash队列(双向循环链表)中。
 * 
 * 0x301 ^ 0x000 = 0x301  0x302 ^ 0x000 = 0x302  ...
 * 0x301 ^ 0x001 = 0x300  0x302 ^ 0x001 = 0x303  ...
 * 0x301 ^ 0x002 = 0x303  0x302 ^ 0x002 = 0x300  ...
 * 0x301 ^ 0x003 = 0x302  0x302 ^ 0x003 = 0x301  ...
 * 0x301 ^ 0x004 = 0x305  0x302 ^ 0x004 = 0x306  ...
 * 0x301 ^ 0x005 = 0x304  0x302 ^ 0x005 = 0x307  ...
 * 0x301 ^ 0x006 = 0x307  0x302 ^ 0x006 = 0x304  ...
 * 0x301 ^ 0x007 = 0x306  0x302 ^ 0x007 = 0x305  ...
 * 0x301 ^ 0x008 = 0x309  0x302 ^ 0x008 = 0x30a  ...
 * 0x301 ^ 0x009 = 0x308  0x302 ^ 0x009 = 0x30b  ...
 * 0x301 ^ 0x00a = 0x30b  0x302 ^ 0x00a = 0x308  ...
 *
 * 第1个硬盘第1分区的数据块0和第1个硬盘第2分区的数据块2将
 * 在同一个hash队列中, 该队列由hash_table[0x300 % NR_HASH]指向。
 * ......
 *
 * 为加快搜索速度, hash_table相当于缓冲区管理的缓存管理, 
 * hash队列数NR_HASH可根据系统复杂度调整。*/
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
#define hash(dev,block) hash_table[_hashfn(dev,block)]

/* [12] remove_from_queues,
 * 将缓冲区管理节点bh从其hash队列和其双向循环链表中移除。
 *
 * 该函数可以插入到顺序[6]阅读。*/
static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
    /* 让bh hash队列中的下一个节点指向bh的上一个节点 */
    if (bh->b_next)
        bh->b_next->b_prev = bh->b_prev;
    /* 让bh hash队列中的上一个节点指向bh的下一个节点 */
    if (bh->b_prev)
        bh->b_prev->b_next = bh->b_next;
    /* hash队列头指向bh的下一个节点 */
    if (hash(bh->b_dev,bh->b_blocknr) == bh)
        hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
    
/* remove from free list */
    if (!(bh->b_prev_free) || !(bh->b_next_free))
        panic("Free block list corrupted");

    /* 让bh 上一个链表指向bh的下一个节点,
     * 让bh 下一个节点指向bh的上一个节点。
     * 若bh为头节点, 则让头指针free_list指向bh下一个节点。*/
    bh->b_prev_free->b_next_free = bh->b_next_free;
    bh->b_next_free->b_prev_free = bh->b_prev_free;
    if (free_list == bh)
        free_list = bh->b_next_free;
}

/* [4] insert_into_queues,
 * 将指向相应缓冲区块节点bh加到管理缓冲区的双向循环链表的末尾,
 * 若该节点已关联设备, 则将其加入到hash队列中。*/
static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
    bh->b_next_free = free_list;
    bh->b_prev_free = free_list->b_prev_free;
    free_list->b_prev_free->b_next_free = bh;
    free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
    bh->b_prev = NULL;
    bh->b_next = NULL;
    if (!bh->b_dev)
        return;
    
    /* 将bh插入到hash(bh->b_dev, bh->blocknr)队列的头部。*/
    bh->b_next = hash(bh->b_dev,bh->b_blocknr);
    hash(bh->b_dev,bh->b_blocknr) = bh;
    bh->b_next->b_prev = bh;
}

/* [6] find_buffer,
 * 根据hash映射关系, 在dev和block对应的hash队列中遍历,
 * 看其缓冲区块的管理节点是否在其hash队列中。
 * 若在其中则返回该节点地址, 否则返回NULL。*/
static struct buffer_head * find_buffer(int dev, int block)
{
    struct buffer_head * tmp;

    /* 在dev和block对应的hash队列中遍历, 看其缓冲区块的管理节点
     * 是否在其hash队列中。若在其中则返回该节点地址, 否则返回NULL。*/
    for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
        if (tmp->b_dev==dev && tmp->b_blocknr==block)
            return tmp;
    return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
/* [5] get_hash_table,
 * 在dev&&block对应hash队列中查找是否有dev&&block对应的缓冲区管理节点,
 * 若有则返回该缓冲区块管理节点, 否则返回NULL。*/
struct buffer_head * get_hash_table(int dev, int block)
{
    struct buffer_head * bh;

    for (;;) {
        /* 在dev&&block对应的hash队列中寻找
         * dev && block对应的缓冲区管理节点。*/
        if (!(bh=find_buffer(dev,block)))
            return NULL;
        /* 若在hash队列中找到其缓冲区管理节点,
         * 则增加缓冲区节点的引用计数,
         * 并等待缓冲区块解锁。*/
        bh->b_count++;
        wait_on_buffer(bh);
        /* 经睡眠等待该缓冲区块管理节点后,
         * 再次检查该缓冲区块的设备号和数据块,
         * 若没有被其它任务修改为其他设备或数据块的缓冲区则返回管理节点,
         * 否则表明该缓冲区块竞争失败, 则需减少该缓冲区块的引用计数。*/
        if (bh->b_dev == dev && bh->b_blocknr == block)
            return bh;
        bh->b_count--;
    }
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)
/* [3] getblk,
 * 根据设备号dev和数据块号block获取一块空闲的缓冲区块。*/
struct buffer_head * getblk(int dev,int block)
{
    struct buffer_head * tmp, * bh;

repeat:
    /* 从dev&&block所映射的hash队列中查找
     * dev&&block缓冲区块的管理节点。*/
    if (bh = get_hash_table(dev,block))
        return bh;
    
    /* 若在hash数组管理的队列中没有找到缓冲区块,
     * 则在双向循环链表中查找一空闲缓冲区块。*/
    tmp = free_list;
    do {
        /* 缓冲区块引用计数不为0则表示其正在被引用 */
        if (tmp->b_count)
            continue;

        /* 用bh指向引用计数为0, 且BADNESS值最小的节点,
         * 引用计数为0时, b_dirt和block即将被原占有者(进程)清0。*/
        if (!bh || BADNESS(tmp)<BADNESS(bh)) {
            bh = tmp;
            
            /* 若当前节点指向的缓冲区块的BADNESS值为0,
             * 即b_dirt(未被修改)和block(未上锁)成员皆为0的节点则结束遍历,
             * 这个状态表明此时bh所指向的缓冲区块完全空闲。*/
            if (!BADNESS(tmp))
                break;
        }
/* and repeat until we find something good */
    /* 从双向循环链表头节点开始遍历,
     * 若再遍历到头节点也没有找到指向空闲缓冲区块的节点则结束此次遍历。*/
    }while ((tmp = tmp->b_next_free) != free_list);
    /* 若遍历完管理缓冲区块的双向链表
     * 也没有找到一个引用计数为0的缓冲区块,
     * 则让当前进程睡眠等待(进入阻塞状态),
     * 直到在其它进程释放缓冲区块后再wake_up(&buffer_wait)唤醒,
     * 然后再重新从repeat处开始试图获取一个指向引用计数为0缓冲区块的节点。
     *
     * sleep_on和wake_up是多任务管理(kernel/sched.c)中的函数,
     * 若跳转阅读二者源码时觉得难以理解则可暂先按照以上描述粗略理解其功能,
     * 待阅读多任务管理时再与其周旋。*/
    if (!bh) {
        sleep_on(&buffer_wait);
        goto repeat;
    }

    /* 等待引用计数为0的缓冲区被原占有者解锁 */
    wait_on_buffer(bh);

    /* 在bh所指向缓冲区块解锁后,
     * 若被其它任务抢先引用则重新回到repeat处寻找引用计数为0的缓冲区块。*/
    if (bh->b_count)
        goto repeat;

    /* 若bh所指向缓冲区块中的数据曾被改写过,
     * 则需在本任务占用该缓冲区块之前将其内数据写回到对应的设备中。
     * 在将数据写回到对应设备后, 若该缓冲区块被其它任务引用则重新
     * 回到repeat处寻找引用计数为0的缓冲区块。*/
    while (bh->b_dirt) {
        sync_dev(bh->b_dev);
        wait_on_buffer(bh);
        if (bh->b_count)
            goto repeat;
    }
    /* NOTE!! While we slept waiting for this block, somebody else might */
    /* already have added "this" block to the cache. check it */
    /* 再检查dev&&block对应的缓冲区块的节点是否已被加入到hash表中,
     * 若已加入到hash表中则回到repeat处尝试从hash表中获取该节点。*/
    if (find_buffer(dev,block))
        goto repeat;
    /* OK, FINALLY we know that this buffer is the only one of it's kind, */
    /* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
    /* 如果以上所有检查都没有发生,
     * 说明本任务所遍历到的完全空闲的缓冲区块
     * 将由本任务获取到并由本任务做该缓冲区块被引用的相关设置。*/
    bh->b_count=1;
    bh->b_dirt=0;
    bh->b_uptodate=0;

    /* 将bh节点从hash队列中移除 */
    remove_from_queues(bh);

    /* 设置bh所指向缓冲区块对应的设备分区和设备上的数据块 */
    bh->b_dev=dev;
    bh->b_blocknr=block;
    /* 将指向空闲缓冲区块节点bh放到双向循环链表末尾,
     * 并将该节点加到hash队列中。*/
    insert_into_queues(bh);
    return bh;
}

/* [13] brelse,
 * 减少buf所关联缓冲区块的引用计数,
 * 并唤醒等等缓冲区块管理节点的任务。*/
void brelse(struct buffer_head * buf)
{
    if (!buf)
        return;
    
    /* 等待解锁 */
    wait_on_buffer(buf);
    if (!(buf->b_count--))
        panic("Trying to free free buffer");

    /* 唤醒最近等待缓冲区块管理节点buf解锁的任务,
     * 这会使得等待buf任务被相继唤醒。*/
    wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
/* [2] bread,
 * 读取设备分区号dev和数据块号block对应外设内容于某缓冲区块中,
 * 若读取成功则返回包含所读内容的缓冲区块首地址,
 * 若dev和block表征的内容不可读则返回NULL。
 * 
 * 在linux0.11的外设管理程序中,
 * 使用逻辑设备号关联外设,
 * 逻辑设备号由设备分区号和分区号构成, 如
 * 0x300为第1个硬盘0分区(分区表)设备号, 1, 2, 3, 4分别为其4个分区的分区号,
 * 0x301,...,0x304分别为第一个硬盘第1个,...,第4个分区的逻辑(设备)编号。
 * (见kernel/blk_dev/hd.c/sys_setup()函数得来的)
 * 
 * block为逻辑设备分区号dev对应分区中的数据块号。*/
struct buffer_head * bread(int dev,int block)
{
    struct buffer_head * bh;

    /* 根据缓冲区分配算法为dev&&block
     * 对应外设数据区分配一空闲缓冲区块。*/
    if (!(bh=getblk(dev,block)))
        panic("bread: getblk returned NULL\n");
    
    /* 若欲读外设数据已在bh所指向缓冲区块中
     * 则返回指向该缓冲区块的链表节点bh。*/
    if (bh->b_uptodate)
        return bh;

    /* 若数据未读入缓冲区块,
     * 则将bh对应外设数据区的数据读入bh所指向缓冲区块中。
     * ll_rw_block定义在kernel/blk_dev/ll_rw_blk.c中,
     * 可在阅读外设管理程序时再细读。*/
    ll_rw_block(READ,bh);
    wait_on_buffer(bh);

    /* 若成功将外设数据读入bh所指缓冲区则返回bh节点,
     * 否则释放bh节点的占用并返回NULL。*/
    if (bh->b_uptodate)
        return bh;
    brelse(bh);
    return NULL;
}

/* COPYBLK(from, to),
 * 将位于同一数据段from处的1024字节拷贝到to处。
 * 
 * 内联汇编输入。
 * "c" (BLOCK_SIZE/4), ecx = (BLOCK_SIZE/4) = 256;
 * "S" (from), esi = from;
 * "D" (to),   edi = to。
 * 
 * cld rep movsl 相当于
 * while (ecx--) 
 *     movl ds:esi, es:edi
 *     esi += 4
 *     edi += 4 */
#define COPYBLK(from,to) \
__asm__("cld\n\t" \
    "rep\n\t" \
    "movsl\n\t" \
    ::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
    :"cx","di","si")

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 */
/* [14] bread_page,
 * 读dev&&b[0..3]所对应设备数据块到缓冲区块中,
 * 然后将缓冲区块内容拷贝到以address为起始地址所对应的内存段中。*/
void bread_page(unsigned long address,int dev,int b[4])
{
    struct buffer_head * bh[4];
    int i;

    /* 将dev&&b[0..3]所对应设备数据读到缓冲区块中,
     * 缓冲区块首地址存在b[0..3]中。*/
    for (i=0 ; i<4 ; i++)
        if (b[i]) {
            if (bh[i] = getblk(dev,b[i]))
                if (!bh[i]->b_uptodate)
                    ll_rw_block(READ,bh[i]);
        } else
            bh[i] = NULL;

    /* 然后将读到缓冲区块的内容拷贝到起始地址为address的内存段中 */
    for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
        if (bh[i]) {
            wait_on_buffer(bh[i]);
            if (bh[i]->b_uptodate)
                COPYBLK((unsigned long) bh[i]->b_data,address);
            brelse(bh[i]);
        }
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */
/* [15] breada,
 * 阅读dev&&first对应数据块到缓冲区块中并返回缓冲区管理节点地址,
 * 若读取失败则返回NULL。
 * 若first后还跟有有效的数据块号, 则预读这些数据块号对应的数据块,
 * 若预读成功, 这些数据块也将在缓冲区块中, 可供将来直接引用。*/
struct buffer_head * breada(int dev,int first, ...)
{
    va_list args;
    struct buffer_head * bh, *tmp;

    va_start(args,first);

    /* 为dev&&first分配缓冲区块,
     * 将将dev&&first对应设备数据块读到缓冲区块中。*/
    if (!(bh=getblk(dev,first)))
        panic("bread: getblk returned NULL\n");
    if (!bh->b_uptodate)
        ll_rw_block(READ,bh);

    /* 若之后还跟有数据块号参数, 则预读这些数据块号对应的数据。
     * 预读是指可以顺利阅读则顺便读了, 不能顺利读也不强求,
     * 具体含义待阅读外设程序时再进一步理解吧。*/
    while ((first=va_arg(args,int))>=0) {
        tmp=getblk(dev,first);
        if (tmp) {
            if (!tmp->b_uptodate)
                ll_rw_block(READA,bh); /* 应该是tmp? */
            tmp->b_count--;
        }
    }
    va_end(args);
    wait_on_buffer(bh);

    /* 若第1数据块读成功, 
     * 则返回所读第一个缓冲区块管理节点地址;
     * 否则释放该节点对应缓冲区块并返回NULL。*/
    if (bh->b_uptodate)
        return bh;
    brelse(bh);
    return (NULL);
}
/* 缓冲区管理程序与多任务管理, 外设管理, 文件管理程序都有交织,
 * 不过此文并没有过多深入到这几个模块中去阅读相应程序源码,
 * 而是用粗略概括相应函数功能的方式衔接相应缓冲区管理函数的功能。
 * 这样做的好处是可解耦阅读缓冲区管理程序, 坏处是对于那些与其他模块
 * 有交织的函数的理解不是那么深, 可阅读多次来解决这个弊端吧。
 * 2019.06.16. */

/* [1] buffer_init,
 * 初始化双向链表以管理buffer。
 *
 * 以linux0.11管理最大内存16Mb为例,
 * 先感官下缓冲区(buffer)的位置吧。
 * --------------==========---------------------------
 * | OS routines | buffer | [ram-disk] | main_memory |
 * --------------|========|--------------------------|
 * 0x0          end      4Mb                        16Mb
 * end是操作系统内核程序的存储结束位置。
 * 回想引导程序bootsect.s, linux0.11为操作系统最大预留了512Kb内存,
 * 可以将512Kb即0x80000作为end的参考值。
 * 
 * 根据实模式内存分布, buffer需除去内存段[0x9ffff, 0xfffff]。
 *  0x00000|----------------------------------|
 *         |            1KB RAM               |
 *         | BIOS Interrupt vector table etc. |
 *  0x003FF|==================================|
 *         |                                  |
 *         |                                  |<-- end
 *         |             639KB                |
 *         |         RAM addr space           |
 *         |                                  |
 *         |                                  |
 *  0x9FFFF|==================================|
 *         |                                  |
 *         |              128K                |
 *         |    video card ram addr space     |
 *  0xBFFFF|==================================|
 *         |                                  |
 *         |             256KB                |
 *         |      BIOS ROM addr space         |
 *         |                                  |
 *         |                                  |
 *  0xFFFFF|==================================|
 *         |               .                  |
 *         |               .                  |
 * 0x400000|               .                  |<-- 4Mb
 * 即, 内存段[end, 0x400000)就是linux0.11设定的buffer了。*/
void buffer_init(long buffer_end)
{
    /* start_buffer为buffer开始处,
     * 管理buffer的双向链表数据结构位于buffer最前面。
     *
     * 双向链表的节点类型为struct buffer_head,
     * 该接头体类型定义在include/linux/fs.h文件中。*/
    struct buffer_head * h = start_buffer;
    void * b;
    int i;

    /* 若buffer结束地址为1Mb处, 则buffer末尾为640Kb处;
     * 之前假定linux0.11所管理的内存为16Mb,
     * 所以此处buffer的结束地址为4Mb处。*/
    if (buffer_end == 1<<20)
        b = (void *) (640*1024);
    else
        b = (void *) buffer_end;

    /* BLOCK_SIZE=1024即1Kb(include/linux/fs.h),
     * 链表中的一个节点 管理1Kb大小的缓冲区块。*/
    while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
        h->b_dev = 0;   /* 链表当前节点所记录的要使用其指向缓冲区块的设备号 */
        h->b_dirt = 0;  /* 标识当前节点所指缓冲区块是否被修改(0-未修改, 1-已修改) */
        h->b_count = 0; /* 当前节点所指缓冲区块的引用计数 */
        h->b_lock = 0;  /* 当前节点所指缓冲区块是否上锁(0-未锁, 1-已上锁) */
        h->b_uptodate = 0; /* 当前节点所指缓冲区块数据是否(0-无数据, 1-有数据) */
        h->b_wait = NULL;  /* 指向在等待 当前节点所指缓冲区块 释放锁的任务/进程 */
        h->b_next = NULL;  /* 指向与当前节点具相同hash值的下一节点 */
        h->b_prev = NULL;  /* 指向与当前节点具相同hash值的上一节点 */
        h->b_data = (char *) b; /* 当前节点所指缓冲区内存块 */
        h->b_prev_free = h-1;   /* 指向链表中的上一个节点 */
        h->b_next_free = h+1;   /* 指向链表中的下一个节点 */
        h++; /* 使用下一块大小为(struct buffer_head)内存用作下一个链表节点 */
        NR_BUFFERS++; /* 更新缓冲区块的数量 */

        /* 跳过VRAM&&BIOS ROM内存地址空间,
         * 内存地址空间[A0000h, 100000h)为VRAM和BIOS ROM内存条的内存地址 */
        if (b == (void *) 0x100000)
        b = (void *) 0xA0000;
    }

    /* 用链表节点指针类型(struct buffer_head *)的全局变量free_list
     * 指向链表首节点;让首节点指向上一个链表节点的成员指针指向链表
     * 中的最后一个节点, 让链表最后一个节点的指向下一个链表节点的
     * 成员指针指向链表首节点, 即完成双向循环链表。*/
    h--;
    free_list = start_buffer;
    free_list->b_prev_free = h;
    h->b_next_free = free_list;

    /* 将类型为(struct buffer_head *)的全局指针数组hash_table初始化为NULL。*/
    for (i=0;i<NR_HASH;i++)
        hash_table[i]=NULL;
}	
/* 先不管hash_table的作用吧,
 * 希望能在buffer.c中的其余函数中慢慢明白hash_table的作用。
 *
 * 若"buffer, 文件系统, 外设管理, 进程/任务管理"等各模块代码
 * 的交叉较大避不可避, 就开启交织阅读模式吧。*/
