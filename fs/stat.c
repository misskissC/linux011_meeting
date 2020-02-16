/*
 *  linux/fs/stat.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/stat.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

/* [1] cp_stat,
 * 将statbuf所需属性从inode所指i节点中拷贝过来。*/
static void cp_stat(struct m_inode * inode, struct stat * statbuf)
{
    /* include/sys/stat.h */
    struct stat tmp;
    int i;

    /* 写时拷贝statbuf所在内存页 */
    verify_area(statbuf,sizeof (* statbuf));

    /* 将inode所指i节点相应属性赋值给tmp */
    tmp.st_dev = inode->i_dev;
    tmp.st_ino = inode->i_num;
    tmp.st_mode = inode->i_mode;
    tmp.st_nlink = inode->i_nlinks;
    tmp.st_uid = inode->i_uid;
    tmp.st_gid = inode->i_gid;
    tmp.st_rdev = inode->i_zone[0];
    tmp.st_size = inode->i_size;
    tmp.st_atime = inode->i_atime;
    tmp.st_mtime = inode->i_mtime;
    tmp.st_ctime = inode->i_ctime;

    /* 将tmp的内容拷贝给statbuf所指内存段中 */
    for (i=0 ; i<sizeof (tmp) ; i++)
        put_fs_byte(((char *) &tmp)[i],&((char *) statbuf)[i]);
}

/* [2] sys_stat,
 * 获取filename的相关信息(struct stat)于statbuf所指内存中。*/
int sys_stat(char * filename, struct stat * statbuf)
{
    struct m_inode * inode;

    /* 获取filename的i节点 */
    if (!(inode=namei(filename)))
        return -ENOENT;
    /* 将i节点中跟struct stat结构体
     * 相关的属性拷贝给statbuf中。*/
    cp_stat(inode,statbuf);
    iput(inode);
    return 0;
}

/* [3] sys_fstat,
 * 获取文件描述符fd对应目录或文件的(struct stat)相关信息
 * 于statbuf所指内存段中。*/
int sys_fstat(unsigned int fd, struct stat * statbuf)
{
    struct file * f;
    struct m_inode * inode;

    if (fd >= NR_OPEN || !(f=current->filp[fd]) || !(inode=f->f_inode))
        return -EBADF;
    /* 从fd对应i节点中拷贝相关信息到statbuf中 */
    cp_stat(inode,statbuf);
    return 0;
}
