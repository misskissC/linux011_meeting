/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */

#include <string.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>

#define MAJOR_NR 1 /* 虚拟硬盘主设备号 */
#include "blk.h"

/* 用于记录虚拟硬盘内存首地址和长度的全局变量 */
char *rd_start;
int rd_length = 0;

/* do_rd_request,
 * 虚拟硬盘读写请求函数。*/
void do_rd_request(void)
{
    int len;
    char *addr;

    /* 检查当前请求是否合理 */
    INIT_REQUEST;

    /* 将扇区号换算为内存地址 */
    addr = rd_start + (CURRENT->sector << 9);
    len = CURRENT->nr_sectors << 9;
    /* 检查次设备号和所读内存地址,若不在范围内则结束本次请求并调度下一个请求 */
    if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
        end_request(0);
        goto repeat;
    }
    /* 若请求为写虚拟硬盘,则将请求缓冲区中的数据拷贝到虚拟硬盘相应内存段中 */
    if (CURRENT-> cmd == WRITE) {
        (void ) memcpy(addr,
                    CURRENT->buffer,
                    len);
    /* 若请求为读虚拟硬盘,则将虚拟硬盘相应内存段数据拷贝到请求缓冲区中 */
    } else if (CURRENT->cmd == READ) {
        (void) memcpy(CURRENT->buffer, 
                    addr,
                    len);
    } else
        panic("unknown ramdisk-command");
    
    /* 正常结束本次虚拟硬盘请求并调度虚拟硬盘下一个请求,
     * 同时回到INIT_REQUEST中repeat处检查该请求的合理性。*/
    end_request(1);
    goto repeat;
}

/*
 * Returns amount of memory which needs to be reserved.
 */
/* [1] rd_init,
 * 初始化与虚拟硬盘内存段[mem_start, mem_start+length)相关信息,
 * 该函数返回需要为虚拟硬盘保留的内存大小。*/
long rd_init(long mem_start, int length)
{
    int i;
    char *cp;

    /* 设置虚拟硬盘设备的请求函数 */
    blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;

    /* 用全局变量记录虚拟硬盘内存段信息并将该段内存初始化为0 */
    rd_start = (char *) mem_start;
    rd_length = length;
    cp = rd_start;
    for (i=0; i < length; i++)
        *cp++ = '\0';
    return(length);
}

/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 */
/* [2] rd_load,
 * 若根文件设备为虚拟硬盘,则尝试加载虚拟硬盘。
 * 虚拟设备的前提是设置FLOPPY作为根文件设备,之后将其改为虚拟硬盘。*/
void rd_load(void)
{
    struct buffer_head *bh;
    struct super_block  s;
    int block = 256;    /* Start at block 256 */
    int i = 1;
    int nblocks;
    char    *cp;    /* Move pointer */

    if (!rd_length)
        return; /* 若虚拟硬盘内存长度为0则返回 */
    printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
        (int) rd_start);
    if (MAJOR(ROOT_DEV) != 2)
        return; /* 若根文件系统主设备号不为软盘则返回 */

    /* 读取软盘根文件系统的超级块到s中 */
    bh = breada(ROOT_DEV,block+1,block,block+2,-1);
    if (!bh) {
        printk("Disk error while looking for ramdisk!\n");
        return;
    }
    *((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data);
    brelse(bh);
    if (s.s_magic != SUPER_MAGIC)
        /* No ram disk image present, assume normal floppy boot */
        return; /* 文件系统非MINIX1.0则返回 */

    /* 计算文件系统数据逻辑块扇区数,若大于虚拟硬盘内存长度则返回 */
    nblocks = s.s_nzones << s.s_log_zone_size;
    if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
        printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
            nblocks, rd_length >> BLOCK_SIZE_BITS);
        return;
    }
    printk("Loading %d bytes into ram disk... 0000k", 
        nblocks << BLOCK_SIZE_BITS);

    /* 将软盘上的文件系统拷贝到虚拟硬盘内存段中 */
    cp = rd_start; /* 虚拟硬盘内存段首地址 */
    while (nblocks) {
        if (nblocks > 2) /* 剩余扇区大于2则以预读的方式读取软盘 */
            bh = breada(ROOT_DEV, block, block+1, block+2, -1);
        else /* 读软盘 */
            bh = bread(ROOT_DEV, block);
        if (!bh) {
            printk("I/O error on block %d, aborting load\n", 
                block);
            return;
        }
        /* 将缓冲区块中的软盘数据拷贝到虚拟硬盘内存段中 */
        (void) memcpy(cp, bh->b_data, BLOCK_SIZE);
        brelse(bh);
        printk("\010\010\010\010\010%4dk",i);
        cp += BLOCK_SIZE;
        block++;
        nblocks--;
        i++;
    }
    printk("\010\010\010\010\010done \n");
    ROOT_DEV=0x0101; /* 设置根文件系统设备为虚拟硬盘 */
}

/* 粗略从不同层次总结块设备请求管理程序,
 * 以文件概念封装处的系统调用(如open,read,write)。
 *                       |
 *                       v
 * 将文件相关参数转换为设备逻辑层面相关参数(如block_read,block_write)。
 *                       |
 *                       V
 * 将对设备逻辑块号相关参数转换为
 * "请求"相关参数的概念组织(如ll_rw_block,make_request,add_request),
 * 以电梯升梯算法调度各请求,不同设备具有不同的请求函数。
 *                       |
 *                       V
 * 块设备请求函数将向设备控制器下发(如do_*_request)实际的请求命令(读写命令,磁道扇
 * 区号等),块设备收到命令就绪后不断向PIC输出中断让CPU执行相应的块设备请求中断函数
 * do_hd读写块设备准备好的数据,直到完成当前设备请求。块设备(硬盘,软盘,虚拟硬盘)相
 * 应请求函数调度一个请求后将调度自动调度下一请求。*/
