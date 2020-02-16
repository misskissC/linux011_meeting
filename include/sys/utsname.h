#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

#include <sys/types.h>

/* struct utsname,
 * 描述linux版本信息的结构体类型。*/
struct utsname {
    char sysname[9];
    char nodename[9];
    char release[9];
    char version[9];
    char machine[9];
};

extern int uname(struct utsname * utsbuf);

#endif
