/*
 *  linux/fs/fcntl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <fcntl.h>
#include <sys/stat.h>

extern int sys_close(int fd);

/* dupfd,
 * 从文件描述符arg开始查找,当查找到未用描述符时,将该
 * 文件描述符和文件描述符fd所关联文件关联。*/
static int dupfd(unsigned int fd, unsigned int arg)
{
    if (fd >= NR_OPEN || !current->filp[fd])
        return -EBADF;
    if (arg >= NR_OPEN)
        return -EINVAL;

    /* 遍历current->filp[arg..NR_OPEN-1]中首个空闲者 */
    while (arg < NR_OPEN)
        if (current->filp[arg])
            arg++;
        else
            break;
    if (arg >= NR_OPEN)
        return -EMFILE;

    /* 标识filp[arg]已关联文件 */
    current->close_on_exec &= ~(1<<arg);
    /* 将filp[arg]与filp[fd]所所关联文件关联,增加该文件引用计数 */
    (current->filp[arg] = current->filp[fd])->f_count++;
    return arg;
}

/* sys_dup2,
 * 关闭文件描述符newfd,将newfd关联文件让给oldfd或后续空闲描述符关联。*/
int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
    sys_close(newfd);
    return dupfd(oldfd,newfd);
}

/* sys_dup,
 * 将文件描述符fildes所关联文件让本
 * 进程中首个(从小往大搜索)空闲文件描述符关联。*/
int sys_dup(unsigned int fildes)
{
    return dupfd(fildes,0);
}

/* sys_fcntl,
 * 根据cmd和arg参数设置fd。
 * cmd为命令类型, arg=1为设置cmd,arg=0为不设置cmd。*/
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
    struct file * filp;

    if (fd >= NR_OPEN || !(filp = current->filp[fd]))
        return -EBADF;

    switch (cmd) {
        case F_DUPFD:
            return dupfd(fd,arg);
        case F_GETFD:
            return (current->close_on_exec>>fd)&1;
        case F_SETFD: /* 根据arg设置执行时关闭标志 */
            if (arg&1)
                current->close_on_exec |= (1<<fd);
            else
                current->close_on_exec &= ~(1<<fd);
            return 0;
        case F_GETFL:
            return filp->f_flags;
        case F_SETFL: /* 根据arg参数设置文件追加和非阻塞标志 */
            filp->f_flags &= ~(O_APPEND | O_NONBLOCK);
            filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
            return 0;
        case F_GETLK: case F_SETLK: case F_SETLKW:
            return -1;
        default:
            return -1;
    }
}
