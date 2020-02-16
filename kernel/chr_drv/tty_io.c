/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>

#define ALRMMASK (1<<(SIGALRM-1))
#define KILLMASK (1<<(SIGKILL-1))
#define INTMASK (1<<(SIGINT-1))
#define QUITMASK (1<<(SIGQUIT-1))
#define TSTPMASK (1<<(SIGTSTP-1))

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <asm/system.h>

/* 判断字符设备本地模式,输入模式,
 * 输出模式中是否设置了标志f。
 *
 * 表达式值为1时表示已设置标志f。*/
#define _L_FLAG(tty,f)  ((tty)->termios.c_lflag & f)
#define _I_FLAG(tty,f)  ((tty)->termios.c_iflag & f)
#define _O_FLAG(tty,f)  ((tty)->termios.c_oflag & f)

/* 判断字符设备本地模式是否设置了
 * 规范标志,产生进程信号标志,回显标志,
 * 规范模式下的擦除标志,规范模式下的删除行标志,
 * 回显控制字符标志,回显删除行标志。
 *
 * 表达式值为1表示已设置。*/
#define L_CANON(tty)    _L_FLAG((tty),ICANON)
#define L_ISIG(tty)     _L_FLAG((tty),ISIG)
#define L_ECHO(tty)     _L_FLAG((tty),ECHO)
#define L_ECHOE(tty)    _L_FLAG((tty),ECHOE)
#define L_ECHOK(tty)    _L_FLAG((tty),ECHOK)
#define L_ECHOCTL(tty)  _L_FLAG((tty),ECHOCTL)
#define L_ECHOKE(tty)   _L_FLAG((tty),ECHOKE)

/* 判断字符设备输入模式是否设置了
 * 输入字符转大写字符标志,输入换行符转回车标志,
 * 输入回车转换行符标志,忽略回车标志。
 *
 * 表达式值为1表示已设置。*/
#define I_UCLC(tty) _I_FLAG((tty),IUCLC)
#define I_NLCR(tty) _I_FLAG((tty),INLCR)
#define I_CRNL(tty) _I_FLAG((tty),ICRNL)
#define I_NOCR(tty) _I_FLAG((tty),IGNCR)

/* 判断字符设备输出模式是否设置了
 * 输出处理标志,换行转回车标志,回车转换行标志,
 * 遇换行作回车处理标志,小写字符转大写字符标志。
 *
 * 表达时返回1时表示已设置。*/
#define O_POST(tty)  _O_FLAG((tty),OPOST)
#define O_NLCR(tty)  _O_FLAG((tty),ONLCR)
#define O_CRNL(tty)  _O_FLAG((tty),OCRNL)
#define O_NLRET(tty) _O_FLAG((tty),ONLRET)
#define O_LCUC(tty)  _O_FLAG((tty),OLCUC)

/* tty_table,
 * 管理字符设备数据接收和发送的全局数组。
 * tty_table[0] - 管理控制台终端(console)数据接收和发送;
 * tty_table[1] - 管理串口1数据接收和发送;
 * tty_table[2] - 管理串口2数据接收和发送。*/
struct tty_struct tty_table[] = {
    {
        {ICRNL,      /* change incoming CR to NL */
        OPOST|ONLCR, /* change outgoing NL to CRNL */
        0,           /* 控制模式初始化为0 */
        /* 产生进程信号;规范模式;回显;控制字符回显;删除行时回显 */
        ISIG | ICANON | ECHO | ECHOCTL | ECHOKE,
        0,          /* console termio */
        INIT_C_CC}, /* 控制字符序列 */
        0,          /* initial pgrp */
        0,          /* initial stopped */
        con_write,  /* 终端写队列被写入数据后的回调函数 */
        {0,0,0,0,""}, /* console read-queue */
        {0,0,0,0,""}, /* console write-queue */
        {0,0,0,0,""}  /* console secondary queue */
    },{
        {0, /* no translation */
        0,  /* no translation */
        B2400 | CS8, /* 波特率=2400,每字符8位 */
        0,           /* 本地模式初始化为0 */
        0,           /* 线路速率初始化为0 */
        INIT_C_CC},  /* 控制字符序列 */
        0,           /* 进程组号初始化为0 */
        0,           /* 停止标志初始化为0 */
        rs_write,    /* 串口1写队列被写入数据后的回调函数 */
        {0x3f8,0,0,0,""}, /* 0x3f8为串口1端口起始地址,串口1读队列初始化 */
        {0x3f8,0,0,0,""}, /* 串口1写队列初始化 */
        {0,0,0,0,""}      /* 串口1辅助队列初始化 */
    },{
        {0, /* no translation */
        0,  /* no translation */
        B2400 | CS8, /* 波特率=2400,每字符8位 */
        0,           /* 本地模式初始化为0 */
        0,           /* 线路速率初始化为0 */
        INIT_C_CC},  /* 控制字符序列 */
        0,           /* 进程组号初始化为0 */
        0,           /* 停止标志初始化为0 */
        rs_write,         /* 串口2写队列被写入数据后的回调函数 */
        {0x2f8,0,0,0,""}, /* 0x2f8为串口2端口起始地址,串口2读队列初始化 */
        {0x2f8,0,0,0,""}, /* 串口2写队列初始化 */
        {0,0,0,0,""}      /* 串口2辅助队列初始化 */
    }
};

/*
 * these are the tables used by the machine code handlers.
 * you can implement pseudo-tty's or something by changing
 * them. Currently not done.
 */
/* table_list 
 * -------------------------------------------
 * |      |      |      |      |      |      |
 * -------------------------------------------
 * +0     +4     +8     +12    +16    +20 
 * table_list +  8(12)为串口1读(写)队列首地址;
 * table_list + 16(20)为串口2读(写)队列首地址。*/
struct tty_queue * table_list[]={
    &tty_table[0].read_q, &tty_table[0].write_q,
    &tty_table[1].read_q, &tty_table[1].write_q,
    &tty_table[2].read_q, &tty_table[2].write_q
};

/* tty_init,
 * 初始化串口(UART)和控制台终端的通信方式(中断),激活键盘。*/
void tty_init(void)
{
    rs_init();
    con_init();
}

/* tty_intr,
 * 产生mask信号给tty所在进程组的进程。*/
void tty_intr(struct tty_struct * tty, int mask)
{
    int i;

    if (tty->pgrp <= 0)
        return;
    for (i=0;i<NR_TASKS;i++)
        if (task[i] && task[i]->pgrp==tty->pgrp)
            task[i]->signal |= mask;
}

/* sleep_if_empty,
 * 若当前进程无其他需处理的信号且queue指向的队列为空则进入睡眠。*/
static void sleep_if_empty(struct tty_queue * queue)
{
    cli(); /* 本进程睡眠过程中进制CPU处理中断 */
    while (!current->signal && EMPTY(*queue))
        /* 让本进程进入睡眠直到有其他进程调用
         * wake_up(&queue->proc_list)将本进程唤醒。
         *
         * interruptible_sleep_on跟sleep_on的区别为
         * 经interruptible_sleep_on函数睡眠的进程的
         * 状态为TASK_INTERRUPTIBLE,可被进程的signal
         * 将进程状态设置为TASK_RUNNING即重新运行;而
         * 后者只能通过显示设置进程状态为TASK_RUNNING
         * 时才能唤醒该进程。*/
        interruptible_sleep_on(&queue->proc_list);
    sti();
}

/* sleep_if_full,
 * 若当前进程无其他需处理的信号且queue所指队列已满则进入睡眠。*/
static void sleep_if_full(struct tty_queue * queue)
{
    if (!FULL(*queue))
        return;
    cli(); /* 睡眠过程中进制CPU处理本进程中断 */
    while (!current->signal && LEFT(*queue)<128)
        /* 进程无其他信号处理且队列空余数小于128时则睡眠
         * 直到被其它进程调用wake_up(&queue->proc_list)将
         * 本进程唤醒。interruptible_sleep_on跟sleep_on的
         * 区别为经interruptible_sleep_on函数睡眠的进程的
         * 状态为TASK_INTERRUPTIBLE,可被进程的signal将进程
         * 状态设置为TASK_RUNNING即重新运行;而后者只能通过
         * 显示设置进程状态为TASK_RUNNING时才能唤醒该进程。*/
        interruptible_sleep_on(&queue->proc_list);
    sti();
}

/* wait_for_keypress,
 * (让使用控制台终端的进程)等待键盘输入。*/
void wait_for_keypress(void)
{
    sleep_if_empty(&tty_table[0].secondary);
}

/* copy_to_cooked,
 * 将从字符设备所读字符(转换为规范模式)并存入辅助队列中。
 *
 * 字符设备控制器接收数据中断-->PIC-->CPU执行串口读中断
 * 处理函数(read_char)-->copy_to_cooked。当字符设备读队
 * 列中的所有字符被存入辅助队列中后,即可唤醒等待读字符设
 * 备辅助队列的进程,以接收到字符设备数据。*/
void copy_to_cooked(struct tty_struct * tty)
{
    signed char c;

    /* 字符设备读队列不为空且辅助队列非满 */
    while (!EMPTY(tty->read_q) && !FULL(tty->secondary)) {

        /* [1] 从字符设备读队列中读取1个字符 */
        GETCH(tty->read_q,c);

        /* [2] 若字符为回车且为设备设置了回车转换行标志,则转换字符,
         * 若设置了忽略回车字符则将其忽略,继续读取下一个字符;
         * 
         * 若字符为换行且设备设置了换行转回车标志则转换字符;
         * 若为设备设置了将字符转换为小写字符标志,则进行转换。*/
        if (c==13) /* 回车键 */
            if (I_CRNL(tty))
                c=10;
            else if (I_NOCR(tty))
                continue;
            else ;
        else if (c==10 && I_NLCR(tty))
            c=13;
        if (I_UCLC(tty))
            c=tolower(c);

        /* 若为字符设备开启了规范模式标志 */
        if (L_CANON(tty)) {
            /* 若当前字符为删除行的字符,则作删除当前行的处理 */
            if (c==KILL_CHAR(tty)) {
                /* deal with killing the input line */
                /* 若读取到删除当前行字符后,则删除当前行
                 * 字符直到遇到上一行回车或文件结束符。若
                 * 为设备设置了回显标志,则将删除字符写入设
                 * 备写的队列中(控制字符共2字节,需两个删除
                 * 字符), 删除辅助队列中1字符时,减少写队列
                 * 数据头索引。*/
                while(!(EMPTY(tty->secondary) ||
                        (c=LAST(tty->secondary))==10 ||
                        c==EOF_CHAR(tty))) {
                    if (L_ECHO(tty)) {
                        if (c<32)
                            PUTCH(127,tty->write_q);
                        PUTCH(127,tty->write_q);
                        tty->write(tty);
                    }
                    DEC(tty->secondary.head);
                }
                /* 当前行删除后,从读队列中读取下一个字符 */
                continue;
            }

            /* 若当前字符为删除字符, */
            if (c==ERASE_CHAR(tty)) {
                /* 若辅助队列为空或者辅助队列尾数据为换行字
                 * 符或为文件结束字符则继续读取下一个字符处理 */
                if (EMPTY(tty->secondary) ||
                   (c=LAST(tty->secondary))==10 ||
                   c==EOF_CHAR(tty))
                        continue;
                /* 若辅助队列非空数据尾字符非换行符或结束标志则向设备的
                 * 写队列中写入删除字符,对于控制字符则需写入两个删除字符 */
                if (L_ECHO(tty)) {
                    if (c<32)
                        PUTCH(127,tty->write_q);
                    PUTCH(127,tty->write_q);
                    tty->write(tty);
                }
                /* 删除辅助队列中数据头字符后减少辅助队列数据头索引,
                 * 并继续从设备读队列中读取下一字符以处理。*/
                DEC(tty->secondary.head);
                continue;
            }

            /* 若读取字符为停止控制字符则置位tty停止输出
             * 标志并继续处理读队列中的下1字符 */
            if (c==STOP_CHAR(tty)) {
                tty->stopped=1;
                continue;
            }
            /* 若读取字符为开始控制字符则复位tty停止输出
             * 标志并继续处理读队列中的下1字符 */
            if (c==START_CHAR(tty)) {
                tty->stopped=0;
                continue;
            }
        }

        /* 若没有为当前设备设置规范标志,则直接将读队列中的字符存储到到辅助队列中。*/
         
        /* 若为设备置位了ISIG标志,若收到INTR,QUIT,SUSP,DSUSP控制字符时,
         * 则向进程输出相应信号。若收到键盘中断控制符(^C)则向当前进程所
         * 在进程组中的所有进程发送中断信号。若收到退出符(^\)则向当前进
         * 程所在组的所有进程发送退出信号。做完这些处理后继续处理下1字符。*/
        if (L_ISIG(tty)) {
            if (c==INTR_CHAR(tty)) {
                tty_intr(tty,INTMASK);
                continue;
            }
            if (c==QUIT_CHAR(tty)) {
                tty_intr(tty,QUITMASK);
                continue;
            }
        }

        /* 若当前字符为换行符或文件结束符,
         * 表示已处理完一行字符,则将辅助队
         * 列成员data增1,以表示往辅助队列中
         * 又转换了一行输入。*/
        if (c==10 || c==EOF_CHAR(tty))
            tty->secondary.data++;

        /* 若为当前设备设置了回显标志,则将读入字符写回设备显示。
         * 若当前字符为换行符,则往设备回写换行符和回车符;若是控
         * 制字符,则将其写回设备显示(若为设备设置了回显控制符标
         * 志,则将控制字符转换为诸如^H形式显示)。*/
        if (L_ECHO(tty)) {
            if (c==10) {
                PUTCH(10,tty->write_q);
                PUTCH(13,tty->write_q);
            } else if (c<32) {
                if (L_ECHOCTL(tty)) {
                    PUTCH('^',tty->write_q);
                    PUTCH(c+64,tty->write_q);
                }
            } else
                PUTCH(c,tty->write_q);
            tty->write(tty);
        }
        PUTCH(c,tty->secondary);
    }
    /* 当字符设备读队列中的字符处理完毕后,即可唤醒等在辅助队列上的进程 */
    wake_up(&tty->secondary.proc_list);
}

/* tty_read,
 * 从当前进程的字符设备的辅助队列中读取nr字节数据到buf中,
 * channel = 0, 控制台终端,
 * channel = 1, 串口1终端,
 * channel = 2, 串口2终端。
 *
 * 系统调用读文件(read) --> 内核(sys_read) -->
 * 区分读字符设备(rw_char) --> 区分字符设备类型
 * (rw_ttyx,rw_tty) --> 从字符设备辅助队列中读取字符(tty_read)。
 *
 * 字符设备数据到辅助队列:当字符设备接收到字符时以中
 * 断方式通知CPU读取该字符到字符设备对应的读队列中,
 * 然后通过中断C处理函数将该字符读(转换)到辅助队列中。*/
int tty_read(unsigned channel, char * buf, int nr)
{
    struct tty_struct * tty;
    char c, * b=buf;
    int minimum,time,flag=0;
    long oldalarm;

    /* 根据channel获取 管理字符设备的结构体 */
    if (channel>2 || nr<0) return -1;
    tty = &tty_table[channel];

    /* 备份当前进程的定时值;
     * 获取为字符设备设置的超时值(0则未设置)和达到该超
     * 时值应读取的字符数,若当前进程没有设置超时值或者
     * 读取字符设备的超时值小于进程原设置的超时值,则用
     * 读取字符的超时值覆盖进程原超时值。待任务调度函数
     * 执行时(如当前进程进入睡眠后)会检查当前进程是否超
     * 时,若超时则会给任务置超时信号。*/
    oldalarm = current->alarm;
    time = 10L*tty->termios.c_cc[VTIME];
    minimum = tty->termios.c_cc[VMIN];
    if (time && !minimum) {
        minimum=1;
        if (flag=(!oldalarm || time+jiffies<oldalarm))
            current->alarm = time+jiffies;
    }
    if (minimum>nr)
        minimum=nr;

    /* 从字符设备辅助队列中读取nr个字符到buf中。
     *
     * 首先检查读取字符是否超时,若超时或进程有其他
     * 信号要处理则停止读取;若辅助队列不满足读取条
     * 件则尝试睡眠等待辅助队列满足被读条件;待辅助
     * 队列满足阅读条件时再检查读取字符是否超时,满
     * 足条件后从辅助队列中读取字符到buf中,直到遇到
     * 如文件结束符等结束标志。*/
    while (nr>0) {
        /* 若当前进程超时值为字符读取超时值(flag),检查
         * 本进程是否有超时信号需处理,若是则清除进程超
         * 时信号后退出循环。*/
        if (flag && (current->signal & ALRMMASK)) {
            current->signal &= ~ALRMMASK;
            break;
        }
        /* 若当前进程还有其他信号需处理则停止字符的继续读取 */
        if (current->signal)
            break;

        /* 若字符设备辅助队列空或者在为字符设备开启规范标志
         * 前提下(以行为单位进行读取),当辅助队列中的字符数少
         * 于1行且辅助队列剩余空间大于20则进入睡眠(在队列为空
         * 时sleep_if_empty才会真正地进入睡眠,这种情况只有等超
         * 时退出读取咯)。待真正进入睡眠时,待被其它进程唤醒后
         * 若未超时将继续读取。*/
        if (EMPTY(tty->secondary) || (L_CANON(tty) &&
        !tty->secondary.data && LEFT(tty->secondary)>20)) {
            sleep_if_empty(&tty->secondary);
            continue;
        }

        /* 从辅助队列中读取字符序列依次存到buf中,
         * 在字符设备开启规范标志时,若读到文件结束
         * 符,换行符时则结束读操作;或在读满nr字符或
         * 读完辅助队列内容方结束。*/
        do {
            GETCH(tty->secondary,c);
            if (c==EOF_CHAR(tty) || c==10)
                tty->secondary.data--;
            if (c==EOF_CHAR(tty) && L_CANON(tty))
                return (b-buf);
            else {
                put_fs_byte(c,b++);
                if (!--nr)
                    break;
            }
        } while (nr>0 && !EMPTY(tty->secondary));

        /* 检查并更新当前进程的超时值。
         * 
         * 若字符设备设置了超时时间且没有开启规范模式,
         * 若当前进程无超时值或其超时值比字符设备所设
         * 置的超时值要大时,则更新当前进程的超时值为
         * 字符设备所设置的超时值,否则恢复进程原本超时值。*/
        if (time && !L_CANON(tty))
            if (flag=(!oldalarm || time+jiffies<oldalarm))
                current->alarm = time+jiffies;
            else
                current->alarm = oldalarm;

        /* 在一次读取循环结束后,
         * 在规范模式下,只要读到字符便结束本次读取;
         * 在非规范模式下,当读取到超时所对应字符数时才停止本次读取。*/
        if (L_CANON(tty)) {
            if (b-buf)
                break;
        } else if (b-buf >= minimum)
            break;
    }
    
    /* 恢复进程的超时值,若读取超时且没有读取到任何字
     * 符则返回相应错误码,否则返回读取成功的字符数。*/
    current->alarm = oldalarm;
    if (current->signal && !(b-buf))
        return -EINTR;
    return (b-buf);
}

/* tty_write,
 * 
 *
 * 系统调用写文件(write)
 * --> 内核(sys_write)
 * --> 区分写字符设备(rw_char)
 * --> 区分字符设备类型(rw_ttyx,rw_tty)
 * --> 往字符设备写队列中写字符(tty_write)
 * --> 开启字符设备发送中断(rs_write)
 * <--> 字符设备发送中断处理函数发送字符(rs_interrupt,write_char)
 * --> 关字符设备发送中断。*/
int tty_write(unsigned channel, char * buf, int nr)
{
    static cr_flag=0;
    struct tty_struct * tty;
    char c, *b=buf;

    if (channel>2 || nr<0) return -1;
    tty = channel + tty_table;

    /* 向字符设备写队列中写入buf内存段的nr字节数据,直到
     * nr字节内容被完全写入或者当前进程收到其他处理信号。*/
    while (nr>0) {
        /* 若字符设备写队列为空则睡眠等待写队列非空 */
        sleep_if_full(&tty->write_q);
        if (current->signal)
            break;

        /* 当写队列非满时,将buf中的nr字符往写队列中。
         * 若设置了输出处理标志则根据相应输出处理标志
         * 对字符做转换后再写入。*/
        while (nr>0 && !FULL(tty->write_q)) {
            c=get_fs_byte(b);
            if (O_POST(tty)) {
                if (c=='\r' && O_CRNL(tty))
                    c='\n';
                else if (c=='\n' && O_NLRET(tty))
                    c='\r';
                if (c=='\n' && !cr_flag && O_NLCR(tty)) {
                    cr_flag = 1;
                    PUTCH(13,tty->write_q);
                    continue;
                }
                if (O_LCUC(tty))
                    c=toupper(c);
            }
            b++; nr--;
            cr_flag = 0;
            PUTCH(c,tty->write_q);
        }
        /* 当完成一次循环写入队列后,调用写队列后的回调函数;
         * 若还未写完nr字符但写队列已满时则调用任务调度函数
         * 切换任务,待切换到本任务时继续发送,在任务切换函数
         * 中有可能会为当前进程置某种信号(signal)。*/
        tty->write(tty);
        if (nr>0)
            schedule();
    }
    
    /* 返回写入字节数 */
    return (b-buf);
}

/*
 * Jeh, sometimes I really like the 386.
 * This routine is called from an interrupt,
 * and there should be absolutely no problem
 * with sleeping even in an interrupt (I hope).
 * Of course, if somebody proves me wrong, I'll
 * hate intel for all time :-). We'll have to
 * be careful and see to reinstating the interrupt
 * chips before calling this, though.
 *
 * I don't think we sleep here under normal circumstances
 * anyway, which is good, as the task sleeping might be
 * totally innocent.
 */
/* do_tty_interrupt,
 * 字符设备读中断C处理函数,
 * 将所读字符存储到字符设备的辅助队列中。
 * 
 * 由字符设备接收中断处理函数read_char调用。*/
void do_tty_interrupt(int tty)
{
    copy_to_cooked(tty_table+tty);
}

void chr_dev_init(void)
{
}

/* 另外,学习字符设备管理程序后可粗略理解计算机(键盘)数据输入流向。
 *   |------------|
 *   |  terminal  |
 *   |------------|
 *   |UART|network|
 *   |------------|
 *         ^
 *         |写往串口/网口控制器发送给其他设备
 *         v
 *     |------|
 *     |  CPU | to echo |----------|   |-------|
 *     |======| ------> |video card|-->|monitor|
 *     |QUEUES|         |----------|   |-------|
 *     |======|
 *        ^
 *        |I/O指令和中断机制
 *        V
 * |--------------------------|
 * |keyboard && its controller|
 * |--------------------------| */
