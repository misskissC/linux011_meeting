#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* open/fcntl - NOCTTY, NDELAY isn't implemented yet */
#define O_ACCMODE 00003 /* 打开文件访问属性屏蔽码 */
#define O_RDONLY  00    /* 只读打开文件标志 */
#define O_WRONLY  01    /* 只写打开文件标志 */
#define O_RDWR    02    /* 读写打开文件标志 */
#define O_CREAT   00100 /* 文件不存在则创建 *//* not fcntl */
#define O_EXCL    00200 /* 独占文件标志 */
#define O_NOCTTY  00400 /* 无终端标志 */
#define O_TRUNC   01000 /* 打开文件后写时清0标志 *//* not fcntl */
#define O_APPEND  02000 /* 追加方式打开文件标志*/
#define O_NONBLOCK 04000 /* 非阻塞方式打开文件 */
#define O_NDELAY   O_NONBLOCK

/* Defines for fcntl-commands. Note that currently
 * locking isn't supported, and other things aren't really
 * tested.
 */
/* 对应sys_fcntl的cmd参数 */
#define F_DUPFD 0   /* dup */
#define F_GETFD 1   /* get f_flags */
#define F_SETFD 2   /* set f_flags */
#define F_GETFL 3   /* more flags (cloexec) */
#define F_SETFL 4   /* set f_flags */
#define F_GETLK 5   /* not implemented */
#define F_SETLK 6
#define F_SETLKW 7

/* for F_[GET|SET]FL */
#define FD_CLOEXEC  1 /* actually anything with low bit set goes */

/* Ok, these are locking features, and aren't implemented at any
 * level. POSIX wants them.
 */
#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

/* Once again - not implemented, but ... */
struct flock {
    short l_type;
    short l_whence;
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
};

extern int creat(const char * filename,mode_t mode);
extern int fcntl(int fildes,int cmd, ...);
extern int open(const char * filename, int flags, ...);

#endif