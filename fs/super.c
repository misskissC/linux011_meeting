/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
/* set_bit(bitnr, addr),
 * 返回起始于addr内存段中的第bitnr位的值。
 * 
 * 内联汇编的输入。
 * "a"(0), eax = 0;
 * "r"(bitnr), 任意空闲寄存器(假设为ebx), ebx=bitnr;
 * "m"(*(addr)), 内存变量*(addr);
 *
 * 内联汇编语句。
 * bt %2, %3 -> bt ebx, *addr, 
 * 检测*addr的ebx位是否为1, 为1则eflag.CF=1, 否则eflag.CF=0;
 * setb %%al, al=CF;
 *
 * 内联汇编输出。
 * __res = eax。
 *
 * __res作为set_bit(bitnr, addr)宏代表表达式的最终值。*/
#define set_bit(bitnr,addr) ({ \
register int __res __asm__("ax"); \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

/* 超级块全局数组,
 * 用于缓存磁盘文件系统中的超级块。*/
struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
/* ROOT_DEV全局变量存储根文件系统的逻辑设备号,
 * 其在init/main.c中被初始化。*/
int ROOT_DEV = 0;

/* [4] lock_super,
 * 置位sb指向的超级块的锁状态。*/
static void lock_super(struct super_block * sb)
{
/* 在给sb所指超级块锁状态置位的过程中,
 * 进入睡眠进程的状态为不可中断状态
 * (TASK_UNINTERRUPTIBLE),所以在此过程
 * 中需禁止CPU响应本进程的中断。*/

    cli();
    while (sb->s_lock)
        /* sleep_on,
         * 置本进程状态为不可运行状态,
         * 调用进程调度函数调度其他进程运行。
         * 
         * 在其他进程运行过程中,直到有进程调
         * 用wake_up(&(sb->s_wait))时才会唤醒
         * 本进程在sleep_on中的睡眠(阻塞),因为
         * 本进程还处在内核态又禁止了中断响应,
         * 所以本进程会一直往后执行,直到本进程
         * 再显式调用进程调度函数或从内核返回到
         * 用户态时或在用户态发生诸定时器中断时
         * 才可能(当前进程时间片完)会进行进程切换。
         *
         * 所以while(sb->s_lock)和sb->s_lock=1的
         * 执行过程中,没有其他进程或中断程序参与,
         * 即没有sb所指超级块被冲突访问的风险。*/
        sleep_on(&(sb->s_wait));
    /* 使用while的原因是可能有多个进程在等待sb所指
     * 超级块,在其他进程中调用wake_up(&(sb_swait))
     * 唤醒等待sb所指超级块的进程为可运行状态后,谁
     * 先被调度是进程的时间片决定的,若是另一个等待
     * sb所指超级块的进程的时间片更大即先被调度而给
     * sb所指超级块的锁状态置了位又因等待其他资源而
     * 显式调用进程调度函数而切换到本进程运行从sleep_on
     * 返回时,因sb所指超级块锁状态呈置位状态,所以需继
     * 续睡眠等待。*/
    sb->s_lock = 1;
    sti();
}

/* [5] free_super,
 * 复位sb所指超级块的锁状态。*/
static void free_super(struct super_block * sb)
{
/* 同lock_super */

    cli();
    sb->s_lock = 0;
    /* 置最近调用sleep_on(&(sb->s_wait))
     * 进程为可运行状态,从而陆续置所有调
     * 用过sleep_on(&(sb->s_wait))的进程
     * 的状态为可运行状态。*/
    wake_up(&(sb->s_wait));
    sti();
}

/* [6] wait_on_super,
 * 等待sb所指超级块解锁。*/
static void wait_on_super(struct super_block * sb)
{
/* 原理同lock_super */

    cli();
    while (sb->s_lock)
        sleep_on(&(sb->s_wait));
    sti();
}

/* [3] get_super,
 * 在超级块全局数组中查找设备号为dev的超级块,
 * 若找到则返回该超级块元素内存首地址, 否则返回NULL。*/
struct super_block * get_super(int dev)
{
    struct super_block * s;

    if (!dev)
        return NULL;
    s = 0+super_block;
    while (s < NR_SUPER+super_block)
        if (s->s_dev == dev) {
            wait_on_super(s);
            if (s->s_dev == dev)
                return s;
            s = 0+super_block;
        } else
            s++;
    return NULL;
}

/* [7] put_super,
 * 释放dev对应设备文件系统在内存中的超级块。
 * 若dev为根文件系统设备或其超级块已挂载到
 * 某i节点,则不释放该超级块。*/
void put_super(int dev)
{
    struct super_block * sb;
    struct m_inode * inode;
    int i;

    if (dev == ROOT_DEV) {
        printk("root diskette changed: prepare for armageddon\n\r");
        return;
    }

    /* 从超级块全局数组中遍历设备号为dev的超级块,
     * 若其已经挂载到某i节点则直接返回。*/
    if (!(sb = get_super(dev)))
        return;
    if (sb->s_imount) {
        printk("Mounted disk changed - tssk, tssk\n\r");
        return;
    }
    /* 上锁修改sb所指超级块。
     *
     * 取消超级块与设备号的关联,
     * 释放i节点位图和逻辑块位图的缓冲区块。
     *
     * 解锁唤醒等待sb所指超级块锁复位的中断程序或进程,
     * 以让进程呈可运行状态。*/
    lock_super(sb);
    sb->s_dev = 0;
    for(i=0;i<I_MAP_SLOTS;i++)
        brelse(sb->s_imap[i]);
    for(i=0;i<Z_MAP_SLOTS;i++)
        brelse(sb->s_zmap[i]);
    free_super(sb);
    return;
}

/* [2] read_super,
 * 将dev对应文件系统中的
 * 超级块及超级块指向的i节点位图块读入到内存中。*/
static struct super_block * read_super(int dev)
{
    struct super_block * s;
    struct buffer_head * bh;
    int i,block;

    if (!dev)
        return NULL;

    /* 检查dev对应磁盘(特指软盘)是否已无效,
     * 无效则释放磁盘相关数据结构所占内存。
     * check_dis_change定义在fs/buffer.c中。*/
    check_disk_change(dev);

    /* 检查dev对应超级块是否已在内存中,
     * 若在则直接返回该超级块在内存中的首地址。*/
    if (s = get_super(dev))
        return s;

    /* 若dev对应超级块还未被读入,
     * 则在超级块全局数组中遍历一个
     * 还未与任何设备关联即空闲的超级块与dev超级块关联。*/
    for (s = 0+super_block ;; s++) {
        if (s >= NR_SUPER+super_block)
            return NULL;
        if (!s->s_dev)
            break;
    }
    /* no lock_super(s) here even earlier ?? */
    s->s_dev = dev;
    s->s_isup = NULL;
    s->s_imount = NULL;
    s->s_time = 0;
    s->s_rd_only = 0;
    s->s_dirt = 0;
    
    /* 在bread函数中可能会调用sleep_on函数
     * 而运行其他进程,所以上锁保护s所指超级块。*/
    lock_super(s);

    /* 读dev对应设备上的逻辑块号为1即
     * 超级块所在的逻辑块到缓冲区块中。
     * 若读取失败,则释放dev所关联超级块s。*/
    if (!(bh = bread(dev,1))) {
        s->s_dev=0;
        free_super(s);
        return NULL;
    }
    /* 将读到缓冲区块中的超级块复制到s所指超级块内存中 */
    *((struct d_super_block *) s) =
        *((struct d_super_block *) bh->b_data);
    /* 释放用于读取超级块的缓冲区块 */
    brelse(bh);

    /* 根据超级块的s_magic成员判断文件系统类型,
     * 若不为minix则返回释放资源返回NULL。*/
    if (s->s_magic != SUPER_MAGIC) {
        s->s_dev = 0;
        free_super(s);
        return NULL;
    }
    /* 初始化超级块i节点位图和数据逻辑块位图指针数组 */
    for (i=0;i<I_MAP_SLOTS;i++)
        s->s_imap[i] = NULL;
    for (i=0;i<Z_MAP_SLOTS;i++)
        s->s_zmap[i] = NULL;

/* MINIX文件系统逻辑块号分布大体如下。
 * ---------------------------------------------------
 * |引导区块|超级块|i节点位图|逻辑块位图|i节点|数据块|
 * ---------------------------------------------------
 * 0        1      2         ^          ^     ^
 *                           |          |     |
 *                 lb=2+s_imap_blocks   |     |
 *                       ib=lb+s_zmap_blocks  |
 *                                s_firstdatazone
 *
 * 以下程序段分别读取文件系统i节点位图和逻辑块位图到缓冲区块中,
 * 分别由超级块的s_imap和s_zmap成员指向。*/
    block=2;
    for (i=0 ; i < s->s_imap_blocks ; i++)
        if (s->s_imap[i]=bread(dev,block))
            block++;
        else
            break;
    for (i=0 ; i < s->s_zmap_blocks ; i++)
        if (s->s_zmap[i]=bread(dev,block))
            block++;
        else
            break;

    /* 若读取文件系统位图数据结构失败, 则释放相关资源 */
    if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
        for(i=0;i<I_MAP_SLOTS;i++)
            brelse(s->s_imap[i]);
        for(i=0;i<Z_MAP_SLOTS;i++)
            brelse(s->s_zmap[i]);
        s->s_dev=0;
        free_super(s);
        return NULL;
    }
    
    /* 将i节点0和逻辑块0标识为使用状态 */
    s->s_imap[0]->b_data[0] |= 1;
    s->s_zmap[0]->b_data[0] |= 1;
    
/* 解锁唤醒等待s指向的超级块解锁的进程 */
    free_super(s);
    return s;
}

/* [9] sys_umount,
 * 取消块设备文件dev_name(所属文件系统)的挂载。*/
int sys_umount(char * dev_name)
{
    struct m_inode * inode;
    struct super_block * sb;
    int dev;

    /* 找到块设备文件dev_name的i节点 */
    if (!(inode=namei(dev_name)))
        return -ENOENT;

    /* 获取块设备的设备分区号 */
    dev = inode->i_zone[0];

    /* 若dev_name不为块设备,
     * 则释放其i节点并返回非块设备的错误码。*/
    if (!S_ISBLK(inode->i_mode)) {
        iput(inode);
        return -ENOTBLK;
    }
    iput(inode);
    /* 若dev_name为根文件系统则不去除挂载,
     * 否则整个文件系统没有了起点,便不能再使用了吧。*/
    if (dev==ROOT_DEV)
        return -EBUSY;

    /* 读取块设备文件dev_name对应的超级块,
     * 若其挂载i节点指针为NULL或者所指向i节
     * 点并未置已挂载文件系统的标志则返回相应错误码。*/
    if (!(sb=get_super(dev)) || !(sb->s_imount))
        return -ENOENT;
    if (!sb->s_imount->i_mount)
        printk("Mounted inode has i_mount=0\n");

    /* 确保dev_name已经没有再被使用了 */
    for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
        if (inode->i_dev==dev && inode->i_count)
            return -EBUSY;
        
    /* 清所有跟挂载相关的数据成员和引用计数 */
    sb->s_imount->i_mount=0;
    iput(sb->s_imount);
    sb->s_imount = NULL;
    iput(sb->s_isup);
    sb->s_isup = NULL;
    put_super(dev);
    sync_dev(dev);
    return 0;
}

/* [8] sys_mount,
 * 将块设备文件dev_name(所属文件系统)挂载到dir_name目录下。*/
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
    struct m_inode * dev_i, * dir_i;
    struct super_block * sb;
    int dev;

    /* 获取dev_name对应的i节点 */
    if (!(dev_i=namei(dev_name)))
        return -ENOENT;

    /* 获取块设备文件的设备分区号;
     * 若dev_name不为块文件设备则
     * 释放其i节点并返回无权限错误码。*/
    dev = dev_i->i_zone[0];
    if (!S_ISBLK(dev_i->i_mode)) {
        iput(dev_i);
        return -EPERM;
    }
    iput(dev_i);

    /* 获取目录dir_name对应的i节点,
     * 若此目录已被其它任务占用或为
     * 根节点目录则释放此i节点并返回被占用错误码。*/
    if (!(dir_i=namei(dir_name)))
        return -ENOENT;
    if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
        iput(dir_i);
        return -EBUSY;
    }
    /* dir_name不为目录则返回无权限错误码 */
    if (!S_ISDIR(dir_i->i_mode)) {
        iput(dir_i);
        return -EPERM;
    }

    /* 读取块设备文件dev_name的超级块,
     * 根据其超级块:若dev_name所属文件
     * 系统已挂载他处则返回已被占用的错误码。
     *
     * 另一方面,若欲挂载目录已挂载
     * 其它文件系统则返回无权限错误码。*/
    if (!(sb=read_super(dev))) {
        iput(dir_i);
        return -EBUSY;
    }
    if (sb->s_imount) {
        iput(dir_i);
        return -EBUSY;
    }
    if (dir_i->i_mount) {
        iput(dir_i);
        return -EPERM;
    }

    /* 若块设备文件dev_name可以挂载到dir_name目录下,
     * 则通过dev_name对应超级块去关联dir_name目录的i节点,
     * 并置dir_name的挂载成员为1表征已有文件系统挂载,
     * 并置dir_name i节点已被修改标志以标识此i节点可被同步到相应设备上了。*/
    sb->s_imount=dir_i;
    dir_i->i_mount=1;
    dir_i->i_dirt=1;    /* NOTE! we don't iput(dir_i) */
    return 0;           /* we do that in umount */
}


/* 在阅读文件系统的第一个函数sys_mount之前,
 * 先粗略了解下linux0.11的文件系统大概是什么样的吧。
 * 
 * 粗略理解MINIX1.0文件系统在磁盘中的组织。
 * ---------------------------------------------------------------------------------
 * |引导块区域|超级块区域|i节点位图区域|数据逻辑块位图区域|i节点区域|数据逻辑块区域|
 * ---------------------------------------------------------------------------------
 * 逻辑块是文件系统程序对磁盘进行操作的基本单位,MINIX1.0为1Kb(2扇区);
 * 纯文件系统的引导块中可以包含分区表信息;
 * 超级块用于描述文件系统总体信息,如文件系统各部分大小信息,数据逻辑块开始位置;
 * i节点位图和逻辑块位图用于记录i节点和数据逻辑块的使用情况;
 * 一个i节点中包含了一个文件的所有信息;
 * 数据逻辑块用于存储文件的数据。*/

/* [1] mount_root,
 * 挂载根文件系统。
 * 
 * 将已被格式化为MINIX1.0格式磁盘上的超级块和根i节点读取到内存中,
 * 设置根i节点关联(关联)高级块描述的文件系统,
 * 将该根i节点作为当前进程的根目录i节点和当前目录i节点。
 *
 * '挂载':将磁盘上MINIX文件系统的超级块和根i节点读入内存后,
 * 就可以点他们管理(读,写,创建等)该磁盘上的文件系统了。
 * 这就是'挂载'的含义吧。
 *
 * 根文件系统的挂载相当于挂载了文件系统的起点。*/
void mount_root(void)
{
    int i,free;

    struct super_block * p;
    struct m_inode * mi;

    /* linux0.11要求磁盘上的i节点占32字节 */
    if (32 != sizeof (struct d_inode))
        panic("bad i-node size");

    /* 初始化用于记录打开文件的全局文件数组,
     * 将其置于未使用的初始状态。*/
    for(i=0;i<NR_FILE;i++)
        file_table[i].f_count=0;

    /* 若根文件系统在软盘上,
     * 则等待软盘插入,键入ENTER键表示插入完毕。*/
    if (MAJOR(ROOT_DEV) == 2) {
        printk("Insert root floppy and press ENTER");
        /* 等待键盘输入,
         * wait_for_keypress定义在
         * kernel/chr_drv/tty_io.c中。*/
        wait_for_keypress();
    }

    /* 初始化超级块全局结构体数组,
     * 将超级块位于内存中的部分数据成员初始化,将其置于未使用状态。*/
    for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
        p->s_dev = 0;
        p->s_lock = 0;
        p->s_wait = NULL;
    }

    /* 将根文件系统超级块读到内存中 */
    if (!(p=read_super(ROOT_DEV)))
        panic("Unable to mount root");

    /* 读取根文件系统中根i节点(i节点号为ROOT_INO)到内存中 */
    if (!(mi=iget(ROOT_DEV,ROOT_INO)))
        panic("Unable to read root i-node");

    /* 3=超级块+当前进程当前目录+当前进程根目录 */
    mi->i_count += 3 ; /* NOTE! it is logically used 4 times, not 1 */

    /* 指定超级块的根i节点,
     * 并将根i节点设置为当前进程当前目录和根目录所对应的i节点。*/
    p->s_isup = p->s_imount = mi;
    current->pwd = mi;
    current->root = mi;

    /* 统计文件系统中空闲的逻辑块数 */
    free=0;
    i=p->s_nzones; /* 数据逻辑块数,最大为2^16 */
    while (-- i >= 0)
        /* 随着i的变化,
         * i&8191值循环落在[8191, 0]区间;
         * i>>13即以8Kb为单位即落到[7, 0]区间。
         * 搭上i初值和while循环,
         * 将会遍历s_zmap指针数组指向的缓冲区块中的每一位,
         * 每当遇到为0的bit位则计数free。*/
        if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
            free++;
    printk("%d/%d free blocks\n\r",free,p->s_nzones);
    /* 以上程序段利用i&8191和i>>13
     * 在一个循环中遍历并判断8个缓冲区块中的每一位,
     * 能显式减少CPU分支预测错误风险, 从而提升程序性能。*/

    /* 同理, 以下程序段统计空闲的i节点数。*/
    free=0;
    i=p->s_ninodes+1; /* +1即算上i节点号为0的i节点 */
    while (-- i >= 0)
    if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
        free++;
    printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
