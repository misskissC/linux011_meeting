/*
 *  linux/kernel/chr_drv/tty_ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <termios.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

/* 波特率因子数组。
 * 波特率=1.8432MHz / (16 * 波特率因子)。
 * 如 波特率因子48对应的波特率=1843200 / (16 * 48) = 2400. */
static unsigned short quotient[] = {
    0, 2304, 1536, 1047, 857,
    768, 576, 384, 192, 96,
    64, 48, 24, 12, 6, 3
};

/* change_speed,
 * 根据tty中的控制模式成员,修改tty对应字符设备传输波特率。*/
static void change_speed(struct tty_struct * tty)
{
    unsigned short port,quot;

    /* 判断tty对应字符设备是否为串口终端,
     * 串口终端读队列data成员保存了串口端
     * 口起始地址,控制终端读队列data成员用
     * 于记录读队列数据的行数,初始值为0。*/
    if (!(port = tty->read_q.data))
        return;

    /* 根据tty控制模式成员获取波特率因子索引,
     * 从而从quotient数组中得到波特率因子。*/
    quot = quotient[tty->termios.c_cflag & CBAUD];

    /* 将波特率因子写入串口控制芯片中(见serial.c/init()) */
    cli();
    outb_p(0x80,port+3);        /* set DLAB */
    outb_p(quot & 0xff,port);   /* LS of divisor */
    outb_p(quot >> 8,port+1);   /* MS of divisor */
    outb(0x03,port+3);          /* reset DLAB */
    sti();
}

/* flush,
 * 清空queue所指字符设备队列,即将队列尾索引赋给队列头索引。*/
static void flush(struct tty_queue * queue)
{
    cli();
    queue->head = queue->tail;
    sti();
}

static void wait_until_sent(struct tty_struct * tty)
{
    /* do nothing - not implemented */
}

static void send_break(struct tty_struct * tty)
{
    /* do nothing - not implemented */
}

/* get_termios,
 * 获取tty字符设备的模式信息存于termios所指内存段中。*/
static int get_termios(struct tty_struct * tty, struct termios * termios)
{
    int i;

    /* 保证termios所指内存段组否 */
    verify_area(termios, sizeof (*termios));

    /* 将ttytermios成员数据拷贝到termios所指内存中 */
    for (i=0 ; i< (sizeof (*termios)) ; i++)
        put_fs_byte( ((char *)&tty->termios)[i] , i+(char *)termios );
    return 0;
}

/* set_termios,
 * 将termios所指内存段中的模式信息设置到tty的模式成员中,
 * 并随之更新字符设备波特率(因为模式信息的控制成员也被设置了)。*/
static int set_termios(struct tty_struct * tty, struct termios * termios)
{
    int i;

    for (i=0 ; i< (sizeof (*termios)) ; i++)
        ((char *)&tty->termios)[i]=get_fs_byte(i+(char *)termios);
    change_speed(tty);
    return 0;
}

/* get_termio,
 * 获取tty所对应字符设备的模式信息于termio所指内存中。*/
static int get_termio(struct tty_struct * tty, struct termio * termio)
{
    int i;
    struct termio tmp_termio;

    /* 保证termio所指内存段组否 */
    verify_area(termio, sizeof (*termio));

    /* struct termio中数据成员的数据类型是
     * struct termios中数据成员数据类型的一半,
     * 先通过各成员的一一赋值以截断各数据成员。*/
    tmp_termio.c_iflag = tty->termios.c_iflag;
    tmp_termio.c_oflag = tty->termios.c_oflag;
    tmp_termio.c_cflag = tty->termios.c_cflag;
    tmp_termio.c_lflag = tty->termios.c_lflag;
    tmp_termio.c_line = tty->termios.c_line;
    for(i=0 ; i < NCC ; i++)
        tmp_termio.c_cc[i] = tty->termios.c_cc[i];

    /* 然后将正确的tmp_termio拷贝到termio所指内存中 */
    for (i=0 ; i< (sizeof (*termio)) ; i++)
        put_fs_byte( ((char *)&tmp_termio)[i] , i+(char *)termio );
    return 0;
}

/*
 * This only works as the 386 is low-byt-first
 */
/* set_termio,
 * 将termio所指内存段中的模式信息设置到tty所指字符设备的模式成员中。
 * 该函数只能在低字节在低地址的386的计算机上正确运行。*/
static int set_termio(struct tty_struct * tty, struct termio * termio)
{
    int i;
    struct termio tmp_termio;

    /* struct termio中数据成员的数据类型是
     * struct termios中数据成员数据类型的一半,
     * 各成员需一对一赋值,不能以内存段的方式进行拷贝。*/
    for (i=0 ; i< (sizeof (*termio)) ; i++)
        ((char *)&tmp_termio)[i]=get_fs_byte(i+(char *)termio);
    *(unsigned short *)&tty->termios.c_iflag = tmp_termio.c_iflag;
    *(unsigned short *)&tty->termios.c_oflag = tmp_termio.c_oflag;
    *(unsigned short *)&tty->termios.c_cflag = tmp_termio.c_cflag;
    *(unsigned short *)&tty->termios.c_lflag = tmp_termio.c_lflag;
    tty->termios.c_line = tmp_termio.c_line;
    for(i=0 ; i < NCC ; i++)
        tty->termios.c_cc[i] = tmp_termio.c_cc[i];

    /* 控制模式成员被改变,所以需重新设置波特率 */
    change_speed(tty);
    return 0;
}

/* tty_ioctl,
 * 根据设置命令cmd和参数arg完成字符设备dev输入输出设置。
 * 
 * 其中的一些功能在linux0.11中还未实现。*/
int tty_ioctl(int dev, int cmd, int arg)
{
    struct tty_struct * tty;

    /* 主设备5为控制终端字符设备,
     * 其进程tty字段为tty次设备号,-1表示无控制终端;
     * 串口主设备号为4,其次设备号就在dev中;0,控制终端;
     * 1,串口1终端;2,串口2终端。根据次设备号索引到管理
     * 该设备数据通信的结构体tty_table[dev]。*/
    if (MAJOR(dev) == 5) {
        dev=current->tty;
        if (dev<0)
            panic("tty_ioctl: dev<0");
    } else
        dev=MINOR(dev);
    tty = dev + tty_table;
    
    switch (cmd) {
        case TCGETS:
            /* 获取字符设备的模式信息于arg参数所指内存段中 */
            return get_termios(tty,(struct termios *) arg);
        /* case TCSETSF-TCSETS: */
        case TCSETSF: /* 情况字符设备读队列后再设置字符设备模式标志 */
            flush(&tty->read_q); /* fallthrough */
        case TCSETSW:
            wait_until_sent(tty); /* fallthrough */
        /* 将arg所指内存段的模式信息设置到字符设备模式成员中 */
        case TCSETS:
            return set_termios(tty,(struct termios *) arg);
        case TCGETA:
            /* 获取字符设备模式信息于arg所指内存段中(struct termio) */
            return get_termio(tty,(struct termio *) arg);
        case TCSETAF: /* 清空字符设备读队列后再设置字符设备标志信息 */
            flush(&tty->read_q); /* fallthrough */
        case TCSETAW:
            wait_until_sent(tty); /* fallthrough */
        case TCSETA: /* 设置字符设备模式信息(struct termio) */
            return set_termio(tty,(struct termio *) arg);
        case TCSBRK:
            if (!arg) {
                wait_until_sent(tty);
                send_break(tty);
            }
            return 0;
        case TCXONC:
            return -EINVAL; /* not implemented */
        case TCFLSH: /* 刷新字符设备指定队列 */
            if (arg==0)
                flush(&tty->read_q);
            else if (arg==1)
                flush(&tty->write_q);
            else if (arg==2) {
                flush(&tty->read_q);
                flush(&tty->write_q);
            } else
                return -EINVAL;
            return 0;
        case TIOCEXCL:
            return -EINVAL; /* not implemented */
        case TIOCNXCL:
            return -EINVAL; /* not implemented */
        case TIOCSCTTY:
            return -EINVAL; /* set controlling term NI */
        case TIOCGPGRP:
            verify_area((void *) arg,4);
            put_fs_long(tty->pgrp,(unsigned long *) arg);
            return 0;
        case TIOCSPGRP:
            tty->pgrp=get_fs_long((unsigned long *) arg);
            return 0;
        case TIOCOUTQ: /* 获取字符设备所在进程组组号 */
            verify_area((void *) arg,4);
            put_fs_long(CHARS(tty->write_q),(unsigned long *) arg);
            return 0;
        case TIOCINQ: /* 获取辅助队列还未被处理的字符数 */
            verify_area((void *) arg,4);
            put_fs_long(CHARS(tty->secondary),
                (unsigned long *) arg);
            return 0;
        case TIOCSTI:
            return -EINVAL; /* not implemented */
        case TIOCGWINSZ:
            return -EINVAL; /* not implemented */
        case TIOCSWINSZ:
            return -EINVAL; /* not implemented */
        case TIOCMGET:
            return -EINVAL; /* not implemented */
        case TIOCMBIS:
            return -EINVAL; /* not implemented */
        case TIOCMBIC:
            return -EINVAL; /* not implemented */
        case TIOCMSET:
            return -EINVAL; /* not implemented */
        case TIOCGSOFTCAR:
            return -EINVAL; /* not implemented */
        case TIOCSSOFTCAR:
            return -EINVAL; /* not implemented */
        default:
            return -EINVAL;
    }
}
