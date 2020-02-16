#ifndef _TIMES_H
#define _TIMES_H

#include <sys/types.h>

/* struct tms,
 * 用于记录当前进程及其子进程用户态和内核态运行时间。*/
struct tms {
    time_t tms_utime;
    time_t tms_stime;
    time_t tms_cutime;
    time_t tms_cstime;
};

extern time_t times(struct tms * tp);

#endif
