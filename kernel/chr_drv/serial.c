/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * serial.c
 *
 * This module implements the rs232 io functions
 * void rs_write(struct tty_struct * queue);
 * void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>

#define WAKEUP_CHARS (TTY_BUF_SIZE/4)

/* 以typedef void (fun)(void)函数类型
 * 声明以下符号,其定义在rs_io.s中。*/
extern void rs1_interrupt(void);
extern void rs2_interrupt(void);

/* init,
 * 初始化串口通信模式,如串口
 * 终端设备以中断方式和CPU通信,数据速率=2400bps。*/
static void init(int port)
{
/* PC/AT分配给UART2和UART1的端口地址空间分别为
 * [0x2f0, 0x2ff]和[0x3f8, 0x3ff]。实际芯片使用
 * 端口低3位用于寄存器寻址,UART2只使用了[0x2f8, 0x2fe],
 * UART1只使用了[0x3f8, 0x3fe]。*/
 
    /* 写3FBH(2FBH) 0x80,
     * 写线路控制寄存器, 确定异步通信的数据格式,
     * DLAB=1, 无奇偶, 停止位为1位, 数据5位。
     *
     * DLAB=1, 写3F8H(2F8H)/3F9H(2F9H) 0x30/0x00,
     * 写波特率因子LSB/MSB,
     * MSB=0x00, LSB=0x30 --> 数据速率=2400bps。
     * (波特率因子: 接收/发送一个bit所需时钟数,
     * 由此顺便计算下此时的时钟频率=2400bps * 48 = 115200Hz)
     *
     * 写3FBH(2FBH) - 写线路控制寄存器 0x03,
     * DLAB = 0, 无奇偶, 数据位为8位。
     *
     * 写3FCH(2FCH) - 写MODEM控制寄存器 0x0b, (RS-232)
     * bit[3]=1, UART为中断I/O方式, 
     * bit[1]=1, 数据终端就绪, DTR输出有效,
     * bit[0]=1, 请求发送, RTS输出有效。
     *
     * DLAB=0,写3F9H(2F9H) 0x0d,
     * 写中断允许寄存器,
     * bit[3]=1, 允许MODEM状态变化中断, 
     * bit[2]=1, 允许接收有错或间断条件中断,
     * bit[1]=0, 禁止发送器保持寄存器空中断,
     * bit[0]=1, 允许接收器数据就绪中断,
     * 当UART为中断I/O方式, 满足以上某中断条件时, 
     * 芯片的INTRPT(中断请求)端输出高电平向8259A(IRQ4/IRQ3中断发生),
     * 并在中断标识寄存器中设置相应标识位标识当前中断。
     *
     * DLAB=0,
     * 读3F8H(2F8H), 读接收数据寄存器, 
     * 将接收数据寄存器的内容读出以恢复接收数据寄存器无数据状态？*/
    outb_p(0x80,port+3); /* set DLAB of line control reg */
    outb_p(0x30,port);   /* LS of divisor (48 -> 2400 bps */
    outb_p(0x00,port+1); /* MS of divisor */
    outb_p(0x03,port+3); /* reset DLAB */
    outb_p(0x0b,port+4); /* set DTR,RTS, OUT_2 */
    outb_p(0x0d,port+1); /* enable all intrs but writes */
    (void)inb(port); /* read data port to reset things (?) */
}
 
/* rs_init,
 * UART初始化,
 * 在IDT中设置串口中断处理入口程序,
 * 设置PIC使能串口中断,并设置串口通信相关参数。*/
void rs_init(void)
{
    /* 在IDT[24h..23h]中设置串口2和串口1
     * 的中断处理入口程序。*/
    set_intr_gate(0x24,rs1_interrupt);
    set_intr_gate(0x23,rs2_interrupt);

    /* 编程设置UART,建立异步串行通信模式。
     * 以中断方式和CPU通信,数据传输速率为2400bps。*/
    init(tty_table[1].read_q.data); /* 0x3f8 */
    init(tty_table[2].read_q.data); /* 0x2f8 */

    /* 设置PIC允许IRQ3和IRQ4即串口2和串口1中断 */
    outb(inb_p(0x21)&0xE7,0x21);
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 * void _rs_write(struct tty_struct * tty);
 */
/* rs_write,
 * 设置tty对应的UART允许其发送保持寄存器空闲时的中断。
 * 
 * 该函数在tty_write往写队列tty->write_queue写一些数
 * 据后被调用,当UART发送保持寄存器空闲时就会向PIC输出
 * 中断从而让CPU执行串口中断处理入口程序rs*_interrupt,
 * 待tty->write_queue队列中无数据时将再禁止UART发送保
 * 持寄存器空闲时中断。*/
void rs_write(struct tty_struct * tty)
{
    cli();
    /* 当串口写队列不为空时,
     * 通过0x3f9(0x2f9)写UART发送保持寄存器
     * bit[1]以允许为发送保持寄存器空时中断。*/
    if (!EMPTY(tty->write_q))
        outb(inb_p(tty->write_q.data+1)|0x02,tty->write_q.data+1);
    sti();
}
