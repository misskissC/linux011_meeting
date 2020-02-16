/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

/* i节点全局数组,
 * NR_INODE定义在include/linux/fs.h中。*/
struct m_inode inode_table[NR_INODE]={{0,},};

static void read_inode(struct m_inode * inode);
static void write_inode(struct m_inode * inode);

/* [1] wait_on_inode,
 * 若inode所指i节点锁状态已置位,则睡眠等待该i节点被解锁。*/
static inline void wait_on_inode(struct m_inode * inode)
{
/* 同wait_on_super */
    cli();
    while (inode->i_lock)
        sleep_on(&inode->i_wait);
    sti();
}

/* [2] lock_inode,
 * 为inode指i节点锁状态置位。*/
static inline void lock_inode(struct m_inode * inode)
{
/* 同lock_super */
    cli();
    while (inode->i_lock)
        sleep_on(&inode->i_wait);
    inode->i_lock=1;
    sti();
}

/* [3] unlock_inode,
 * 为inode所指i节点锁状态复位,唤醒等待inode所指i节点解锁的进程。*/
static inline void unlock_inode(struct m_inode * inode)
{
    inode->i_lock=0;
    wake_up(&inode->i_wait);
}

/* [4] invalidate_inodes,
 * 将设备号dev对应的i节点无效化,
 * 即将该i节点的设备号和修改标志都置为0.*/
void invalidate_inodes(int dev)
{
    int i;
    struct m_inode * inode;

    /* 在i节点全局数组中遍历设备分区号为dev的i节点,
     * 并将其设备号和修改标志都置为0以使该节点无效。*/
    inode = 0+inode_table;
    for(i=0 ; i<NR_INODE ; i++,inode++) {
        wait_on_inode(inode);
        if (inode->i_dev == dev) {
            if (inode->i_count)
                printk("inode in use on removed disk\n\r");
            inode->i_dev = inode->i_dirt = 0;
        }
    }
}

/* [5] sysnc_inodes,
 * 将内存中已发生修改的i节点同步到相应磁盘中。*/
void sync_inodes(void)
{
    int i;
    struct m_inode * inode;

    /* 遍历i节点全局数组,将每一个内容已发
     * 生改变的非管道i节点写回到相应磁盘中。*/
    inode = 0+inode_table;
    for(i=0 ; i<NR_INODE ; i++,inode++) {
        wait_on_inode(inode);
        if (inode->i_dirt && !inode->i_pipe)
            write_inode(inode);
    }
}

/* [6] _bmap,
 * create=1时,
 * 将逻辑块号block映射到inode所指i节点的z_inode[8]中,
 * 返回为block新创建磁盘块的逻辑块号。
 *
 * create=0时,
 * 若block对应逻辑块存在则返回该其逻辑块号,否则返回0。
 *
 * block是[0,i_zone[8]能表示逻辑块数]中的一个值。*/
static int _bmap(struct m_inode * inode,int block,int create)
{
    struct buffer_head * bh;
    int i;

    /* 见struct m_inode的z_none字段 */
    if (block<0)
        panic("_bmap: block<0");
    if (block >= 7+512+512*512)
        panic("_bmap: block>big");

    /* i节点逻辑块小于7时,
     * 它跟i_zone[0..6]其中一个元素直接对应,
     * 则为i_zone[block]分配一个可用逻辑块的逻辑块号,
     * 并设置i节点被修改标志和被修改时间。
     * new_block定义在fs/bitmap.c中, 届时粗略阅读。*/
    if (block<7) {
        if (create && !inode->i_zone[block])
            if (inode->i_zone[block]=new_block(inode->i_dev)) {
                inode->i_ctime=CURRENT_TIME;
                inode->i_dirt=1;
            }
        return inode->i_zone[block];
    }
/* i_zone[0..6]中的逻辑块号对应实际的逻辑块
 * i_zone[0] = lx0 --> 某可用磁盘逻辑块
 * i_zone[1] = lx1 --> 某可用磁盘逻辑块
 * i_zone[2] = lx2 --> 某可用磁盘逻辑块
 * i_zone[3] = lx3 --> 某可用磁盘逻辑块
 * i_zone[4] = lx4 --> 某可用磁盘逻辑块
 * i_zone[5] = lx5 --> 某可用磁盘逻辑块
 * i_zone[6] = lx6 --> 某可用磁盘逻辑块 */

    /* 若逻辑块号block在[7, 519)之间,
     * 首先为zone[7]分配一逻辑块,
     * 然后在zone[7]对应的逻辑块中为block分配实际的磁盘逻辑块号。
     * 并设置i节点被修改标志和修改时间。*/
    block -= 7;
    if (block<512) {
        if (create && !inode->i_zone[7])
            if (inode->i_zone[7]=new_block(inode->i_dev)) {
                inode->i_dirt=1;
                inode->i_ctime=CURRENT_TIME;
            }
        if (!inode->i_zone[7])
            return 0;

        /* 读取逻辑块号为i_zone[7]的数据块到bh所指缓冲区块中 */
        if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
            return 0;
        /* 在bh所指缓冲区块中取应存储block逻辑块号的位置取2字节数据。
         * 若该2字节处还未存储任何的逻辑块号,
         * 则分配一可用逻辑块的逻辑块号存储到此处,
         * 同时置该缓冲区块的修改标志为1。*/
        i = ((unsigned short *) (bh->b_data))[block];
        if (create && !i)
            if (i=new_block(inode->i_dev)) {
                ((unsigned short *) (bh->b_data))[block]=i;
                bh->b_dirt=1;
            }
        brelse(bh);
        return i;
    }
/* i_zone[7]中逻辑块号对应逻辑块存储了逻辑块号,
 * 这些逻辑块号与实际逻辑块对应。
 * 
 * 如block=7时, 其对应的逻辑块号存储位置和其磁盘块如下所示。
 * i_zone[7] = lx7 -->可用磁盘块(如下)
 *   0   |-----|
 *       | lx72|---> 某可用磁盘逻辑块
 *   2   |-----|
 *       |     |
 *   4   |-----|
 *       | ... |
 *   510 |-----|
 *       |     |
 *       |-----| */

    /* 若block在[512, 7+512+512*512)之间,
     * 则按照i_zone[8]存储逻辑号的规则为
     * block分配可用逻辑块的逻辑块号。*/
    block -= 512;

    /* 首先, 为i_zone[8]分配可用逻辑块的逻辑块号,
     * 同时置i节点修改标志和修改时间。*/
    if (create && !inode->i_zone[8])
        if (inode->i_zone[8]=new_block(inode->i_dev)) {
            inode->i_dirt=1;
            inode->i_ctime=CURRENT_TIME;
        }
    if (!inode->i_zone[8])
        return 0;
    /* 然后, 将逻辑块号为i_zone[8]的逻辑块读入缓冲区块中。*/
    if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
        return 0;
    /* 为block分配可用的逻辑块用作存储二级逻辑块号 */
    i = ((unsigned short *)bh->b_data)[block>>9];
    if (create && !i)
        if (i=new_block(inode->i_dev)) {
            ((unsigned short *) (bh->b_data))[block>>9]=i;
            bh->b_dirt=1;
        }
    brelse(bh);
    if (!i)
        return 0;
    /* 将用于存储二级逻辑块号对应的逻辑块读入缓冲区块中,
     * 最后为其block分配逻辑块。*/
    if (!(bh=bread(inode->i_dev,i)))
        return 0;
    i = ((unsigned short *)bh->b_data)[block&511];
    if (create && !i)
        if (i=new_block(inode->i_dev)) {
            ((unsigned short *) (bh->b_data))[block&511]=i;
            bh->b_dirt=1;
        }
    brelse(bh);
/* i_zone[8]中逻辑块号对应逻辑块存储了逻辑块号,
 * 这些逻辑块号对应的逻辑块号仍存了逻辑块号,
 * 最后的这些逻辑块号与实际磁盘逻辑块对应。
 * 
 * 如block=519时, 其对应的逻辑块号存储位置和其磁盘块如下所示。
 * i_zone[8] = lx8 -->可用磁盘块(如下)
 *   0   |-----|                    
 *       | 1x82|--->某可用磁盘逻辑块(如下)
 *   2   |-----|                    
 *       |     |                    
 *   4   |-----|                    
 *       | ... |                    
 *   510 |-----|                    
 *       |     |                    
 *       |-----|                    
 *
 *   0   |-----|                   
 *       | 1x83|-->某可用磁盘逻辑块为其可用磁盘逻辑块(如下)
 *   2   |-----|                   
 *       |     |                   
 *   4   |-----|                   
 *       | ... |                   
 *   510 |-----|                   
 *       |     |                   
 *       |-----|                   
 *
 *   0   |-----|
 *       |     |
 *   2   |-----|
 *       |     |
 *   4   |-----|
 *       | ... |
 *   510 |-----|
 *       |     |
 *       |-----| */

    return i;
}

/* [7] bmap,
 * 获取inode所指i节点中block对应逻辑块的逻辑块号。*/
int bmap(struct m_inode * inode,int block)
{
    return _bmap(inode,block,0);
}

/* [8] create_block,
 * 在inode所指i节点中为block创建对应的逻辑块,并返回该逻辑块的逻辑块号。*/
int create_block(struct m_inode * inode, int block)
{
    return _bmap(inode,block,1);
}

/* [9] iput,
 * 将inode所指i节点内容同步到相应设备中,并释放inode所指i节点。*/
void iput(struct m_inode * inode)
{
    if (!inode)
        return;

    /* 等待inode所指节点被解锁 */
    wait_on_inode(inode);
    if (!inode->i_count)
        panic("iput: trying to free free inode");

    /* 若inode所指节点为管道节点,
     * 则唤醒等待该管道节点的进程并减少当前任务对其的引用计数,
     * 若此时引用计数还不为0则直接返回;
     * 若此时引用计数为0则释放管道节点的内存页并
     * 复位inode所指节点的引用计数、管道标志、修改标志等数据成员。*/
    if (inode->i_pipe) {
        wake_up(&inode->i_wait);
        if (--inode->i_count)
            return;
        free_page(inode->i_size);
        inode->i_count=0;
        inode->i_dirt=0;
        inode->i_pipe=0;
        return;
    }

    /* 若inode所指i节点没有应用于设备上的文件,
     * 则将其引用计数减1后返回。*/
    if (!inode->i_dev) {
        inode->i_count--;
        return;
    }

    /* 若inode所指i节点为块设备的i节点,
     * 则将内存中的i节点和缓冲区块内容
     * 同步到块设备inode->i_zone[0]中。
     * (块设备i节点i_zone[0]中保存了设备号) */
    if (S_ISBLK(inode->i_mode)) {
        sync_dev(inode->i_zone[0]);
        wait_on_inode(inode);
    }
repeat:
    /* 若inode所指节点引用计数大于1,
     * 则对其应用计数减1后返回。*/
    if (inode->i_count>1) {
        inode->i_count--;
        return;
    }
    /* 若inode所指i节点文件链接数为0,
     * 表明该i节点无对应的文件, 
     * 则释放inode所指i节点的所有逻辑块并释放该i节点。
     * truncate和free_inode分别定义在fs/truncate.c和fs/bitmap.c中,
     * 届时再粗略阅读。*/
    if (!inode->i_nlinks) {
        truncate(inode);
        free_inode(inode);
        return;
    }

    /* 若inode所指i节点以被修改,
     * 则将该i节点同步到相应设备中,
     * 然后回到repeat处。*/
    if (inode->i_dirt) {
        write_inode(inode); /* we can sleep - so do again */
        wait_on_inode(inode);
        goto repeat;
    }
    /* 此时inode所指i节点引用计数为1且文件链接数不为0,
     * 则将i节点引用计数减为0。*/
    inode->i_count--;
    return;
}

/* [10] get_empty_inode,
 * 从i节点全局数组中遍历一个空闲的i节点,成功则返回其地址。*/
struct m_inode * get_empty_inode(void)
{
    struct m_inode * inode;
    static struct m_inode * last_inode = inode_table;
    int i;

    do {
        /* 在i节点全局数组中inode_table遍历一个空闲的i节点,
         * 即引用计数为0。当修改标志和上锁标志都为0时跳出遍历。*/
        inode = NULL;
        for (i = NR_INODE; i ; i--) {
            if (++last_inode >= inode_table + NR_INODE)
                last_inode = inode_table;
            if (!last_inode->i_count) {
                inode = last_inode;
                if (!inode->i_dirt && !inode->i_lock)
                    break;
            }
        }

        /* 若i节点全局数组中无空闲即引用计数为0的i节点,
         * 则打印i节点数组中各元素的设备号和i节点号并在
         * 死机前显示 内存中没有空闲的i节点了。*/
        if (!inode) {
            for (i=0 ; i<NR_INODE ; i++)
                printk("%04x: %6d\t",inode_table[i].i_dev,
                    inode_table[i].i_num);
            panic("No free inodes in mem");
        }
        /* 等待空闲i节点解锁 */
        wait_on_inode(inode);

        /* 若所遍历到的空闲i节点的内容曾被修改过,
         * 则需将该i节点同步到其对应的设备中。*/
        while (inode->i_dirt) {
            write_inode(inode);
            wait_on_inode(inode);
        }
    /* 若不幸该i节点被其它任务抢占了去, 那继续遍历 */
    } while (inode->i_count);

    /* 初始化inode所指向的i节点,
     * 除了引用计数为1外, 其余成员都初始化为0. */
    memset(inode,0,sizeof(*inode));
    inode->i_count = 1;
    return inode;
}

/* [11] get_pipe_indode,
 * 在i节点全局数组中获取一个空闲i节点,
 * 并将其用作管道节点, 最后返回该管道节点地址。*/
struct m_inode * get_pipe_inode(void)
{
    struct m_inode * inode;

    /* 在i节点全局数组中遍历一个空闲的i节点。
     * get_empty_inode不会返回NULL吧。*/
    if (!(inode = get_empty_inode()))
        return NULL;

    /* 为所获取到的inode所指向节点分配一页内存,
     * 若失败则将该i节点引用计数恢复为0.*/
    if (!(inode->i_size=get_free_page())) {
        inode->i_count = 0;
        return NULL;
    }

    /* 管道节点初始有两个引用计数, 一个读任务, 一个写任务。
     * 当i节点用作管道时,i_zone[0..1]分别用来存储管道头/尾,
     * 此处先将其初始化为0,
     * 最后置i节点的i_pipe标志, 表示该i节点用作管道i节点。*/
    inode->i_count = 2; /* sum of readers/writers */
    PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
    inode->i_pipe = 1;
    return inode;
}

/* [12] iget,
 * 获取dev对应设备上i节点号为nr的i节点。
 *
 * 若目标i节点已在内存中,
 * 若其已挂载文件系统,则返回所挂载文件系统的根i节点;
 * 若其未挂载文件系统,则返回目标i节点地址。
 *
 * 若目标i节点还未在内存中,
 * 则将其读取到内存中并返回其在内存中的首地址。*/
struct m_inode * iget(int dev,int nr)
{
    struct m_inode * inode, * empty;

    if (!dev)
        panic("iget with dev==0");

    /* 在i节点全局数组中遍历一个空闲i节点 */
    empty = get_empty_inode();

    /* 在i节点全局数组中查看是否已存在目标i节点 */
    inode = inode_table;
    while (inode < NR_INODE+inode_table) {
        if (inode->i_dev != dev || inode->i_num != nr) {
            inode++;
            continue;
        }
        /* 若目标i节点已在内存中
         * 则等待该i节点解锁 */
        wait_on_inode(inode);

        /* 若被本任务等到该i节点时目标i节点已被改写则重新遍历一次 */
        if (inode->i_dev != dev || inode->i_num != nr) {
            inode = inode_table;
            continue;
        }

        /* 增加目标i节点的引用计数 */
        inode->i_count++;

        /* 若目标i节点已挂载了文件系统,
         * 则将目标i节点所挂载文件系统的根i节点地址返回。
         * 若并未在内存中找到目标i节点所挂载的文件系统,
         * 则返回目标i节点本身。*/
        if (inode->i_mount) {
            int i;
            for (i = 0 ; i<NR_SUPER ; i++)
                if (super_block[i].s_imount==inode)
                    break;
            /* 若并未遍历到目标i节点所挂载文件系统的超级块,
             * 则显示错误信息, 释放最初申请的空闲i节点,
             * 并返回该i节点的地址。*/
            if (i >= NR_SUPER) {
                printk("Mounted inode hasn't got sb\n");
                if (empty)
                    iput(empty);
                return inode;
            }
            /* 若遍历到目标i节点所挂载的超级块,
             * 则将目标i节点更新到其对应设备中,
             * 并寻找原inode指向i节点所挂载的根文件节点。*/
            iput(inode);
            /* 尝试返回将目标i节点挂载文件系统中的根i节点 */
            dev = super_block[i].s_dev;
            nr = ROOT_INO;
            inode = inode_table;
            continue;
        }
        /* 若目标i节点没有挂载文件系统,
         * 释放不需要的空闲i节点, 返回目标i节点在内存中的首地址。*/
        if (empty)
            iput(empty);
        return inode;
    }
    
    /* 若目标i节点并不在内存中,
     * 则将设备号为devi节点号为nr的i节点读到内存中,
     * 并返回该i节点在内存中的地址。*/
    if (!empty)
        return (NULL);
    inode=empty;
    inode->i_dev = dev;
    inode->i_num = nr;
    read_inode(inode);
    return inode;
}

/* [13] read_inode,
 * 从inode所指i节点所对应设备中读取
 * i节点号inode->i_num对应的i节点到内存中。*/
static void read_inode(struct m_inode * inode)
{
    struct super_block * sb;
    struct buffer_head * bh;
    int block;

    /* 为inode指向i节点上锁,
     * 并在i节点全局数组中遍历inode所指i节点对应的超级块。*/
    lock_inode(inode);
    if (!(sb=get_super(inode->i_dev)))
        panic("trying to read inode without dev");

    /* 计算inode所指i节点在设备中的逻辑块号,
     * 逻辑块号 = 引导块数 + 超级块数 + i位图所占逻辑块数 +
     * 逻辑块位图所占逻辑块数 + i节点号i_num之前i节点所占逻辑块数。
     * 磁盘上的i节点号从1开始, 所以(i_num-1) / 磁盘块中i节点数才能
     * 准确得到i_num在i节点逻辑块中的偏移。
     *
     * 见fs/super.c read_super中对磁盘逻辑块的描述。*/
    block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
        (inode->i_num-1)/INODES_PER_BLOCK;
    /* 从相应设备中读取inode所指i节点到缓冲区块中 */
    if (!(bh=bread(inode->i_dev,block)))
        panic("unable to read i-node block");
    /* 将缓冲区块中i_num对应的i节点内容(仅存在于磁盘中部分)
     * 复制到inode所指i节点中 */
    *(struct d_inode *)inode =
        ((struct d_inode *)bh->b_data)
            [(inode->i_num-1)%INODES_PER_BLOCK];
    /* 释放缓冲区;
     * 解锁inode所指i节点。*/
    brelse(bh);
    unlock_inode(inode);
}

/* [14] write_inode,
 * 若inode所指节点已被修改,
 * 将inode所指节点写回其所在逻辑块的缓冲区块中,
 * 并标识该缓冲区块为已被修改状态,
 * 以让该缓冲区块伺时被同步到相应设备中。*/
static void write_inode(struct m_inode * inode)
{
    struct super_block * sb;
    struct buffer_head * bh;
    int block;

    /* 为inode所指节点上锁 */
    lock_inode(inode);

    /* 若inode所指节点数据未发生改变或
     * inode所指节点无对应设备号,
     * 则不用回写inode到对应设备中。*/
    if (!inode->i_dirt || !inode->i_dev) {
        unlock_inode(inode);
        return;
    }

    /* 从inode所指节点对应设备中读取超级块到缓冲区块中 */
    if (!(sb=get_super(inode->i_dev)))
        panic("trying to write inode without device");

    /* 计算inode节点在磁盘中逻辑块号 */
    block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
        (inode->i_num-1)/INODES_PER_BLOCK;

    /* 从设备上读取inode所指inode节点所在的逻辑块到缓冲区块中,
     * 并将inode所指i节点写到缓冲区块的相应位置上,
     * 并置缓冲区块已修改标志(该标志会使得该缓冲区块被伺机同步到设备中),
     * 同时恢复inode所指节点修改标志。*/
    if (!(bh=bread(inode->i_dev,block)))
    panic("unable to read i-node block");
    ((struct d_inode *)bh->b_data)
        [(inode->i_num-1)%INODES_PER_BLOCK] =
            *(struct d_inode *)inode;
    bh->b_dirt=1;
    inode->i_dirt=0;

    /* 释放缓冲区块;
     * 解锁inode所指i节点。*/
    brelse(bh);
    unlock_inode(inode);
}
