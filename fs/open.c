/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>

int sys_ustat(int dev, struct ustat * ubuf)
{
    return -ENOSYS;
}

/* [4] sys_utime,
 * 修改filename最后修改时间和最后被访问时间。*/
int sys_utime(char * filename, struct utimbuf * times)
{
    struct m_inode * inode;
    long actime,modtime;

    /* 获取filename的i节点 */
    if (!(inode=namei(filename)))
        return -ENOENT;
    /* 若times参数为NULL则用当前时间顶替 */
    if (times) {
        actime = get_fs_long((unsigned long *) &times->actime);
        modtime = get_fs_long((unsigned long *) &times->modtime);
    } else
        actime = modtime = CURRENT_TIME;
    inode->i_atime = actime;
    inode->i_mtime = modtime;
    inode->i_dirt = 1;
    iput(inode);
    return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
/* [5] sys_access,
 * 检查当前任务对filename是否能用mode属性取访问,
 * 可以返回0,否则返回非法访问错误码。*/
int sys_access(const char * filename,int mode)
{
    struct m_inode * inode;
    int res, i_mode;

    /* 保留mode[0..2]即rwx权限 */
    mode &= 0007;
    
    /* 获取filename的i节点,
     * 从中获取filename属性后释放。*/
    if (!(inode=namei(filename)))
        return -EACCES;
    i_mode = res = inode->i_mode & 0777;
    iput(inode);

    /* 若filename文件i节点同当前任务
     * 用户或组id则取其bit[6..8]即宿主rwx权限。*/
    if (current->uid == inode->i_uid)
        res >>= 6;
    else if (current->gid == inode->i_gid)
        res >>= 6;
    
    /* 若当前任务拥有访问filename的mode权限则返回0 */
    if ((res & 0007 & mode) == mode)
        return 0;
    /*
     * XXX we are doing this test last because we really should be
     * swapping the effective with the real user id (temporarily),
     * and then calling suser() routine.  If we do call the
     * suser() routine, it needs to be called last. 
     */
    /* 超级用户且
     * 对filename不要求有执行权限或者任何任务都能执行filename文件。*/
    if ((!current->uid) &&
        (!(mode & 1) || (i_mode & 0111)))
        return 0;
    return -EACCES;
}

/* [6] sys_chdir,
 * 更改当前任务当前目录为filename。*/
int sys_chdir(const char * filename)
{
    struct m_inode * inode;

    /* 获取filename的i节点地址,
     * 查看其属性,若不为目录则返回非目录的错误码。
     * 若为目录则更改当前任务的当前目录i节点为
     * filename目录的i节点。*/
    if (!(inode = namei(filename)))
        return -ENOENT;
    if (!S_ISDIR(inode->i_mode)) {
        iput(inode);
        return -ENOTDIR;
    }
    iput(current->pwd);
    current->pwd = inode;
    return (0);
}

/* [7] sys_chroot,
 * 更改当前任务更目录为filename。*/
int sys_chroot(const char * filename)
{
    struct m_inode * inode;

    /* 获取filename对应i节点,
     * 若为目录则将当前任务根目录换位filename。*/
    if (!(inode=namei(filename)))
        return -ENOENT;
    if (!S_ISDIR(inode->i_mode)) {
        iput(inode);
        return -ENOTDIR;
    }
    iput(current->root);
    current->root = inode;
    return (0);
}

/* [8] sys_chmod,
 * 修改filename的属性为mode。*/
int sys_chmod(const char * filename,int mode)
{
    struct m_inode * inode;

    /* 获取filename对应i节点,
     * 只有当前任务有效用户id和文件用户id相同或
     * 当前任务为超级用户时有权修改filename的访问属性。*/
    if (!(inode=namei(filename)))
        return -ENOENT;
    if ((current->euid != inode->i_uid) && !suser()) {
        iput(inode);
        return -EACCES;
    }
    /* 修改filename对应i节点的访问属性并置i节点已被修改标志,
     * 以伺机同步回设备。*/
    inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
    inode->i_dirt = 1;
    iput(inode);
    return 0;
}

/* [9] sys_chown,
 * 修改filename的拥有者: 用户id和组id。*/
int sys_chown(const char * filename,int uid,int gid)
{
    struct m_inode * inode;

    /* 获取filename的i节点,
     * 若当前用户不为超级用户则不能更改filename的拥有者。*/
    if (!(inode=namei(filename)))
        return -ENOENT;
    if (!suser()) {
        iput(inode);
        return -EACCES;
    }
    inode->i_uid=uid;
    inode->i_gid=gid;
    inode->i_dirt=1;
    iput(inode);
    return 0;
}

/* [1] sys_open,
 * 以flag标识属性打开文件,若文件不存在则以mode所标识
 * 属性创建文件。
 * 
 * 为当前任务打开filename文件,成功打开后,该文件的fd为
 * 当前任务文件结构体空闲下标。*/
int sys_open(const char * filename,int flag,int mode)
{
    struct m_inode * inode;
    struct file * f;
    int i,fd;

    /* 计算文件被创建时应具备属性;
     * 为当前进程遍历空闲文件指针,
     * 该空闲文件指针下标即为文件描述符。*/
    mode &= 0777 & ~current->umask;
    for(fd=0 ; fd<NR_OPEN ; fd++)
        if (!current->filp[fd])
            break;
    if (fd>=NR_OPEN)
        return -EINVAL;
    current->close_on_exec &= ~(1<<fd);
    f=0+file_table;
    for (i=0 ; i<NR_FILE ; i++,f++)
        if (!f->f_count) break;
    if (i>=NR_FILE)
        return -EINVAL;
    (current->filp[fd]=f)->f_count++;

    /* 以flag访问属性打开filename, 其i节点地址将输出给inode */
    if ((i=open_namei(filename,flag,mode,&inode))<0) {
        current->filp[fd]=NULL;
        f->f_count=0;
        return i;
    }
/* ttys are somewhat special (ttyxx major==4, tty major==5) */
    /* 若(打开文件)为字符设备(文件),如终端,串口 */
    if (S_ISCHR(inode->i_mode))
        if (MAJOR(inode->i_zone[0])==4) {
            if (current->leader && current->tty<0) {
                current->tty = MINOR(inode->i_zone[0]);
                tty_table[current->tty].pgrp = current->pgrp;
            }
        } else if (MAJOR(inode->i_zone[0])==5)
            if (current->tty<0) {
                iput(inode);
                current->filp[fd]=NULL;
                f->f_count=0;
                return -EPERM;
            }
/* Likewise with block-devices: check for floppy_change */
    if (S_ISBLK(inode->i_mode))
        check_disk_change(inode->i_zone[0]);
    f->f_mode = inode->i_mode;
    f->f_flags = flag;
    f->f_count = 1;
    f->f_inode = inode;
    f->f_pos = 0;
    return (fd);
}

/* [2] sys_create,
 * 若果pathname不存在,则按照mode属性创建该文件,
 * 若pathname存在,则将该文件清0.*/
int sys_creat(const char * pathname, int mode)
{
    return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

/* [3] sys_close,
 * 在当前任务中关闭文件描述符fd对应的文件,
 * 并释放fd对应i节点所占资源。*/
int sys_close(unsigned int fd)
{
    struct file * filp;

    if (fd >= NR_OPEN)
        return -EINVAL;
    current->close_on_exec &= ~(1<<fd);
    if (!(filp = current->filp[fd]))
        return -EINVAL;
    current->filp[fd] = NULL;
    if (filp->f_count == 0)
        panic("Close: file count is 0");
    if (--filp->f_count)
        return (0);
    iput(filp->f_inode);
    return (0);
}
