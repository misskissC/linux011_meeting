#ifndef _ERRNO_H
#define _ERRNO_H

/*
 * ok, as I hadn't got any other source of information about
 * possible error numbers, I was forced to use the same numbers
 * as minix.
 * Hopefully these are posix or something. I wouldn't know (and posix
 * isn't telling me - they want $$$ for their f***ing standard).
 *
 * We don't use the _SIGN cludge of minix, so kernel returns must
 * see to the sign by themselves.
 *
 * NOTE! Remember to change strerror() if you change this file!
 *
 * 由于寡人没有从其他源码中了解过关于错误码的定义,所以此处延用minix中
 * 定义的错误码。我不知道这是否能与posix或其他标准匹配,但孤希望如此。
 * (主要是posix也木有告知我——他们设计标准时总是向美刀看齐)
 *
 * 此文不用minix中定义的_SIGN,所以在内核中返回这些错误码时需手动添加负号。
 *
 * 注,若修改了本文件,则也需同步修改strerror()。
 */

/* 声明定义在errno.c中的全局变量errno */
extern int errno;

/* 当内核出错时则将相应错误码赋值给
 * errno变量,粗略领略下错误码含义吧。*/
#define ERROR   99  /* 普通错误 */
#define EPERM   1   /* 无操作权限 */
#define ENOENT  2   /* (文件或目录)项不存在 */
#define ESRCH   3   /* 进程不存在 */
#define EINTR   4   /* (阻塞型)系统调用被信号中断(唤醒)错误码 */
#define EIO     5   /* 输入/输出错误码 */
#define ENXIO   6   /* I/O不存在 */
#define E2BIG   7   /* 参数过多 */
#define ENOEXEC 8   /* 可执行文件格式错误 */
#define EBADF   9   /* 文件描述符错误 */
#define ECHILD  10  /* 子进程不存在 */
#define EAGAIN  11  /* 资源暂时不可用 */
#define ENOMEM  12  /* 内存暂无 */
#define EACCES  13  /* 无访问权限 */
#define EFAULT  14  /* 地址错误 */
#define ENOTBLK 15  /* 非块设备 */
#define EBUSY   16  /* 资源处于忙碌状态 */
#define EEXIST  17  /* 文件已存在 */
#define EXDEV   18  /* 跨设备链接 */
#define ENODEV  19  /* 无此设备 */
#define ENOTDIR 20  /* 非目录 */
#define EISDIR  21  /* 目录 */
#define EINVAL  22  /* 无效参数 */
#define ENFILE  23  /* 文件表溢出(无空闲) */
#define EMFILE  24  /* 达文件打开上限 */
#define ENOTTY  25  /* 无TTY终端 */
#define ETXTBSY 26  /* 文本文件忙,不可使用 */
#define EFBIG   27  /* 文件太大 */
#define ENOSPC  28  /* 设备无剩余空间 */
#define ESPIPE  29  /* 文件指针重定位非法 */
#define EROFS   30  /* 文件系统只读 */
#define EMLINK  31  /* 链接(link)过多 */
#define EPIPE   32  /* 管道错误 */
#define EDOM    33  /* 超函数域 */
#define ERANGE  34  /* 超最大结果 */
#define EDEADLK 35  /* 资源将发生死锁 */
#define ENAMETOOLONG 36  /* 文件名过长 */
#define ENOLCK  37  /* 无锁资源 */
#define ENOSYS  38  /* 未实现 */
#define ENOTEMPTY   39 /* 非空目录 */

#endif
