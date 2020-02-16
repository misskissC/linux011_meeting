/*
 *  linux/fs/truncate.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>

#include <sys/stat.h>

/* [2] free_ind,
 * 释放i节点i_zone[7]所指向的逻辑块。*/
static void free_ind(int dev,int block)
{
    struct buffer_head * bh;
    unsigned short * p;
    int i;

    if (!block)
        return;

    /* 读取保存逻辑块号的逻辑块到缓冲区块中, */
    if (bh=bread(dev,block)) {
        /* 释放i_zone[7]所指逻辑块中逻辑块
         * 号所对应的逻辑块。*/
        p = (unsigned short *) bh->b_data;
        for (i=0;i<512;i++,p++)
            if (*p)
                free_block(dev,*p);
        brelse(bh);
    }
    /* 最后释放i_zone[7]所指逻辑块 */
    free_block(dev,block);
}

/* [3] free_dind,
 * 释放i节点i_zone[8]所指向的所有逻辑块。*/
static void free_dind(int dev,int block)
{
    struct buffer_head * bh;
    unsigned short * p;
    int i;

    if (!block)
        return;
    /* 读取i_zone[8]对应逻辑块到缓冲区块中, */
    if (bh=bread(dev,block)) {
        p = (unsigned short *) bh->b_data;
        /* 释放1级逻辑块 */
        for (i=0;i<512;i++,p++)
            if (*p)
                free_ind(dev,*p);
        brelse(bh);
    }
    /* 最后释放i_zone[8]对应的逻辑块*/
    free_block(dev,block);
}

/* [1] truncate,
 * 将inode所指i节点对应目录或文件数据清0.*/
void truncate(struct m_inode * inode)
{
    int i;

    if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
        return;

    /* 释放inode所指i节点的逻辑块,
     * 清保存数据逻辑块号的数组。*/
    for (i=0;i<7;i++)
        if (inode->i_zone[i]) {
            free_block(inode->i_dev,inode->i_zone[i]);
            inode->i_zone[i]=0;
        }
    free_ind(inode->i_dev,inode->i_zone[7]);
    free_dind(inode->i_dev,inode->i_zone[8]);
    inode->i_zone[7] = inode->i_zone[8] = 0;

    /* 清inode所指i节点尺寸,
     * 置inode所指i节点已修改标志,
     * 置文件和i节点最后修改时间。*/
    inode->i_size = 0;
    inode->i_dirt = 1;
    inode->i_mtime = inode->i_ctime = CURRENT_TIME;
}
