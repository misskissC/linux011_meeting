/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>

extern int rw_char(int rw,int dev, char * buf, int count, off_t * pos);
extern int read_pipe(struct m_inode * inode, char * buf, int count);
extern int write_pipe(struct m_inode * inode, char * buf, int count);
extern int block_read(int dev, off_t * pos, char * buf, int count);
extern int block_write(int dev, off_t * pos, char * buf, int count);
extern int file_read(struct m_inode * inode, struct file * filp,
        char * buf, int count);
extern int file_write(struct m_inode * inode, struct file * filp,
        char * buf, int count);

/* [3] sys_lseek,
 * 将fd对应文件中的origin位置偏移offset个字节,
 * 并返回文件经偏移后的位置。*/
int sys_lseek(unsigned int fd,off_t offset, int origin)
{
    struct file * file;
    int tmp;

    /* 由fd找到当前打开文件file,并检查当前进程对该文件是否有相关权限,
     * 从file就可以找到文件的i节点。*/
    if (fd >= NR_OPEN || !(file=current->filp[fd]) || !(file->f_inode)
        || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev)))
        return -EBADF;
    if (file->f_inode->i_pipe)
        return -ESPIPE;
    switch (origin) {
        case 0: /* 从文件开始处偏移 */
            if (offset<0) return -EINVAL;
            file->f_pos=offset;
            break;
        case 1: /* 从文件当前位置开始偏移 */
            if (file->f_pos+offset<0) return -EINVAL;
            file->f_pos += offset;
            break;
        case 2: /* 从文件末尾开始偏移 */
            if ((tmp=file->f_inode->i_size+offset) < 0)
                return -EINVAL;
            file->f_pos = tmp;
            break;
        default:
            return -EINVAL;
    }
    return file->f_pos;
}

/* [1] sys_read,
 * 从文件描述符对应文件中读取count字节到buf内存段中。*/
int sys_read(unsigned int fd,char * buf,int count)
{
    struct file * file;
    struct m_inode * inode;

    /* fd->file->inode,
     * 检查文件描述符fd的有效性,通过fd获取其对应的文件指针。*/
    if (fd>=NR_OPEN || count<0 || !(file=current->filp[fd]))
        return -EINVAL;
    if (!count)
        return 0;
    
    /* 写时拷贝buf所在内存页 */
    verify_area(buf,count);

    /* 通过i节点读取文件,管道,字符设备文件,块设备文件,目录或常规文件 */
    inode = file->f_inode;
    if (inode->i_pipe)
        return (file->f_mode&1)?read_pipe(inode,buf,count):-EIO;
    if (S_ISCHR(inode->i_mode))
        return rw_char(READ,inode->i_zone[0],buf,count,&file->f_pos);
    if (S_ISBLK(inode->i_mode))
        return block_read(inode->i_zone[0],&file->f_pos,buf,count);
    if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {
        if (count+file->f_pos > inode->i_size)
            count = inode->i_size - file->f_pos;
        if (count<=0)
            return 0;
        return file_read(inode,file,buf,count);
    }
    printk("(Read)inode->i_mode=%06o\n\r",inode->i_mode);
    return -EINVAL;
}

/* [2] sys_write,
 * 将buf内存段中的count字节内容写入到fd对应的文件中。*/
int sys_write(unsigned int fd,char * buf,int count)
{
    struct file * file;
    struct m_inode * inode;

    /* fd->file->inode + 参数合法性检查*/
    if (fd>=NR_OPEN || count <0 || !(file=current->filp[fd]))
        return -EINVAL;
    if (!count)
        return 0;

    /* 根据fd对应i节点判断文件类型,并调用相应类型函数完成写操作。*/
    inode=file->f_inode;
    if (inode->i_pipe)
        return (file->f_mode&2)?write_pipe(inode,buf,count):-EIO;
    if (S_ISCHR(inode->i_mode))
        return rw_char(WRITE,inode->i_zone[0],buf,count,&file->f_pos);
    if (S_ISBLK(inode->i_mode))
        return block_write(inode->i_zone[0],&file->f_pos,buf,count);
    if (S_ISREG(inode->i_mode))
        return file_write(inode,file,buf,count);
    printk("(Write)inode->i_mode=%06o\n\r",inode->i_mode);
    return -EINVAL;
}
