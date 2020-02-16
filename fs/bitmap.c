/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
/* bitmap.c包含了处理inode(i节点)和块位图的代码 */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

/* clear_block,
 * 将起始于addr的BLOCK_SIZE字节清0。
 *
 * 内联汇编输入。
 * "a" (0), eax = 0;
 * "c" (BLOCK_SIZE / 4), ecx = BLOCK_SIZE / 4;
 * "D" ((long) (addr)), edi = (long) (addr)。
 *
 * 内联汇编指令。
 * cld rep stosl 相当于
 * while (ecx--)
 *    movl eax, es:edi
 *    edi += 4
 */
#define clear_block(addr) \
__asm__("cld\n\t" \
    "rep\n\t" \
    "stosl" \
    ::"a" (0),"c" (BLOCK_SIZE/4),"D" ((long) (addr)):"cx","di")

/* set_bit(nr, addr),
 * 在addr起始内存段中的nr位上置1。
 *
 * 内联汇编输入。
 * "0"(0), eax = 0;
 * "r" (nr), 通用空闲寄存器=nr;
 * "m"(*(addr)), 内存变量*(addr)。
 *
 * 内联汇编指令。
 * btsl %2,%3, 相当于btsl nr, *(addr),
 * 即将*(addr)内存段中的nr位存在eflag.CF中, 然后将该位置1;
 *
 * setb %%al, 若eflag.CF=1则给al置位。
 * 
 * 内联汇编的输出。
 * res = eax。
 *
 * res最终作为该宏的"返回值"。*/
#define set_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

/* clear_bit(nr,addr),
 *
 * 内联汇编输入。
 * "0" (0), 将%0的约束即eax=0;
 * "r" (nr), 任意空闲通用寄存器(r)=nr,
 * "m" (*(addr)), 内存变量*addr。
 *
 * 内联汇编输出。
 * btrl %2, %3即btrl r, *addr,
 * 将*addr的r位保存在eflag.CF中, 将addr的位号为r的位置为0;
 *
 * setnb %%al, 若CF=0则设置al=0。
 *
 * 内联汇编输出。
 * "=a"(res), res = eax。
 *
 * res作为clear_bit表达式最终值。*/
#define clear_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

/* find_first_zero(addr),
 * 在起始于addr共1024字节的内存块中找到首位为0位的位号。
 *
 * 内联汇编输入。
 * "c"(0), ecx = 0;
 * "S"(addr), ESI = addr。
 *
 * 内联汇编指令。
 * cld, eflag.DF=0, 
 * 即执行串相关指令后地址偏移寄存器增相应长度
 * 
 * lodsl, 相当于movl (DS:ESI), EAX; SI += 4
 * 即把addr处的4字节内容读到EAX寄存器中。
 *
 * notl %%eax, eax ~= eax。
 *
 * bsfl %%eax, %%edx,
 * 从eax低位开始扫描, 将eax首个非0位的位号(从0开始)赋给edx;
 * 若扫描到则置eflag.ZF=0。
 * 
 * 若在当前DS:SI指向的4字节内容中找到为0的位,
 * 则将该位的位号赋值给ecx并跳转标号3处,
 * 并将ecx值赋给__res作为该宏最终的"返回值";
 * 如果在当前DS:SI指向的4字节内容中没有找到为0的位,
 * 则用ecx记录当前经过的位数, 继续回到标号1处寻找,
 * 在找到位0时再输出给__res;
 * 若ecx大于等于8192时, 则不再继续寻找DS:SI指向内存块中的位0。*/
#define find_first_zero(addr) ({ \
int __res; \
__asm__("cld\n" \
    "1:\tlodsl\n\t" \
    "notl %%eax\n\t" \
    "bsfl %%eax,%%edx\n\t" \
    "je 2f\n\t" \
    "addl %%edx,%%ecx\n\t" \
    "jmp 3f\n" \
    "2:\taddl $32,%%ecx\n\t" \
    "cmpl $8192,%%ecx\n\t" \
    "jl 1b\n" \
    "3:" \
    :"=c" (__res):"c" (0),"S" (addr):"ax","dx","si"); \
__res;})

/* free_block,
 * 释放设备号dev对应设备上逻辑块号为block
 * 对应的缓冲区块,复位dev&&block逻辑块对应
 * 的逻辑块位图位。*/
void free_block(int dev, int block)
{
    struct super_block * sb;
    struct buffer_head * bh;

    /* 获取设备分区号dev在内存中的超级块,
     * 检查将要释放的逻辑块号block是否在合法区域中。*/
    if (!(sb = get_super(dev)))
        panic("trying to free block on nonexistent device");
    if (block < sb->s_firstdatazone || block >= sb->s_nzones)
        panic("trying to free block not in datazone");

    /* 在hash数组中寻找dev&&block对应的缓冲区块节点,并将其释放。*/
    bh = get_hash_table(dev,block);
    if (bh) {
        if (bh->b_count != 1) {
            printk("trying to free block (%04x:%d), count=%d\n",
                dev,block,bh->b_count);
            return;
        }
        bh->b_dirt=0;
        bh->b_uptodate=0;
        brelse(bh);
    }
    /* 复位逻辑块对应的逻辑块位图位,
     * 置位该位所在的缓冲区块的已修改标志。*/
    block -= sb->s_firstdatazone - 1 ;
    if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) {
        printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
        panic("free_block: bit already cleared");
    }
    sb->s_zmap[block/8192]->b_dirt = 1;
}

/* [3] new_block,
 * 在dev对应设备上寻找一个空闲逻辑块并将其清0,
 * 在dev超级块的逻辑位图中记录该逻辑块已被使用。*/
int new_block(int dev)
{
    struct buffer_head * bh;
    struct super_block * sb;
    int i,j;

    /* 获取dev在内存中对应的超级块 */
    if (!(sb = get_super(dev)))
        panic("trying to get new block from nonexistant device");

    /* 在dev对应超级块的逻辑块位图中寻找首个空闲位,
     * 若没有找到空闲位则返回0。*/
    j = 8192;
    for (i=0 ; i<8 ; i++)
        if (bh=sb->s_zmap[i])
            if ((j=find_first_zero(bh->b_data))<8192)
                break;
    if (i>=8 || !bh || j>=8192)
        return 0;

    /* 置位逻辑块位图中的空闲位用作此次分配,
     * 并标识用作逻辑块位图的缓冲区块已修改标志。*/
    if (set_bit(j,bh->b_data))
        panic("new_block: bit already set");
    bh->b_dirt = 1;

    /* 若逻辑块号大于最大逻辑块号则返回0 */
    j += i*8192 + sb->s_firstdatazone-1;
    if (j >= sb->s_nzones)
        return 0;

    /* 为设备分区号dev的逻辑块j分配一空闲缓冲区块 */
    if (!(bh=getblk(dev,j)))
        panic("new_block: cannot get block");
    if (bh->b_count != 1)
        panic("new block: count is != 1");

    /* 为dev在设备上分配逻辑块对应缓冲区块清0,
     * 并置更新标志相当于告知其他任务该逻辑块
     * 的内容已在缓冲块中,同时置缓冲区块的已修
     * 改标志。最后释放缓冲区块以伺机将该缓冲区
     * 块的内容同步到设备上。*/
    clear_block(bh->b_data);
    bh->b_uptodate = 1;
    bh->b_dirt = 1;
    brelse(bh);
    return j;
}

/* [2] free_inode,
 * 释放inode所指向的在内存中的i节点。*/
void free_inode(struct m_inode * inode)
{
    struct super_block * sb;
    struct buffer_head * bh;

    if (!inode)
        return;

    /* 若inode所指i节点没有与设备关联,
     * 则表明其没有i节点位图。*/
    if (!inode->i_dev) {
        memset(inode,0,sizeof(*inode));
        return;
    }

    /* 检查inode所指i节点是否还有其他任务在引用 */
    if (inode->i_count>1) {
        printk("trying to free inode with count=%d\n",inode->i_count);
        panic("free_inode");
    }
    if (inode->i_nlinks)
        panic("trying to free inode with links");

    /* 检查inode所指i节点中的成员数据是否在合理范围内 */
    if (!(sb = get_super(inode->i_dev)))
        panic("trying to free inode on nonexistent device");
    if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
        panic("trying to free inode 0 or nonexistant inode");
    if (!(bh=sb->s_imap[inode->i_num>>13]))
        panic("nonexistent imap in superblock");

    /* 将inode所指i节点对应的i位图位恢复为0 */
    if (clear_bit(inode->i_num&8191,bh->b_data))
        printk("free_inode: bit already cleared.\n\r");

    /* 置用作i节点位图的缓冲区块已被修改标志,
     * 恢复inode所指i节点的数据成员。*/
    bh->b_dirt = 1;
    memset(inode,0,sizeof(*inode));
}

/* [1] new_inode,
 * 为dev分配一个新的i节点。
 *
 * 在i节点数组中遍历一个空闲的i节点,
 * 在设备分区号dev对应的超级块中的i
 * 位图中寻找到首个空闲位,用该空闲位
 * 的位号作为以上i节点的i节点号,并设
 * 置以上i节点与dev关联。*/
struct m_inode * new_inode(int dev)
{
    struct m_inode * inode;
    struct super_block * sb;
    struct buffer_head * bh;
    int i,j;

    /* 从内存即i节点全局数组中寻找一个空闲i节点 */
    if (!(inode=get_empty_inode()))
        return NULL;

    /* 在内存即超级块全局数组中寻找分区设备号dev对应的超级块。*/
    if (!(sb = get_super(dev)))
        panic("new_inode with unknown device");

    /* 在用作i节点位图的连续8个缓冲区块中寻找首个bit为0的位 */
    j = 8192;
    for (i=0 ; i<8 ; i++)
        if (bh=sb->s_imap[i])
            if ((j=find_first_zero(bh->b_data))<8192)
                break;
    /* 若无8个缓冲区块作为i节点位图,
     * 或没有找到空闲i节点,
     * 或位图数大于i节点数则释放申请的空闲i节点。*/
    if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
        iput(inode);
        return NULL;
    }

    /* 将遍历到的空闲的i节点位图置1以表被占用,
     * 若该位已经被置位就提示后死机。*/
    if (set_bit(j,bh->b_data))
        panic("new_inode: bit already set");

    /* 因为设置过i节点位图,
     * 所以用作i节点位图的缓冲区块已被修改过了,
     * 需设置相应缓冲区块的被修改标志,
     * 已伺时被同步到设备上。*/
    bh->b_dirt = 1;

    /* 虽然get_empty_inodey已经将inode所指节点
     * 的引用计数置位1了, 在此处再置一次吧;
     * i节点链接数置为1;
     * 关联设备分区号;
     * 关联i节点用户id和组id;
     * 置i节点已被修改标志, 以伺机同步到设备中;
     * 记录该i节点的i节点号;
     * i节点相关时间的更新。*/
    inode->i_count=1;
    inode->i_nlinks=1;
    inode->i_dev=dev;
    inode->i_uid=current->euid;
    inode->i_gid=current->egid;
    inode->i_dirt=1;
    inode->i_num = j + i*8192;
    inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
    return inode;
}
