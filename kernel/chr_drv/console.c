/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * console.c
 *
 * This module implements the console io functions
 * 'void con_init(void)'
 * 'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 */
/* 本文件实现了控制台I/O操作函数
 * 'void con_init(void)'
 * 'void con_write(struct tty_queue * queue)'
 * 希望这是一个相当完整的VT102版实现。*/

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 */
/* 注,在放置数据到显卡I/O时会禁止CPU处理中断,这些代码也会在键盘中断中运行。
 * 由于使用陷阱门,所以其实在键盘中断中没有使能CPU处理中断。希望这种重复禁止
 * 的方式可以正常运行。*/
 
/*
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 */

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>

/*
 * These are set up by the setup-routine at boot-time:
 */
/* 以下这些宏读取在setup.s中通过BIOS获取并存储的跟显示相关的信息 */
#define ORIG_X          (*(unsigned char *)0x90000) /* 光标x方向位置 */
#define ORIG_Y          (*(unsigned char *)0x90001) /* 光标y方向位置 */
#define ORIG_VIDEO_PAGE (*(unsigned short *)0x90004) /* 当前显示页 */
#define ORIG_VIDEO_MODE ((*(unsigned short *)0x90006) & 0xff) /* 显示模式 */
#define ORIG_VIDEO_COLS (((*(unsigned short *)0x90006) & 0xff00) >> 8) /* 窗口宽度 */
#define ORIG_VIDEO_LINES    (25)
#define ORIG_VIDEO_EGA_AX   (*(unsigned short *)0x90008) /**/
#define ORIG_VIDEO_EGA_BX   (*(unsigned short *)0x9000a) /* EGA显存大小 */
#define ORIG_VIDEO_EGA_CX   (*(unsigned short *)0x9000c) /* 属性等设置 */

#define VIDEO_TYPE_MDA  0x10 /* Monochrome Text Display */
#define VIDEO_TYPE_CGA  0x11 /* CGA Display */
#define VIDEO_TYPE_EGAM 0x20 /* EGA/VGA in Monochrome Mode */
#define VIDEO_TYPE_EGAC 0x21 /* EGA/VGA in Color Mode */

#define NPAR 16

extern void keyboard_interrupt(void);

static unsigned char  video_type;        /* Type of display being used */
static unsigned long  video_num_columns; /* Number of text columns */
static unsigned long  video_size_row;    /* Bytes per row */
static unsigned long  video_num_lines;   /* Number of test lines */
static unsigned char  video_page;        /* Initial video page */
static unsigned long  video_mem_start;   /* Start of video RAM */
static unsigned long  video_mem_end;     /* End of video RAM (sort of) */
static unsigned short video_port_reg;    /* Video register select port */
static unsigned short video_port_val;    /* Video register value port */
static unsigned short video_erase_char;  /* Char+Attrib to erase with */

/* 跟显存地址对应的屏幕位置 */
static unsigned long origin;  /* 屏幕内容左上角对应的显存地址 */
static unsigned long scr_end; /* 屏幕内容末端内容对应的显存地址 */
static unsigned long pos;     /* 屏幕坐标(x,y)对应的显存地址 */
static unsigned long x,y;     /* 屏幕坐标 */
static unsigned long top,bottom; /* 屏幕内容的顶端和底部坐标 */
static unsigned long state=0; /* 解析终端写队列中数据的状态/步骤 */
static unsigned long npar,par[NPAR];
static unsigned long ques=0;
static unsigned char attr=0x07;

static void sysbeep(void);

/*
 * this is what the terminal answers to a ESC-Z or csi0c
 * query (= vt100 response).
 */
#define RESPONSE "\033[?1;2c"

/* NOTE! gotoxy thinks x==video_num_columns is ok */
/* gotoxy,
 * 更新光标在屏幕上的位置(x,y),并计算该位置所对应的显存地址。*/
static inline void gotoxy(unsigned int new_x,unsigned int new_y)
{
    if (new_x > video_num_columns || new_y >= video_num_lines)
        return;
    /* x,y用于记录光标在屏幕上的坐标 */
    x=new_x;
    y=new_y;
    /* 计算屏幕坐标(x,y)对应的显存地址(1列用2字节显存表示) */
    pos=origin + y*video_size_row + (x<<1);
}

/* set_origin,
 * 设置终端屏幕起始显存地址,以将整屏对应的内容显示在终端上。*/
static inline void set_origin(void)
{
    cli();
    /* 选择显示控制数据寄存器r12,
     * 写入终端将要显示内容所在显存中的偏移地址的高字节。*/
    outb_p(12, video_port_reg);
    outb_p(0xff&((origin-video_mem_start)>>9), video_port_val);

    /* 选择显示控制数据寄存器r3,
     * 写入终端将要显示内容所在显存中的偏移地址的低字节(1列2字节)。*/
    outb_p(13, video_port_reg);
    outb_p(0xff&((origin-video_mem_start)>>1), video_port_val);
    sti();
}

/* scrup,
 * 将终端窗口向上移动一行。
 * 
 * 终端内容向下继续显示一行,
 * 若终端屏幕已满,则向上移动一行,
 * 终端屏幕下方出现的新行用空格填充。*/
static void scrup(void)
{
    /* EGA显卡支持终端区域和整屏窗口移动, */
    if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
    {
        /* 若终端已全屏显示,*/
        if (!top && bottom == video_num_lines) {
            /* 则将终端内容左上角,坐标位置,终端内容末端
             * 的显存地址更新到下一行 */
            origin += video_size_row;
            pos += video_size_row;
            scr_end += video_size_row;

            /* 若控制终端内容末端显存地址已超过显存末尾地址,
             * 则将终端除第1行以外的内容重新写入显存起始
             * 地址处(新行用空格填充),并更新终端内容对应的显存内存段。*/
            if (scr_end > video_mem_end) {
                __asm__("cld\n\t"
                    "rep\n\t"
                    "movsl\n\t"
                    "movl _video_num_columns,%1\n\t"
                    "rep\n\t"
                    "stosw"
                    ::"a" (video_erase_char),
                    "c" ((video_num_lines-1)*video_num_columns>>1),
                    "D" (video_mem_start),
                    "S" (origin)
                    :"cx","di","si");
                scr_end -= origin-video_mem_start;
                pos -= origin-video_mem_start;
                origin = video_mem_start;
            /* 若控制终端内容末端还未超出显存末端则用空格填充新行*/
            } else {
                __asm__("cld\n\t"
                    "rep\n\t"
                    "stosw"
                    ::"a" (video_erase_char),
                    "c" (video_num_columns),
                    "D" (scr_end-video_size_row)
                    :"cx","di");
            }
            /* 将屏幕窗口内容对应的显存段写往显示控制
             * 器中,以将指定显存段内容显示在控制终端上 */
            set_origin();
            
        /* 若EGA显卡下,终端内容未满屏,不用整屏移动,
         * 此时将开始于top+1到bottom区域中的内容向
         * 上移动一行,用空格填充新出现的行。*/
        } else {
            __asm__("cld\n\t"
                "rep\n\t"
                "movsl\n\t"
                "movl _video_num_columns,%%ecx\n\t"
                "rep\n\t"
                "stosw"
                ::"a" (video_erase_char),
                "c" ((bottom-top-1)*video_num_columns>>1),
                "D" (origin+video_size_row*top),
                "S" (origin+video_size_row*(top+1))
                :"cx","di","si");
        }
    }
    /* 非EGA显卡, 诸如MDA显卡控制器只支持整屏滚动,
     * 但其会自动调整超出显存范围的情况。*/
    else    /* Not EGA/VGA */
    {
        __asm__("cld\n\t"
            "rep\n\t"
            "movsl\n\t"
            "movl _video_num_columns,%%ecx\n\t"
            "rep\n\t"
            "stosw"
            ::"a" (video_erase_char),
            "c" ((bottom-top-1)*video_num_columns>>1),
            "D" (origin+video_size_row*top),
            "S" (origin+video_size_row*(top+1))
            :"cx","di","si");
    }
}

/* scrdown,
 * 将终端窗口向下移动一行。
 * 
 * 终端内容向上继续显示一行,
 * 若终端屏幕已满,则向下移动一行,
 * 终端屏幕上方出现的新行用空格填充。*/
static void scrdown(void)
{
    if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
    {
        __asm__("std\n\t"
            "rep\n\t"
            "movsl\n\t"
            "addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
            "movl _video_num_columns,%%ecx\n\t"
            "rep\n\t"
            "stosw"
            ::"a" (video_erase_char),
            "c" ((bottom-top-1)*video_num_columns>>1),
            "D" (origin+video_size_row*bottom-4),
            "S" (origin+video_size_row*(bottom-1)-4)
            :"ax","cx","di","si");
    }
    else    /* Not EGA/VGA */
    {
        __asm__("std\n\t"
            "rep\n\t"
            "movsl\n\t"
            "addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
            "movl _video_num_columns,%%ecx\n\t"
            "rep\n\t"
            "stosw"
            ::"a" (video_erase_char),
            "c" ((bottom-top-1)*video_num_columns>>1),
            "D" (origin+video_size_row*bottom-4),
            "S" (origin+video_size_row*(bottom-1)-4)
            :"ax","cx","di","si");
    }
}

/* lf,
 * 针对输出的换行符进行换行。*/
static void lf(void)
{
    /* 若还未到屏幕底部则
     * 更新y方向坐标和对应的显存地址 */
    if (y+1<bottom) {
        y++;
        pos += video_size_row;
        return;
    }
    /* 将终端窗口向上移动一行 */
    scrup();
}

/* ri,
 * 保持列不变,将光标移动上一行。*/
static void ri(void)
{
    /* 若光标不在终端顶端则直接
     * 更新光标坐标和对应的显存地址*/
    if (y>top) {
        y--;
        pos -= video_size_row;
        return;
    }
    /* 若光标在终端内容顶端,
     * 则将终端窗口向下移动一行。*/
    scrdown();
}

/* cr,
 * 将光标置于行首。*/
static void cr(void)
{
    /* 显存地址减去 
     * 行首到x位置处的显存字节数 */
    pos -= x<<1;
    x=0;
}

/* del,
 * 更新删除1字符后光标的位置。*/
static void del(void)
{
    if (x) {
        pos -= 2;
        x--;
        *(unsigned short *)pos = video_erase_char;
    }
}

/* csi_J,
 * 以光标为基准,删除终端上的内容。
 *
 * 'ESC [par J',
 * par=0, 删除光标处到终端底端的内容;
 * par=1, 删除终端开始到光标处的内容;
 * par=2, 删除终端整屏。*/
static void csi_J(int par)
{
    long count __asm__("cx");
    long start __asm__("di");

    /* 根据par参数值,计算删除字符数及对应显存区域 */
    switch (par) {
        case 0: /* erase from cursor to end of display */
            count = (scr_end-pos)>>1;
            start = pos;
            break;
        case 1: /* erase from start to cursor */
            count = (pos-origin)>>1;
            start = origin;
            break;
        case 2: /* erase whole display */
            count = video_num_columns * video_num_lines;
            start = origin;
            break;
        default:
            return;
    }
    /* 用擦除字符video_erase_char填充终端上指定区域的字符 */
    __asm__("cld\n\t"
        "rep\n\t"
        "stosw\n\t"
        ::"c" (count),
        "D" (start),"a" (video_erase_char)
        :"cx","di");
}

/* csi_K,
 * 以光标位置为基准,删除光标所在行的内容。
 *
 * 'ESC [par K',
 * par=0,删除光标到行尾内容;
 * par=1,行首到光标段内容;
 * par=2,删除光标所在行。*/
static void csi_K(int par)
{
    long count __asm__("cx");
    long start __asm__("di");

    /* 根据par值计算删除的字符数及对应的显存区域 */
    switch (par) {
        case 0: /* erase from cursor to end of line */
            if (x>=video_num_columns)
                return;
            count = video_num_columns-x;
            start = pos;
            break;
        case 1:	/* erase from start of line to cursor */
            start = pos - (x<<1);
            count = (x<video_num_columns)?x:video_num_columns;
            break;
        case 2: /* erase whole line */
            start = pos - (x<<1);
            count = video_num_columns;
            break;
        default:
            return;
    }
    /* 用擦除字符video_erase_char填充终端上指定区域的字符 */
    __asm__("cld\n\t"
        "rep\n\t"
        "stosw\n\t"
        ::"c" (count),
        "D" (start),"a" (video_erase_char)
        :"cx","di");
}

/* csi_m,
 * 设置显示字符属性。
 *
 * 'ESC [par m',
 * par=0,默认属性(0x07);
 * par=1,加粗(0x0f);
 * par=4,加下划线;
 * par=7,反显(0x70);
 * par=27,正显(0x07)。*/
void csi_m(void)
{
    int i;

    for (i=0;i<=npar;i++)
        switch (par[i]) {
            case 0:attr=0x07;break;
            case 1:attr=0x0f;break;
            case 4:attr=0x0f;break;
            case 7:attr=0x70;break;
            case 27:attr=0x07;break;
        }
}

/* set_cursor,
 * 根据光标显存地址pos显示光标。*/
static inline void set_cursor(void)
{
    cli();
    /* 选择显示控制器数据寄存器r14写入鼠标在显存中偏移的高字节 */
    outb_p(14, video_port_reg);
    outb_p(0xff&((pos-video_mem_start)>>9), video_port_val);
    
    /* 选择显示控制器数据寄存器r14写入鼠标在显存中偏移的低字节*/
    outb_p(15, video_port_reg);
    outb_p(0xff&((pos-video_mem_start)>>1), video_port_val);
    sti();
}

/* respond,
 * 向主机响应终端的设备属性(主机通过'ESC Z'等控制序列请求)。*/
static void respond(struct tty_struct * tty)
{
    char * p = RESPONSE;

    cli();
    /* 将应答序列放入读队列中, */
    while (*p) {
        PUTCH(*p,tty->read_q);
        p++;
    }
    sti();
    /* 将含应答序列读队列中的内容转换到辅助队列中 */
    copy_to_cooked(tty);
}

/* insert_char,
 * 在光标位置插入擦除字符,将光标原后续字符皆后移。*/
static void insert_char(void)
{
    int i=x;
    unsigned short tmp, old = video_erase_char; /* 擦除字符 */
    unsigned short * p = (unsigned short *) pos; /* 光标对应显存地址 */

    /* 将擦除字符插入光标位置处 */
    while (i++<video_num_columns) {
        tmp=*p;
        *p=old;
        old=tmp;
        p++;
    }
}

/* insert_line,
 * 在光标位置处插入一行内容。*/
static void insert_line(void)
{
    int oldtop,oldbottom;

    oldtop=top;
    oldbottom=bottom;
    
    /* 从光标所在行让终端窗口向下移动一行 */
    top=y;
    bottom = video_num_lines;
    scrdown();

    /* 恢复光标位置 */
    top=oldtop;
    bottom=oldbottom;
}

/* delete_char,
 * 删除光标处字符,原光标后续字符向左移一个字符位置。*/
static void delete_char(void)
{
    int i;
    unsigned short * p = (unsigned short *) pos;

    if (x>=video_num_columns)
        return;
    /* 将光标之后字符依次左移,在行尾用擦除字符填充 */
    i = x;
    while (++i < video_num_columns) {
        *p = *(p+1);
        p++;
    }
    *p = video_erase_char;
}

/* delete_line,
 * 删除光标所在行。*/
static void delete_line(void)
{
    int oldtop,oldbottom;

    oldtop=top;
    oldbottom=bottom;

    /* 从光标所在行开始,将中断窗口上移一行 */
    top=y;
    bottom = video_num_lines;
    scrup();

    /* 恢复光标位置 */
    top=oldtop;
    bottom=oldbottom;
}

/* csi_at,
 * 在光标处插入nr个擦除字符。
 * 光标右边的字符往右移,超过终端右边界即列数的字符将消失。
 * 
 * 'ESC [nr @',nr为插入字符个数。*/
static void csi_at(unsigned int nr)
{
    if (nr > video_num_columns)
        nr = video_num_columns;
    else if (!nr)
        nr = 1;
    while (nr--)
        insert_char();
}

/* csi_L,
 * 在光标位置处插入nr行。
 *
 * 'ESC [nr L'。*/
static void csi_L(unsigned int nr)
{
    if (nr > video_num_lines)
        nr = video_num_lines;
    else if (!nr)
        nr = 1;
    while (nr--)
        insert_line();
}

/* csi_P,
 * 删除光标处的nr个字符。
 *
 * 'ESC [nr P'。*/
static void csi_P(unsigned int nr)
{
    if (nr > video_num_columns)
        nr = video_num_columns;
    else if (!nr)
        nr = 1;
    while (nr--)
        delete_char();
}

/* csi_M,
 * 删除光标处的nr行。
 *
 * 'ESC [ nr M'。*/
static void csi_M(unsigned int nr)
{
    if (nr > video_num_lines)
        nr = video_num_lines;
    else if (!nr)
        nr=1;
    while (nr--)
        delete_line();
}

/* 用于保存光标行和列号 */
static int saved_x=0;
static int saved_y=0;

/* save_cur,
 * 同步光标当前位置。*/
static void save_cur(void)
{
    saved_x=x;
    saved_y=y;
}

/* restore_cur,
 * 恢复所保存的光标位置。*/
static void restore_cur(void)
{
    gotoxy(saved_x, saved_y);
}

/* con_write,
 * 往控制终端写队列中写入数据后的回调函数。
 *
 * 解析终端写队列中的数据,若是控制字符,转义字符,
 * 控制序列则在终端实现这些字符对应的功能。*/
void con_write(struct tty_struct * tty)
{
    int nr;
    char c;

    /* 控制终端写队列中的字符数 */
    nr = CHARS(tty->write_q);

    while (nr--) {
        /* 从写队列中读取1个字符 */
        GETCH(tty->write_q,c);

        switch(state) {
            case 0:
                /* 在state=0阶段,若读取到普通字符
                 * 则直接显示在光标位置处,若已到行
                 * 尾则将字符写到下一行。显示字符后
                 * 更新光标和其对应的显存地址。*/
                if (c>31 && c<127) {
                    if (x>=video_num_columns) {
                        x -= video_num_columns;
                        pos -= video_size_row;
                    lf();
                    }
                    /* 将字符显示在经计算的位置 */
                    __asm__("movb _attr,%%ah\n\t"
                        "movw %%ax,%1\n\t"
                        ::"a" (c),"m" (*(short *)pos)
                        :"ax");
                    pos += 2;
                    x++;

                /* 为转义字符时,置state=1 */
                } else if (c==27)
                    state=1;
                /* 若为换行符,纵向制表符,换页符则将光标移到下一行 */
                else if (c==10 || c==11 || c==12)
                    lf();
                /* 若为回车符, 则将光标移到行首 */
                else if (c==13)
                    cr();
                /* 若是擦除字符,则擦除光标前1字符 */
                else if (c==ERASE_CHAR(tty))
                    del();
                /* 若是退格字符,则左移光标1字符位置 */
                else if (c==8) {
                    if (x) {
                        x--;
                        pos -= 2;
                    }
                /* 若字符为水平制表符则将光标移到最近列数为8倍数的列上 */
                } else if (c==9) {
                    c=8-(x&7);
                    x += c;
                    pos += c<<1;
                    if (x>video_num_columns) {
                        x -= video_num_columns;
                        pos -= video_size_row;
                        lf();
                    }
                    c=9;
                /* 若为响铃字符则调用蜂鸣函数蜂鸣一下 */
                } else if (c==7)
                    sysbeep();
                break;
            /* 若在state=0阶段解析到转义字符序列,*/
            case 1:
                state=0; /* 恢复state 0状态 */
                /* 若继state=0解析到ESC后又解析到'['则置state=2 */
                if (c=='[')
                    state=2;
                else if (c=='E') /* ESC 'E' */
                    gotoxy(0,y+1);
                else if (c=='M') /* ESC 'M'*/
                    ri();
                else if (c=='D') /* ESC 'D'*/
                    lf();
                else if (c=='Z') /* ESC 'Z'*/
                    respond(tty);
                else if (x=='7') /* ESC '7'*/
                    save_cur();
                else if (x=='8') /* ESC '8'*/
                    restore_cur();
                break;
            /* ESC [ */
            case 2:
                /* 初始化par数组供case 3使用 */
                for(npar=0;npar<NPAR;npar++)
                    par[npar]=0;
                npar=0;
                state=3; /* 置3表明下一个字符为 'ESC ['序列中的字符*/
                if (ques=(c=='?')) /* 是否有'?'*/
                    break;
            
            /* 在case 3中解析转义字符序列 */
            case 3:
                /* 解析到';'则增加par索引并退出 */
                if (c==';' && npar<NPAR-1) {
                    npar++;
                    break;
                /* 将数字字符转换为数字存储在par数组中 */
                } else if (c>='0' && c<='9') {
                    par[npar]=10*par[npar]+c-'0';
                    break;
                /* 字符不为';'或数字字符则置state=4 */
                } else state=4;

            /* 解析转义字符序列的最后一个字符,该字符表示具体命令 */
            case 4:
                state=0; /* 复位state=0 */
                switch(c) {
                    /* ESC [ par 'G'或'`',光标水平移动 */
                    case 'G': case '`':
                        if (par[0]) par[0]--;
                        gotoxy(par[0],y);
                        break;
                    /* ESC [ par 'A',光标上移 */
                    case 'A':
                        if (!par[0]) par[0]++;
                        gotoxy(x,y-par[0]);
                        break;
                    /* ESC [ par 'B' 或 'e', 光标下移 */
                    case 'B': case 'e':
                        if (!par[0]) par[0]++;
                        gotoxy(x,y+par[0]);
                        break;
                    /* ESC [ par 'C' 或 'a', 光标右移 */
                    case 'C': case 'a':
                        if (!par[0]) par[0]++;
                        gotoxy(x+par[0],y);
                        break;
                    /* ESC [ par 'D', 光标左移 */
                    case 'D':
                        if (!par[0]) par[0]++;
                        gotoxy(x-par[0],y);
                        break;
                    /* ESC [ par 'E', 光标下移并回行首 */
                    case 'E':
                        if (!par[0]) par[0]++;
                        gotoxy(0,y+par[0]);
                        break;
                    /* ESC [ par 'F', 光标上移并回行首 */
                    case 'F':
                        if (!par[0]) par[0]++;
                        gotoxy(0,y-par[0]);
                        break;
                    /* ESC [ par 'd', 当前列设置行位置 */
                    case 'd':
                        if (par[0]) par[0]--;
                        gotoxy(x,par[0]);
                        break;
                    /* ESC [ par 'H' 或 'f', 光标定位 */
                    case 'H': case 'f':
                        if (par[0]) par[0]--;
                        if (par[1]) par[1]--;
                        gotoxy(par[1],par[0]);
                        break;
                    /* ESC [ par 'j', 删除操作 */
                    case 'J':
                        csi_J(par[0]);
                        break;
                    /* ESC [ par 'K', 行内删除 */
                    case 'K':
                        csi_K(par[0]);
                        break;
                    /* ESC [ par 'L', 插入行 */
                    case 'L':
                        csi_L(par[0]);
                        break;
                    /* ESC [ par 'M', 删除行 */
                    case 'M':
                        csi_M(par[0]);
                        break;
                    /* ESC [ par 'P', 删除字符 */
                    case 'P':
                        csi_P(par[0]);
                        break;
                    /* ESC [ par '@', 插入字符 */
                    case '@':
                        csi_at(par[0]);
                        break;
                    /* ESC [ par 'm', 设置显示字符属性 */
                    case 'm':
                        csi_m();
                        break;
                    /* ESC [ par 'r', 设置滚屏上下界 */
                    case 'r':
                        if (par[0]) par[0]--;
                        if (!par[1]) par[1] = video_num_lines;
                        if (par[0] < par[1] &&
                        par[1] <= video_num_lines) {
                            top=par[0];
                            bottom=par[1];
                        }
                        break;
                    /* ESC [ par 's', 保存光标位置 */
                    case 's':
                        save_cur();
                        break;
                    /* ESC [ par 'u', 用保存的光标位置设置光标当前位置 */
                    case 'u':
                        restore_cur();
                        break;
                }
        }
    }
    /* 解析并完成终端写队列中数据对应操作后,重新显示光标 */
    set_cursor();
}

/*
 *  void con_init(void);
 *
 * This routine initalizes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequece.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 */
/* con_init,
 * 初始化控制台终端。
 *
 * 初始化画面显示信息,光标位置,键盘中断,使能键盘等。*/
void con_init(void)
{
    register unsigned char a;
    char *display_desc = "????";
    char *display_ptr;
    
    /* 获取在setup.s通过BIOS所获取到的显卡
     * 所支持的显示参数并保存在全局变量中。*/
    video_num_columns = ORIG_VIDEO_COLS;
    video_size_row = video_num_columns * 2;
    video_num_lines = ORIG_VIDEO_LINES;
    video_page = ORIG_VIDEO_PAGE;
    video_erase_char = 0x0720;

    /* 判断所设置显卡的显示模式,并根据显示模式作相应设置 */

    /* 单色模式 */
    if (ORIG_VIDEO_MODE == 7) /* Is this a monochrome display? */
    {
        video_mem_start = 0xb0000;
        video_port_reg = 0x3b4;
        video_port_val = 0x3b5;
        if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
        {
            video_type = VIDEO_TYPE_EGAM;
            video_mem_end = 0xb8000;
            display_desc = "EGAm";
        }
        else
        {
            video_type = VIDEO_TYPE_MDA;
            video_mem_end = 0xb2000;
            display_desc = "*MDA";
        }
    }
    else /* 彩色模式 */
    {
        video_mem_start = 0xb8000;
        video_port_reg  = 0x3d4;
        video_port_val  = 0x3d5;
        if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
        {
            video_type    = VIDEO_TYPE_EGAC;
            video_mem_end = 0xbc000;
            display_desc  = "EGAc";
        }
        else
        {
            video_type = VIDEO_TYPE_CGA;
            video_mem_end = 0xba000;
            display_desc = "*CGA";
        }
    }

    /* Let the user known what kind of display driver we are using */
    /* 将当前显示模式显示在右上角 */
    display_ptr = ((char *)video_mem_start) + video_size_row - 8;
    while (*display_desc)
    {
        *display_ptr++ = *display_desc++;
        display_ptr++;
    }

    /* Initialize the variables used for scrolling (mostly EGA/VGA) */
    /* 根据显存地址计算屏幕坐标大小 */
    origin  = video_mem_start;
    scr_end = video_mem_start + video_num_lines * video_size_row;
    top     = 0;
    bottom  = video_num_lines;

    /* 更新记录光标坐标的变量,计算光标对应的显存地址 */
    gotoxy(ORIG_X,ORIG_Y);

    /* 在IDT[21h]中设置键盘中断处理入口函数,
     * 设置PIC允许IRQ1即键盘中断。*/
    set_trap_gate(0x21,&keyboard_interrupt);
    outb_p(inb_p(0x21)&0xfd,0x21);

    /* 通过端口地址61h(8255A)激活键盘 */
    a=inb_p(0x61);
    outb_p(a|0x80,0x61);
    outb(a,0x61);
}
/* from bsd-net-2: */

/* sysbeepstop,
 * 停止蜂鸣声。*/
void sysbeepstop(void)
{
    /* disable counter 2 */
    outb(inb_p(0x61)&0xFC, 0x61);
}

int beepcount = 0;

/* sysbeep,
 * 使能蜂鸣功能。
 * 
 * 8255A PB bit[1]=1时开启扬声器,
 * PB bit[0]=1时开启8253定时器2,
 * 通过61h设置PB bit[1..0]=(11)2时,
 * 在8253定时器2开通时,扬声器以8253
 * 定时器2的频率蜂鸣。*/
static void sysbeep(void)
{
    /* enable counter 2 */
    outb_p(inb_p(0x61)|3, 0x61);
    /* set command for counter 2, 2 byte write */
    outb_p(0xB6, 0x43);
    /* send 0x637 for 750 HZ */
    outb_p(0x37, 0x42);
    outb(0x06, 0x42);
    /* 1/8 second */
    beepcount = HZ/8;
}
