/*
 *  linux/fs/ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <linux/sched.h>

extern int tty_ioctl(int dev, int cmd, int arg);

/* ioctl_ptr,
 * IO设置函数指针类型。
 * dev为IO设备号,
 * cmd和arg为设置参数。*/
typedef int (*ioctl_ptr)(int dev,int cmd,int arg);

/* 编译阶段计算ioctl_table数组的元素个数 */
#define NRDEVS ((sizeof (ioctl_table))/(sizeof (ioctl_ptr)))

/* io设置静态数组,
 * 目前只实现了串口(4)和终端(5)的IO设置函数。
 * tty_ioctl为字符设备驱动中的函数,届时再粗读吧。*/
static ioctl_ptr ioctl_table[]={
    NULL,   /* nodev */
    NULL,   /* /dev/mem */
    NULL,   /* /dev/fd */
    NULL,   /* /dev/hd */
    tty_ioctl,  /* /dev/ttyx */
    tty_ioctl,  /* /dev/tty */
    NULL,   /* /dev/lp */
    NULL};  /* named pipes */

/* sys_ioctl,
 * 调用fd对应的IO设置函数根据cmd和arg参数设置IO.*/
int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
    struct file * filp;
    int dev,mode;

    /* fd->file->inode, 检查参数合法性 */
    if (fd >= NR_OPEN || !(filp = current->filp[fd]))
        return -EBADF;

    /* 判断文件描述符fd对应文件的属性,
     * 若非字符设备或块设备文件则返回无效错误码。
     * 因为ioctl是设置I/O的函数,所以fd所对应的需为外设。*/
    mode=filp->f_inode->i_mode;
    if (!S_ISCHR(mode) && !S_ISBLK(mode))
        return -EINVAL;
    /* 判断设备号是否在ioctl_table数组内 */
    dev = filp->f_inode->i_zone[0];
    if (MAJOR(dev) >= NRDEVS)
        return -ENODEV;

    /* 调用设备号对应的IO设置函数设置IO相关 */
    if (!ioctl_table[MAJOR(dev)])
        return -ENOTTY;
    return ioctl_table[MAJOR(dev)](dev,cmd,arg);
}
