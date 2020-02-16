/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);

/* release,
 * 释放p所指向结构体所占内存。*/
void release(struct task_struct * p)
{
    int i;

    if (!p)
        return;
    /* 遍历地址为p的结构体,释放其内存并置空后进行任务调度 */
    for (i=1 ; i<NR_TASKS ; i++)
        if (task[i]==p) {
            task[i]=NULL;
            free_page((long)p);
            schedule();
            return;
        }
    panic("trying to release non-existent task");
}

/* send_sig,
 * 若priv置位,则向p所指结构体所管理的进程发送sig信号。*/
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
    if (!p || sig<1 || sig>32)
        return -EINVAL;

    /* priv置位或当前进程与目标进程有效进程id相同或当前
     * 进程为超级进程则往p指向结构体所管理进程置sig信号。*/
    if (priv || (current->euid==p->euid) || suser())
        p->signal |= (1<<(sig-1));
    else
        return -EPERM;
    return 0;
}

/* kill_session,
 * 向与当前进程同会话的进程发送进程终止信号。*/
static void kill_session(void)
{
    struct task_struct **p = NR_TASKS + task;

    while (--p > &FIRST_TASK) {
        if (*p && (*p)->session == current->session)
            (*p)->signal |= 1<<(SIGHUP-1);
    }
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 *
 * 
 */
int sys_kill(int pid,int sig)
{
    struct task_struct **p = NR_TASKS + task;
    int err, retval = 0;

    /* 当pid=0时,则向所有进程组id为当前进程id的进程发送sig信号 */
    if (!pid) while (--p > &FIRST_TASK) {
        if (*p && (*p)->pgrp == current->pid) 
            if (err=send_sig(sig,*p,1))
                retval = err;
    /* 若当前进程有足够权限(超级进程或与目标进程有效用户id相同)
     * 则向进程id为pid的进程发送sig信号 */
    } else if (pid>0) while (--p > &FIRST_TASK) {
        if (*p && (*p)->pid == pid) 
            if (err=send_sig(sig,*p,0))
                retval = err;
    /* 若pid为-1,则向所有进程尝试发送sig信号 */
    } else if (pid == -1) while (--p > &FIRST_TASK)
        if (err = send_sig(sig,*p,0))
            retval = err;
    /* 若pid小于-1,则将pid取反后当pid大于0情况处理 */
    else while (--p > &FIRST_TASK)
        if (*p && (*p)->pgrp == -pid)
            if (err = send_sig(sig,*p,0))
                retval = err;
    return retval;
}

/* tell_father,
 * 向父进程(pid)发送当前进程已停止的信号。*/
static void tell_father(int pid)
{
    int i;

    if (pid)
        for (i=0;i<NR_TASKS;i++) {
            if (!task[i])
                continue;
        if (task[i]->pid != pid)
            continue;
        task[i]->signal |= (1<<(SIGCHLD-1));
        return;
    }
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
/* 若当前进程没有父进程,则自我释放管理当前进程的结构体,不提倡自我释放管理当前
 * 进程的结构体,在没有找到父进程时应该do_exit中将其父进程id设置为1(init进程)。*/
    printk("BAD BAD - no father found\n\r");
    release(current);
}

/* do_exit,
 * 同步或释放当前进程所占资源,然后退出当前进程。*/
int do_exit(long code)
{
    int i;

    /* 释放当前进程数据段和代码段页表所占物理内存和页表所映射的物理内存页*/
    free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
    free_page_tables(get_base(current->ldt[2]),get_limit(0x17));

    /* 标记当前进程子进程的父进程id为1,若其子进程为僵尸进程则向init进程发送
     * SIGCHLD信号让init进程回收下管理当前进程子进程的结构体资源,见sys_waitpid。*/
    for (i=0 ; i<NR_TASKS ; i++)
        if (task[i] && task[i]->father == current->pid) {
            task[i]->father = 1;
            if (task[i]->state == TASK_ZOMBIE)
                /* assumption task[1] is always init */
                (void) send_sig(SIGCHLD, task[1], 1);
        }

    /* 关闭当前进程所打开的文件 */
    for (i=0 ; i<NR_OPEN ; i++)
        if (current->filp[i])
            sys_close(i);

    /* 同步当前目录,根目录,当前进程可执行文件i节点到设备并释放相应i节点 */
    iput(current->pwd);
    current->pwd=NULL;
    iput(current->root);
    current->root=NULL;
    iput(current->executable);
    current->executable=NULL;

    /* 若当前进程为会话首领且拥有终端则释放
     * 终端,清除当前进程使用协处理器的记录。*/
    if (current->leader && current->tty >= 0)
        tty_table[current->tty].pgrp = 0;
    if (last_task_used_math == current)
        last_task_used_math = NULL;

    /* 若当前进程为会话首领,则终止该会话下的所有进程 */
    if (current->leader)
        kill_session();

    /* 置当前进程为僵尸进程以标识管理该进程的结构体内存还未释放 */
    current->state = TASK_ZOMBIE;
    /* 置当前进程退出码以让回收该进程结构体资源的进程获取 */
    current->exit_code = code;

    /* 向父进程发送信号告知本进程已停止运行 */
    tell_father(current->father);
    
    /* 调度时间片最大的进程运行 */
    schedule();

    /* 已置当前进程为僵尸状态,本进程不会再被调度运行即
     * schedule()函数不会返回,此语句仅用于避免编译器的警告。*/
    return (-1); /* just to suppress warnings */
}

/* sys_exit,
 * 系统调用_exit内核入口函数。*/
int sys_exit(int error_code)
{
    return do_exit((error_code&0xff)<<8);
}

/* sys_waitpid,
 * 等待pid所指定子进程运行结束,并获取指定子进程运行结束退出码于stat_addr中。
 *
 * pid>0,等待进程id为pid的子进程结束;pid=0,等待与本进程组id相同的子进程结束;
 * pid=-1,等待任意子进程结束;pid<-1,等待组id为-pid的子进程结束。
 *
 * options=WNOHANG,不挂起等待即无指定子进程结束时返回0;
 * options=WUNTRACED,只等待处于僵尸状态的指定子进程。*/
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
    int flag, code;
    struct task_struct ** p;

    /* 写时拷贝当前进程数据段中的stat_addr */
    verify_area(stat_addr,4);
    
repeat:
    flag=0;
    for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
        /* 遍历系统当前进程管理结构体, */

        /* 跳过本进程及无关联进程的结构体元素 */
        if (!*p || *p == current)
            continue;
        /* 忽略父进程不为本进程的进程 */
        if ((*p)->father != current->pid)
            continue;
        /* pid>0时表等待指定的子进程id */
        if (pid>0) {
            if ((*p)->pid != pid)
                continue;
        /* pid=0时表等待与本进程组id相同的子进程 */
        } else if (!pid) {
            if ((*p)->pgrp != current->pgrp)
                continue;
        /* pid < -1时表等待组id为|pid|的子进程 */
        } else if (pid != -1) {
            if ((*p)->pgrp != -pid)
                continue;
        }
        /* pid=-1则表明等待任意的子进程结束 */
        switch ((*p)->state) {
            case TASK_STOPPED: /* 进程已停止 */
                /* 若不获取已停止进程状态则继续遍历 */
                if (!(options & WUNTRACED))
                    continue;
                /* 对于已停止进程,退出码为0x7f */
                put_fs_long(0x7f,stat_addr);
                return (*p)->pid; /* 返回子进程id */
            case TASK_ZOMBIE: /* 僵尸进程 */
                current->cutime += (*p)->utime;
                current->cstime += (*p)->stime;
                flag = (*p)->pid;
                code = (*p)->exit_code;
                release(*p); /* 回收僵尸进程资源 */
                /* 返回进程运行结束退出码和进程id */
                put_fs_long(code,stat_addr);
                return flag;
            default: /* 无子进程结束 */
                flag=1;
                continue;
        }
    }
    /* 若遍历完当前进程结构体仍无结束的子进程, */
    if (flag) {
        /* 若option为WNOHANG(不等待)则理解返回0, */
        if (options & WNOHANG)
            return 0;
        /* 否则将当前进程置为就绪状态并调度其他进程运行, */
        current->state=TASK_INTERRUPTIBLE;
        schedule();
        /* 本进程收到信号时会被重新置于可被调度状态从而从schedule()函数返回执行
         * 到此处。若本进程收到SIGCHLD信号则回到repeat处继续等待指定子进程结束。*/
        if (!(current->signal &= ~(1<<(SIGCHLD-1))))
            goto repeat;
        else
        /* 若当前进程被其它信号唤醒,则返回相应错误码。
         * 应用程序收到-EINTN返回值时应继续调用该函数。*/
        return -EINTR;
    }
    return -ECHILD;
}

