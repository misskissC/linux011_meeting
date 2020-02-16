#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

typedef int sig_atomic_t;
typedef unsigned int sigset_t; /* 32 bits */

#define _NSIG   32
#define NSIG    _NSIG /* 信号种类 */

/* 粗略领略进程各种信号 */
#define SIGHUP      1 /* 进程终止信号 */
#define SIGINT      2 /* 键盘中断信号 */
#define SIGQUIT     3 /* 键盘退出信号 */
#define SIGILL      4 /* 非法指令信号 */
#define SIGTRAP     5 /* 断点跟踪信号 */
#define SIGABRT     6 /* 异常结束信号 */
#define SIGIOT      6 /* 同SIGABRT */
#define SIGUNUSED   7 /* 保留未用 */
#define SIGFPE      8 /* 协处理器出错信号 */
#define SIGKILL     9 /* 强迫进程终止信号 */
#define SIGUSR1     10 /* 用户信号1 */
#define SIGSEGV     11 /* 非法访问内存信号 */
#define SIGUSR2     12 /* 用户信号2 */
#define SIGPIPE     13 /* 管道写错误信号 */
#define SIGALRM     14 /* 超时报警信号 */
#define SIGTERM     15 /* 进程终止信号 */
#define SIGSTKFLT   16 /* 协处理器栈出错信号 */
#define SIGCHLD     17 /* 子进程运行结束或被终止 */
#define SIGCONT     18 /* 恢复进程继续执行信号 */
#define SIGSTOP     19 /* 暂停进程的执行信号 */
#define SIGTSTP     20 /* 来自tty的进程停止信号 */
#define SIGTTIN     21 /* 后台进程请求输入信号 */
#define SIGTTOU     22 /* 后台进程请求输出信号 */

/* Ok, I haven't implemented sigactions, but trying to keep headers POSIX */
#define SA_NOCLDSTOP 1 /* 若子进程处于停止状态则不处理SIGCHLD */
#define SA_NOMASK  0x40000000 /* 在用户设定的信号处理函数中可再收该信号 */
#define SA_ONESHOT 0x80000000 /* 信号处理函数指针一旦被调用过就恢复到默认值0 */

#define SIG_BLOCK   0 /* for blocking signals */
#define SIG_UNBLOCK 1 /* for unblocking signals */
#define SIG_SETMASK 2 /* for setting the signal mask */

/* 信号处理函数 */
#define SIG_DFL ((void (*)(int))0) /* default signal handling */
#define SIG_IGN ((void (*)(int))1) /* ignore signal */

/* struct sigaction,
 * 信号处理结构体类型。*/
struct sigaction {
    void (*sa_handler)(int); /* 信号处理函数指针 */
    sigset_t sa_mask; /* 信号屏蔽位 */
    int sa_flags;     /* 信号处理标志(SA_NOMASK etc.) */
    void (*sa_restorer)(void); /* 信号恢复用户现场函数指针 */
};

void (*signal(int _sig, void (*_func)(int)))(int);
int raise(int sig);
int kill(pid_t pid, int sig);
int sigaddset(sigset_t *mask, int signo);
int sigdelset(sigset_t *mask, int signo);
int sigemptyset(sigset_t *mask);
int sigfillset(sigset_t *mask);
int sigismember(sigset_t *mask, int signo); /* 1 - is, 0 - not, -1 error */
int sigpending(sigset_t *set);
int sigprocmask(int how, sigset_t *set, sigset_t *oldset);
int sigsuspend(sigset_t *sigmask);
int sigaction(int sig, struct sigaction *act, struct sigaction *oldact);

#endif /* _SIGNAL_H */
