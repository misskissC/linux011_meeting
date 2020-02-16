#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

/* struct stat,
 * 描述一个文件或目录相关信息的结构体类型。*/
struct stat {
    dev_t   st_dev;   /* 设备号 */
    ino_t   st_ino;   /* i节点号 */
    umode_t st_mode;  /* 文件类型和rwx属性 */
    nlink_t st_nlink; /* 链接数 */
    uid_t   st_uid;   /* 用户id */
    gid_t   st_gid;   /* 组id */
    dev_t   st_rdev;  /* 字符设备或块设备文件设备号 */
    off_t   st_size;  /* 常规文件大小 */
    time_t  st_atime; /* 文件i节点最后访问时间 */
    time_t  st_mtime; /* 文件最后修改时间 */
    time_t  st_ctime; /* 文件i节点最后修改时间 */
};

#define S_IFMT  00170000 /* 文件类型屏蔽码 */
#define S_IFREG 0100000  /* 常规文件码 */
#define S_IFBLK 0060000  /* 块设备文件码 */
#define S_IFDIR 0040000  /* 目录文件码 */
#define S_IFCHR 0020000  /* 字符设备文件码 */
#define S_IFIFO 0010000  /* FIFO文件码 */

#define S_ISUID 0004000  /* 执行时设置进程有效id为其宿主id标志 */
#define S_ISGID 0002000  /* 执行时设置进程组id为其宿主组id标志 */
#define S_ISVTX 0001000  /* 目录删除受限标志 */

/* 判断文件类型宏,宏值为1表明文件为
 * 常规文件;目录;字符设备文件;块设备文件;FIFO文件。*/
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

/* 文件访问权限,见fs.h中i节点i_mode */
#define S_IRWXU 00700 /* 宿主可读写执行标志 */
#define S_IRUSR 00400 /* 宿主可读标志 */
#define S_IWUSR 00200 /* 宿主可写标志 */
#define S_IXUSR 00100 /* 宿主可执行标志 */

#define S_IRWXG 00070 /* 组内用户可读写执行标志 */
#define S_IRGRP 00040 /* 组内用户可读标志 */
#define S_IWGRP 00020 /* 组内用户可写标志 */
#define S_IXGRP 00010 /* 组内用户可执行标志 */

#define S_IRWXO 00007 /* 其他用户可读写执行标志 */
#define S_IROTH 00004 /* 其他用户可读标志 */
#define S_IWOTH 00002 /* 其他用户可写标志 */
#define S_IXOTH 00001 /* 其他用户可执行标志 */

extern int chmod(const char *_path, mode_t mode);
extern int fstat(int fildes, struct stat *stat_buf);
extern int mkdir(const char *_path, mode_t mode);
extern int mkfifo(const char *_path, mode_t mode);
extern int stat(const char *filename, struct stat *stat_buf);
extern mode_t umask(mode_t mask);

#endif
