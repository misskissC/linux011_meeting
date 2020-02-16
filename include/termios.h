#ifndef _TERMIOS_H
#define _TERMIOS_H

#define TTY_BUF_SIZE 1024

/* 0x54 is just a magic number to make these relatively uniqe ('T') */
/* struct termios 结构体模式信息操作标志 */
#define TCGETS  0x5401 /* 获取字符设备的模式标志 */
#define TCSETS  0x5402 /* 设置字符设备的模式标志 */
#define TCSETSW 0x5403
#define TCSETSF 0x5404 /* 情况字符设备读队列后再设置字符设备模式标志 */
#define TCGETA  0x5405 /* 获取字符设备模式标志信息(struct termio) */
#define TCSETA  0x5406 /* 设置字符设备模式标志信息((struct termio))*/
#define TCSETAW 0x5407
#define TCSETAF 0x5408 /* 清空字符设备读队列后再设置字符设备标志信息 */
#define TCSBRK  0x5409
#define TCXONC  0x540A
#define TCFLSH  0x540B /* 刷新字符设备对垒 */
#define TIOCEXCL    0x540C
#define TIOCNXCL    0x540D
#define TIOCSCTTY   0x540E
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410
#define TIOCOUTQ    0x5411 /* 获取字符设备所在进程组组号 */
#define TIOCSTI     0x5412
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TIOCMGET    0x5415
#define TIOCMBIS    0x5416
#define TIOCMBIC    0x5417
#define TIOCMSET    0x5418
#define TIOCGSOFTCAR    0x5419
#define TIOCSSOFTCAR    0x541A
#define TIOCINQ 0x541B /* 获取辅助队列还未被读取的字符数 */

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define NCC 8
struct termio {
    unsigned short c_iflag;  /* input mode flags */
    unsigned short c_oflag;  /* output mode flags */
    unsigned short c_cflag;  /* control mode flags */
    unsigned short c_lflag;  /* local mode flags */
    unsigned char c_line;    /* line discipline */
    unsigned char c_cc[NCC]; /* control characters */
};

#define NCCS 17
struct termios {
    unsigned long c_iflag;    /* input mode flags */
    unsigned long c_oflag;    /* output mode flags */
    unsigned long c_cflag;    /* control mode flags */
    unsigned long c_lflag;    /* local mode flags */
    unsigned char c_line;     /* line discipline,线路速率 */
    unsigned char c_cc[NCCS]; /* control characters */
};

/* c_cc characters */
/* struct termios结构体类型中控制字符数组c_cc种控制字符的索引 */
#define VINTR 0  /* c_cc[VINTR] =^C, \003,中断 */
#define VQUIT 1  /* c_cc[VQUIT] =^\, \034,退出 */
#define VERASE 2 /* c_cc[VERASE]=^H, \177,删除字符 */
#define VKILL 3  /* c_cc[VKILL] =^U, \025,删除行 */
#define VEOF 4   /* c_cc[VEOF]  =^D, \004,文件结束字符 */
#define VTIME 5  /* c_cc[VTIME] =,      1/10秒定时值 */
#define VMIN 6   /* c_cc[VMIN]  =,      应读取最少字符个数 */
#define VSWTC 7  /* c_cc[VSWTC] =\0，     交换字符 */
#define VSTART 8 /* c_cc[VSTART]=^Q, \021,开始字符 */
#define VSTOP 9  /* c_cc[VSTOP] =^S, \023,停止字符 */
#define VSUSP 10 /* c_cc[VSUSP] =^Z, \032,挂起字符 */
#define VEOL 11  /* c_cc[VSUSP] =\0, \032,行结束字符 */
#define VREPRINT 12 /* c_cc[VREPRINT]=^R, \022,重显字符 */
#define VDISCARD 13 /* c_cc[VDISCARD]=^0, \017,丢弃字符 */
#define VWERASE 14  /* c_cc[VWERASE] =^W, \027,单词擦除字符 */
#define VLNEXT 15   /* c_cc[VLNEXT]  =^V, \026,下一行字符 */
#define VEOL2 16    /* c_cc[VEOL2]   =\0,     ,行结束符2 */

/* c_iflag bits */
/* struct termios结构体类型中输入模式标志位 */
#define IGNBRK 0000001 /* 输入时忽略BREAK标志 */
#define BRKINT 0000002 /* 输入BREAK时产生SIGINT信号 */
#define IGNPAR 0000004 /* 忽略校验出错字符标志 */
#define PARMRK 0000010 /* 标记奇偶校验错标志 */
#define INPCK  0000020 /* 允许输入奇偶校验标志 */
#define ISTRIP 0000040 /* 屏蔽字符第8位标志 */
#define INLCR  0000100 /* 将输入的换行符转回车键标志 */
#define IGNCR  0000200 /* 忽略输入回车键标志 */
#define ICRNL  0000400 /* 将输入的回车键转换为换行符标志 */
#define IUCLC  0001000 /* 将输入转换为小写字符标志 */
#define IXON   0002000 /* 允许开始/停止输出控制标志 */
#define IXANY  0004000 /* 允许任何字符重启输出标志 */
#define IXOFF  0010000 /* 允许开始/停止输入控制标志 */
#define IMAXBEL 0020000 /* 输入队列满时响铃标志 */

/* c_oflag bits */
/* struct termios 结构体类型中输出模式标志位 */
#define OPOST   0000001 /* 输出处理置位标志 */
#define OLCUC   0000002 /* 输出时将小写字符转为大写字符标志 */
#define ONLCR   0000004 /* 输出时将换行符转换为回车和换行符的标志 */
#define OCRNL   0000010 /* 输出时将回车转换为换行符标志 */
#define ONOCR   0000020 /* 行首不输出回车符标志 */
#define ONLRET  0000040 /* 遇到换行符时当回车符处理标志 */
#define OFILL   0000100 /* 延迟时使用填充字符而不使用时间延迟标志 */
#define OFDEL   0000200 /* 填充字符为ASCII DEL码,未设置时为ASCII NULL */
#define NLDLY   0000400 /* 选择换行延迟标志 */
#define   NL0   0000000 /* 换行延迟类型0标志 */
#define   NL1   0000400 /* 换行延迟类型1标志 */
#define CRDLY   0003000 /* 使用回车延迟标志 */
#define   CR0   0000000 /* 回车延迟类型0标志 */
#define   CR1   0001000 /* 回车延迟类型1标志 */
#define   CR2   0002000 /* 回车延迟类型2标志 */
#define   CR3   0003000 /* 回车延迟类型3标志 */
#define TABDLY  0014000 /* 使用TAB延迟标志 */
#define   TAB0  0000000 /* TAB延迟类型0标志 */
#define   TAB1  0004000 /* TAB延迟类型1标志 */
#define   TAB2  0010000 /* TAB延迟类型2标志 */
#define   TAB3  0014000 /* TAB延迟类型3标志 */
#define   XTABS 0014000 /* TAB转换为空格标志 */
#define BSDLY   0020000 /* 使用退格延迟标志 */
#define   BS0   0000000 /* 退格延迟类型0标志 */
#define   BS1   0020000 /* 退格延迟类型1标志 */
#define VTDLY   0040000 /* 使用纵向制表延迟标志 */
#define   VT0   0000000 /* 纵向制表延迟类型0标志 */
#define   VT1   0040000 /* 纵向制表延迟类型1标志 */
#define FFDLY   0040000 /* 使用换页延迟标志 */
#define   FF0   0000000 /* 换页延迟类型0标志 */
#define   FF1   0040000 /* 换页延迟类型1标志*/

/* c_cflag bit meaning */
/* struct termios 结构体类型中控制模式标志位 */
#define CBAUD   0000017 /* 传输速率屏蔽码 */
#define  B0     0000000 /* 挂断线路标志 */
#define  B50    0000001 /* 波特率50 */
#define  B75    0000002 /* 波特率75 */
#define  B110   0000003 /* 波特率110 */
#define  B134   0000004 /* 波特率134 */
#define  B150   0000005 /* 波特率150 */
#define  B200   0000006 /* 波特率200 */
#define  B300   0000007 /* 波特率300 */
#define  B600   0000010 /* 波特率600 */
#define  B1200  0000011 /* 波特率1200 */
#define  B1800  0000012 /* 波特率1800 */
#define  B2400  0000013 /* 波特率2400 */
#define  B4800  0000014 /* 波特率4800 */
#define  B9600  0000015 /* 波特率9600 */
#define  B19200 0000016 /* 波特率19200 */
#define  B38400 0000017 /* 波特率38400 */
#define EXTA    B19200  /* 扩展波特率A */
#define EXTB    B38400  /* 扩展波特率B */
#define CSIZE   0000060 /* 字符位宽屏蔽码 */
#define   CS5   0000000 /* 每字符5位 */
#define   CS6   0000020 /* 每字符6位 */
#define   CS7   0000040 /* 每字符7位 */
#define   CS8   0000060 /* 每字符8位 */
#define CSTOPB  0000100 /* 设置两个停止位 */
#define CREAD   0000200 /* 使能接收 */
#define CPARENB 0000400 /* 输出时产生奇偶位,输入时奇偶校验 */
#define CPARODD 0001000 /* 输入/输出校验为奇校验 */
#define HUPCL   0002000 /* 进程关闭后挂断标志 */
#define CLOCAL  0004000 /* 忽略modem控制线路 */
#define CIBAUD  03600000     /* input baud rate (not used) */
#define CRTSCTS 020000000000 /* flow control */

#define PARENB CPARENB
#define PARODD CPARODD

/* c_lflag bits */
/* struct termios结构体类型中本地模式标志位 */
#define ISIG    0000001 /* 当收到INTR,QUIT等字符时产生对应信号给进程 */
#define ICANON  0000002 /* 规范模式(熟模式)开启标志 */
#define XCASE   0000004 /* ICANON设置前提下,回显大写字符 */
#define ECHO    0000010 /* 回显标志 */
#define ECHOE   0000020 /* ICANON设置前提下,ERASE/WERASE擦除前1字符或单词 */
#define ECHOK   0000040 /* ICANON设置前提下,收到KILL字符时将删除当前行 */
#define ECHONL  0000100 /* ICANON设置前提下,显示换行字符 */
#define NOFLSH  0000200 /* 产生SIGINT和SIGQUIT信号时不刷新队列,产生SIGSUSP则刷新 */
#define TOSTOP  0000400 /* 发送SIGTTOU信号到准备写控制终端的后台进程的进程组 */
#define ECHOCTL 0001000 /* ECHO设置前提下,除TAB NL START STOP以外控制字符被回显成^X, X为控制字符+40h */
#define ECHOPRT 0002000 /* ICANON和IECHO设置前提下,擦除字符时回显 */
#define ECHOKE  0004000 /* ICANON设置前提下,KILL删除当前行将回显 */
#define FLUSHO  0010000 /* 刷新输出 */
#define PENDIN  0040000 /* 收到读字符时重现所有字符 */
#define IEXTEN  0100000 /* 开启实时定义的输出处理 */

/* modem lines */
#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG

/* tcflow() and TCXONC use these */
#define	TCOOFF		0
#define	TCOON		1
#define	TCIOFF		2
#define	TCION		3

/* tcflush() and TCFLSH use these */
#define	TCIFLUSH	0
#define	TCOFLUSH	1
#define	TCIOFLUSH	2

/* tcsetattr uses these */
#define TCSANOW		0
#define TCSADRAIN	1
#define TCSAFLUSH	2

typedef int speed_t;

extern speed_t cfgetispeed(struct termios *termios_p);
extern speed_t cfgetospeed(struct termios *termios_p);
extern int cfsetispeed(struct termios *termios_p, speed_t speed);
extern int cfsetospeed(struct termios *termios_p, speed_t speed);
extern int tcdrain(int fildes);
extern int tcflow(int fildes, int action);
extern int tcflush(int fildes, int queue_selector);
extern int tcgetattr(int fildes, struct termios *termios_p);
extern int tcsendbreak(int fildes, int duration);
extern int tcsetattr(int fildes, int optional_actions,
    struct termios *termios_p);

#endif
