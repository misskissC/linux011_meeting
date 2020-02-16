#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

/* 由typedef和数据类型定义一些具命名含义的类型,如若要定义
 * 跟时间相关的数据,则可统一使用time_t,统一使用time_t比统
 一使用long更好记一点,阅读性也可能会高一点。*/
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#ifndef _TIME_T
#define _TIME_T
typedef long time_t;
#endif

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef long ptrdiff_t;
#endif

#ifndef NULL
#define NULL ((void *) 0)
#endif

typedef int pid_t;
typedef unsigned short uid_t;
typedef unsigned char gid_t;
typedef unsigned short dev_t;
typedef unsigned short ino_t;
typedef unsigned short mode_t;
typedef unsigned short umode_t;
typedef unsigned char nlink_t;
typedef int daddr_t;
typedef long off_t;
typedef unsigned char u_char;
typedef unsigned short ushort;

typedef struct { int quot,rem; } div_t;
typedef struct { long quot,rem; } ldiv_t;

/* 用于ustat()参数的结构体类型,ustat()暂未实现 */
struct ustat {
    daddr_t f_tfree;
    ino_t f_tinode;
    char f_fname[6];
    char f_fpack[6];
};

#endif
