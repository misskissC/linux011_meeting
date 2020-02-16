/*
 *  linux/fs/file_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* file_read,
 * 从inode所指i节点对应文件当前位置读取count字节到buf内存段中。
 * 函数返回读取成功的字节数,若读取0字节则返回错误号。*/
int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
    int left,chars,nr;
    struct buffer_head * bh;

    if ((left=count)<=0)
        return 0;
    
    while (left) {
        /* 将文件当前偏移换算为逻辑块单位,获取其逻辑块号nr;
         * 读取inode所指i节点逻辑块号为nr的逻辑块到缓冲区块中。*/
        if (nr = bmap(inode,(filp->f_pos)/BLOCK_SIZE)) {
            if (!(bh=bread(inode->i_dev,nr)))
                break;
        } else
            bh = NULL;
        /* 计算能从当前缓冲区块中获取数据的最大字节数,
         * 后移文件偏移及剩余未读字节数。*/
        nr = filp->f_pos % BLOCK_SIZE;
        chars = MIN( BLOCK_SIZE-nr , left );
        filp->f_pos += chars;
        left -= chars;
        if (bh) { /* 将所读内容拷贝到出参buf中 */
            char * p = nr + bh->b_data;
            while (chars-->0)
                put_fs_byte(*(p++),buf++);
            brelse(bh);
        } else { /* 否则往buf内存段中填0 */
            while (chars-->0)
                put_fs_byte(0,buf++);
        }
    }
    /* 修改i节点最后访问时间 */
    inode->i_atime = CURRENT_TIME;
    return (count-left)?(count-left):-ERROR;
}

/* file_write,
 * 根据打开文件的访问属性,将buf内存段中的count字节内容写入inode所指i节点对应文件中。
 * 返回写入成功的字节数或写入0字节时的错误码。*/
int file_write(struct m_inode * inode, struct file * filp, char * buf, int count)
{
    off_t pos;
    int block,c;
    struct buffer_head * bh;
    char * p;
    int i=0;

/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
    /* 若该文件访问标志为追加内容,则从文件末尾处开始写,否则从当前位置开写。*/
    if (filp->f_flags & O_APPEND)
        pos = inode->i_size;
    else
        pos = filp->f_pos;
    while (i<count) {
        /* 将文件偏移位置pos换算为逻辑块单位,计算其在磁盘上的逻辑块号,
         * 然后将该逻辑块号对应的逻辑块读到缓冲区块中。*/
        if (!(block = create_block(inode,pos/BLOCK_SIZE)))
            break;
        if (!(bh=bread(inode->i_dev,block)))
            break;
        /* 计算写入位置和能写入的最大字节数 */
        c = pos % BLOCK_SIZE;
        p = c + bh->b_data;
        bh->b_dirt = 1; /* 置缓冲区块已修改标志 */
        c = BLOCK_SIZE-c;
        if (c > count-i) c = count-i;
        /* 更新文件偏移或文件尺寸及i节点修改标志 */
        pos += c;
        if (pos > inode->i_size) {
            inode->i_size = pos;
            inode->i_dirt = 1;
        }
        i += c; /* 已写入字节数更新 */
        
        /* 将buf内存段中的内容拷贝到缓冲区块 */
        while (c-->0)
            *(p++) = get_fs_byte(buf++);
        brelse(bh);
    }
    /* 修改文件最后被修改时间;
     * 若本文件访问属性无追加属性则
     * 调整文件偏移到当前读写位置处并
     * 修改inode节点最后被修改时间。*/
    inode->i_mtime = CURRENT_TIME;
    if (!(filp->f_flags & O_APPEND)) {
        filp->f_pos = pos;
        inode->i_ctime = CURRENT_TIME;
    }
    return (i?i:-1);
}
