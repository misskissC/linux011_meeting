/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 *
 * NOTE! Don't touch this without checking that nothing in rs_io.s or
 * con_io.s breaks. Some constants are hardwired into the system (mainly
 * offsets into 'tty_queue'
 */
/* tty.h 为tty_io.c定义了一些数据结构体,还定义了一些宏常量。
 *
 * 注,在没有了解/修改 rs_io.s 或 con_io.s 之前,不要修改此文件。一些宏常量
 * 是直接写在代码中的(如 tty_queue 的初始化)。*/

#ifndef _TTY_H
#define _TTY_H

#include <termios.h>

#define TTY_BUF_SIZE 1024
/* struct tty_queue,
 * 缓冲字符设备数据(接收,发送)的队列结构体类型 */
struct tty_queue {
    unsigned long data; /* 存串口控制器的起始端口地址;计数队列所含行数 */
    unsigned long head; /* buf头部数据的索引 */
    unsigned long tail; /* buf尾部数据的索引 */
    struct task_struct * proc_list; /* 用于同步其他进程对本队列的访问 */
    char buf[TTY_BUF_SIZE]; /* 用于缓存数据 */
};

#define INC(a) ((a) = ((a)+1) & (TTY_BUF_SIZE-1))
#define DEC(a) ((a) = ((a)-1) & (TTY_BUF_SIZE-1))
#define EMPTY(a) ((a).head == (a).tail)
#define LEFT(a) (((a).tail-(a).head-1)&(TTY_BUF_SIZE-1))
#define LAST(a) ((a).buf[(TTY_BUF_SIZE-1)&((a).head-1)])
#define FULL(a) (!LEFT(a))
#define CHARS(a) (((a).head-(a).tail)&(TTY_BUF_SIZE-1))
/* GETCH(queue, c),
 * 从队列queue的buf中读取数据尾部数据保存到c中,
 * 并将数据尾部数据索引增1. */
#define GETCH(queue,c) \
(void)({c=(queue).buf[(queue).tail];INC((queue).tail);})

/* PUTCH(c,queue),
 * 从将字符c写入queue的buf中并将指向数据头部数据索引增1. */
#define PUTCH(c,queue) \
(void)({(queue).buf[(queue).head]=(c);INC((queue).head);})

#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE])
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])
#define SUSPEND_CHAR(tty) ((tty)->termios.c_cc[VSUSP])

/* struct tty_struct,
 * 管理字符设备读写的结构体类型。*/
struct tty_struct {
    struct termios termios;
    int pgrp;    /* 字符设备进程组id */
    int stopped; /* 停止标志 */

    /* 写队列函数指针 */
    void (*write)(struct tty_struct * tty);

    /* 读队列;写队列;辅助队列(存放读队列规范字符序列) */
    struct tty_queue read_q;
    struct tty_queue write_q;
    struct tty_queue secondary;
};

extern struct tty_struct tty_table[];

/*  intr=^C     quit=^|     erase=del   kill=^U
    eof=^D      vtime=\0    vmin=\1     sxtc=\0
    start=^Q    stop=^S     susp=^Z     eol=\0
    reprint=^R  discard=^U  werase=^W   lnext=^V
    eol2=\0
*/
/* 控制字符序列定义 */
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"

void rs_init(void);
void con_init(void);
void tty_init(void);

int tty_read(unsigned c, char * buf, int n);
int tty_write(unsigned c, char * buf, int n);

void rs_write(struct tty_struct * tty);
void con_write(struct tty_struct * tty);

void copy_to_cooked(struct tty_struct * tty);

#endif
