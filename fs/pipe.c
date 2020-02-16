/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>

#include <linux/sched.h>
#include <linux/mm.h>	/* for get_free_page */
#include <asm/segment.h>

/* [1] read_pipe,
 * 从inode所指管道中读取count字节数据到buf内存段中。*/
int read_pipe(struct m_inode * inode, char * buf, int count)
{
    int chars, size, read = 0;

    while (count>0) {
        while (!(size=PIPE_SIZE(*inode))) {
            /* 若管道中没有数据,则唤醒写该管道的进程,
             * 并睡眠等待数据写入完毕。对于管道来说,
             * 只有读管道和写管道两个进程,分别由i节点
             * 的z_zone[1]和z_zone[0]指向管道数据区头尾两端。*/
            wake_up(&inode->i_wait);
            if (inode->i_count != 2) /* are there any writers? */
                return read;
            sleep_on(&inode->i_wait);
        }
        /* 计算此次能从管道中读取的最大字节数 */
        chars = PAGE_SIZE-PIPE_TAIL(*inode);
        if (chars > count)
            chars = count;
        if (chars > size)
            chars = size;
        /* 先更新各字段值,
         * count,剩余未读字节数;
         * read,已读字节数;
         * size,管道数据头位置;
         * 管道数据头前移chars字节,
         * 若超过管道缓冲区块总大小则
         * 从缓冲区块起始处重新开始(循环队列)。*/
        count -= chars;
        read += chars;
        size = PIPE_TAIL(*inode);
        PIPE_TAIL(*inode) += chars;
        PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
        /* 将管道中的数据拷贝到buf内存段中 */
        while (chars-->0)
            put_fs_byte(((char *)inode->i_size)[size++],buf++);
    }
    /* 读取完毕后唤醒写管道进程 */
    wake_up(&inode->i_wait);
    return read;
}

/* [2] write_pipe,
 * 往inode所指管道中写buf内存段中的数据,共写count字节。*/
int write_pipe(struct m_inode * inode, char * buf, int count)
{
    int chars, size, written = 0;

    while (count>0) {
        while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
            /* 若管道无剩余写空间则唤醒读管道的任务读管道,
             * 并进入睡眠等待读管道任务在读完数据后的唤醒。*/
            wake_up(&inode->i_wait);
            if (inode->i_count != 2) { /* no readers */
                current->signal |= (1<<(SIGPIPE-1));
                return written?written:-1;
            }
            sleep_on(&inode->i_wait);
        }
        /* 计算能写入管道中的最大字节数 */
        chars = PAGE_SIZE-PIPE_HEAD(*inode);
        if (chars > count)
            chars = count;
        if (chars > size)
            chars = size;

        /* 提前更新各字段值,
         * count, 剩余未写字节数;
         * written,已写字节数;
         * size,数据头位置;
         * 以循环队列方式更新管道数据头的位置。*/
        count -= chars;
        written += chars;
        size = PIPE_HEAD(*inode);
        PIPE_HEAD(*inode) += chars;
        PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
        /* 将buf内存段中的数据写入管道中 */
        while (chars-->0)
            ((char *)inode->i_size)[size++]=get_fs_byte(buf++);
    }
    /* 写入完毕唤醒读管道的进程 */
    wake_up(&inode->i_wait);
    return written;
}

/* sys_pipe,
 * 在文件内存段中寻找两个文件元素分别
 * 以读写属性指向管道,最终将文件地址输出给fildes。*/
int sys_pipe(unsigned long * fildes)
{
    struct m_inode * inode;
    struct file * f[2];
    int fd[2];
    int i,j;

    /* 在文件静态表中寻找两个空闲
     * 的文件元素分别赋给f数组,若寻找失败则返回-1。*/
    j=0;
    for(i=0;j<2 && i<NR_FILE;i++)
        if (!file_table[i].f_count)
            (f[j++]=i+file_table)->f_count++;
    if (j==1)
        f[0]->f_count=0;
    if (j<2)
        return -1;

    /* 将f数组中赋给当前任务两个空闲的文件元素,
     * 若当前任务没有两个空闲的文件元素,则返回-1.*/
    j=0;
    for(i=0;j<2 && i<NR_OPEN;i++)
        if (!current->filp[i]) {
            current->filp[ fd[j]=i ] = f[j];
            j++;
        }
    if (j==1)
        current->filp[fd[0]]=NULL;
    if (j<2) {
        f[0]->f_count=f[1]->f_count=0;
        return -1;
    }

    /* 在i节点内存段中寻找一个空闲i节点用作管道i节点 */
    if (!(inode=get_pipe_inode())) {
        current->filp[fd[0]] =
            current->filp[fd[1]] = NULL;
        f[0]->f_count = f[1]->f_count = 0;
        return -1;
    }
    /* f数组共同指向同一个inode节点,
     * 文件偏移初始化为0,
     * f[0]对管道只有读权限,
     * f[1]对管道只有写权限。*/
    f[0]->f_inode = f[1]->f_inode = inode;
    f[0]->f_pos = f[1]->f_pos = 0;
    f[0]->f_mode = 1;   /* read */
    f[1]->f_mode = 2;   /* write */

    /* 将读管道和写管道的文件地址分别赋给fildes */
    put_fs_long(fd[0],0+fildes);
    put_fs_long(fd[1],1+fildes);
    return 0;
}
