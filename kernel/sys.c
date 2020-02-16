/*
 *  linux/kernel/sys.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <sys/times.h>
#include <sys/utsname.h>

int sys_ftime()
{
    return -ENOSYS;
}

int sys_break()
{
    return -ENOSYS;
}

int sys_ptrace()
{
    return -ENOSYS;
}

int sys_stty()
{
    return -ENOSYS;
}

int sys_gtty()
{
    return -ENOSYS;
}

int sys_rename()
{
    return -ENOSYS;
}

int sys_prof()
{
    return -ENOSYS;
}

/* sys_setregid,
 * 设置当前进程实际组id和有效组id。*/
int sys_setregid(int rgid, int egid)
{
    if (rgid>0) {
        /* 超级进程可以设置其组id为实际组id */
        if ((current->gid == rgid) || 
            suser())
            current->gid = rgid;
        else
            return(-EPERM);
    }
    if (egid>0) {
        /* 超级进程或满足组id为有效组id..则设置进程有效组id为egid */
        if ((current->gid == egid) ||
            (current->egid == egid) ||
            (current->sgid == egid) ||
            suser())
            current->egid = egid;
        else
            return(-EPERM);
    }
    return 0;
}

/* sys_setgid,
 * 设置当前进程组id。*/
int sys_setgid(int gid)
{
    return(sys_setregid(gid, gid));
}

int sys_acct()
{
    return -ENOSYS;
}

int sys_phys()
{
    return -ENOSYS;
}

int sys_lock()
{
    return -ENOSYS;
}

int sys_mpx()
{
    return -ENOSYS;
}

int sys_ulimit()
{
    return -ENOSYS;
}

/* sys_time,
 * 获取自1970年1月1号0时0分0秒
 * 到现在的描述于tloc所指内存中。
 *
 * 该函数时系统调用time的内核函数。*/
int sys_time(long * tloc)
{
    int i;

    i = CURRENT_TIME;
    if (tloc) {
        /* 写时拷贝tloc所在内存页 */
        verify_area(tloc,4);
        /* 将当前时间(秒数)写到tloc所指用户内存中 */
        put_fs_long(i,(unsigned long *)tloc);
    }
    return i;
}

/*
 * Unprivileged users may change the real user id to the effective uid
 * or vice versa.
 */
/* sys_setreuid,
 * 设置进程实际和有效的用户id。*/
int sys_setreuid(int ruid, int euid)
{
    int old_ruid = current->uid;

    if (ruid>0) {
        /* 为超级进程或若进程有效用户id/用户id
         * 等于实际用户id则设置进程用户id为ruid。*/
        if ((current->euid==ruid) ||
            (old_ruid == ruid) ||
            suser())
            current->uid = ruid;
        else
            return(-EPERM);
    }
    if (euid>0) {
        /* 为超级进程或若当前进程用户id/有效用户
         * id为euid则设置进程有效用户id为euid。*/
        if ((old_ruid == euid) ||
            (current->euid == euid) ||
            suser())
            current->euid = euid;
        else {/* 否则当前用户id不变 */
            current->uid = old_ruid;
            return(-EPERM);
        }
    }
    return 0;
}

/* sys_setuid,
 * 设置进程用户id。*/
int sys_setuid(int uid)
{
    return(sys_setreuid(uid, uid));
}

/* sys_stime,
 * 用于超级进程设置系统开机时间为tptr所指时间。*/
int sys_stime(long * tptr)
{
    if (!suser())
        return -EPERM;
    startup_time = get_fs_long((unsigned long *)tptr) - jiffies/HZ;
    return 0;
}

/* sys_times,
 * 获取当今进程及其子进程用户态,内核态运行时间(单位为10ms)。*/
int sys_times(struct tms * tbuf)
{
    if (tbuf) {
        /* 写时拷贝tbuf所指内存段所在内存页 */
        verify_area(tbuf,sizeof *tbuf);
        /* 将时间拷贝到用户内存空间 */
        put_fs_long(current->utime,(unsigned long *)&tbuf->tms_utime);
        put_fs_long(current->stime,(unsigned long *)&tbuf->tms_stime);
        put_fs_long(current->cutime,(unsigned long *)&tbuf->tms_cutime);
        put_fs_long(current->cstime,(unsigned long *)&tbuf->tms_cstime);
    }
    return jiffies;
}

/* sys_brk,
 * 设置进程数据段大小(逻辑上)。*/
int sys_brk(unsigned long end_data_seg)
{
    /* 数据段大小需要满足
     * 大于进程代码段 && 能预留16Kb栈内存 */
    if (end_data_seg >= current->end_code &&
        end_data_seg < current->start_stack - 16384)
        current->brk = end_data_seg;
    return current->brk;
}

/*
 * This needs some heave checking ...
 * I just haven't get the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 */
/* sys_setpgid,
 * 设置进程id为pid进程的组id。*/
int sys_setpgid(int pid, int pgid)
{
    int i;

    if (!pid)
        pid = current->pid;
    if (!pgid)
        pgid = current->pid;
    for (i=0 ; i<NR_TASKS ; i++)
        if (task[i] && task[i]->pid==pid) {
            if (task[i]->leader) /* 不能设置会话领导进程的组id */
                return -EPERM;
            /* 当前进程与目标进程不属同一会话不能设置 */
            if (task[i]->session != current->session)
                return -EPERM;
            task[i]->pgrp = pgid;
            return 0;
        }
    return -ESRCH;
}

/* sys_getpgrp,
 * 获取当前进程组id。*/
int sys_getpgrp(void)
{
    return current->pgrp;
}

/* sys_setsid,
 * 在当前进程中创建会话,会话领导id为1。*/
int sys_setsid(void)
{
    /* 已是会话领导且不为超级进程则返回 */
    if (current->leader && !suser())
        return -EPERM;
    current->leader = 1;
    /* 当前进程会话号和当前进程组id=当前进程id*/
    current->session = current->pgrp = current->pid;
    current->tty = -1;
    return current->pgrp;
}

/* sys_uname,
 * 获取linux版本信息于name所指内存段中。*/
int sys_uname(struct utsname * name)
{
    static struct utsname thisname = {
        "linux .0","nodename","release ","version ","machine "
    };
    int i;

    /* 写时拷贝name地址映射的内存页 */
    if (!name) return -ERROR;
    verify_area(name,sizeof *name);

    /* 将内核空间的thisname内存段拷贝到name所指用户内存段 */
    for(i=0;i<sizeof *name;i++)
        put_fs_byte(((char *) &thisname)[i],i+(char *) name);
    return 0;
}

/* sys_umask,
 * 设置当前进程文件创建属性屏蔽位。*/
int sys_umask(int mask)
{
    int old = current->umask;

    current->umask = mask & 0777;
    return (old);
}
