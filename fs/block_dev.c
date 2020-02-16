/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

/* block_write,
 * 将buf中的count字节内容写入块设备号dev对应设备上,
 * 从dev对应设备数据区的pos偏移处开始写起;返回成功写入字节数。*/
int block_write(int dev, long * pos, char * buf, int count)
{
    /* 计算pos所对应的逻辑块号和在逻辑块中的偏移 */
    int block = *pos >> BLOCK_SIZE_BITS;
    int offset = *pos & (BLOCK_SIZE-1);
    int chars;
    int written = 0;
    struct buffer_head * bh;
    register char * p;

    while (count>0) {
        /* 计算此次能写入的最大字节数 */
        chars = BLOCK_SIZE - offset;
        if (chars > count)
            chars=count;
        /* 若此次写入字节数刚好为1块则将block块号对应
         * 逻辑块读入缓冲区块中,否则除block对应逻辑块
         * 外再预读两个逻辑块。*/
        if (chars == BLOCK_SIZE)
            bh = getblk(dev,block);
        else
            bh = breada(dev,block,block+1,block+2,-1);
        block++;
        if (!bh)
            return written?written:-EIO;
        /* 计算写入位置,更新写入字节数,更新未写入的剩余字节数 */
        p = offset + bh->b_data;
        offset = 0;
        *pos += chars;
        written += chars;
        count -= chars;
        /* 将buf内存段中的内容写入到缓冲区块中 */
        while (chars-->0)
            *(p++) = get_fs_byte(buf++);
        /* 置缓冲区块已修改标志以标识该缓冲区块可同步到磁盘中 */
        bh->b_dirt = 1;
        brelse(bh);
    }
    return written;
}

/* block_read,
 * 从块设备号dev对应设备中偏移pos处开始读取count字节到buf中。
 * 返回所读取字节数。*/
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
    /* 根据文件内容当前偏移计算该偏移的逻辑块号和在此逻辑块中的偏移。*/
    int block = *pos >> BLOCK_SIZE_BITS;
    int offset = *pos & (BLOCK_SIZE-1);
    int chars;
    int read = 0;
    struct buffer_head * bh;
    register char * p;

    while (count>0) {
        /* 此次读取字节=min(当前块还身下的字节数,count) */
        chars = BLOCK_SIZE-offset;
        if (chars > count)
            chars = count;
        /* 从设备分区号dev中读取逻辑块号block对应的逻辑块,
         * 并预读其后两块逻辑块。*/
        if (!(bh = breada(dev,block,block+1,block+2,-1)))
            return read?read:-EIO;
        block++;
        p = offset + bh->b_data;
        offset = 0;
        /* 偏移,已读,剩余未读量更新 */
        *pos += chars;
        read += chars;
        count -= chars;
        /* 将缓冲区块中所需数据拷贝到出参buf中 */
        while (chars-->0)
            put_fs_byte(*(p++),buf++);
        brelse(bh);
    }
    return read;
}
