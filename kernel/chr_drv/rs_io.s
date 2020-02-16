/*
 *  linux/kernel/rs_io.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * rs_io.s
 *
 * This module implements the rs232 io interrupts.
 */
/* rs_io.s
 * 本程序实现了 rs232 I/O中断处理程序。*/

.text
.globl _rs1_interrupt,_rs2_interrupt

size = 1024 /* must be power of two !
               and must match the value
               in tty_io.c!!! */

/* these are the offsets into the read/write buffer structures */
/* 串口读写队列结构体中各成员偏移量 */
rs_addr = 0
head = 4
tail = 8
proc_list = 12
buf = 16

startup = 256 /* chars left in write queue when we restart it */

/*
 * These are the actual interrupt routines. They look where
 * the interrupt is coming from, and take appropriate action.
 */
.align 2
_rs1_interrupt:
    pushl $_table_list+8 /* table_list第3个元素的地址 */
    jmp rs_int
.align 2
_rs2_interrupt:
    pushl $_table_list+16 /* table_list第5个元素的地址 */
rs_int:
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    push %es
    push %ds    /* as this is an interrupt, we cannot */
    pushl $0x10 /* know that bs is ok. Load it */
    pop %ds     /* es=ds=10h, 加载内核数据段到数据段寄存器中 */
    pushl $0x10
    pop %es
    movl 24(%esp),%edx /* edx=table_list + 8 or 16 */
    movl (%edx),%edx   /* 取串口1或串口2读队列内存首地址 */
    movl rs_addr(%edx),%edx /* 取读队列data成员即串口1或2的起始端口(0x3f8, 0x2f8) */
    addl $2,%edx /* interrupt ident. reg  0x3fa(0x2fa) */
rep_int:
    xorl %eax,%eax
    inb %dx,%al  /* 读0x3fa(0x2fa)即读中断标识寄存器以判断串口中断类型 */
    testb $1,%al /* bit[1]=1则表示无中断 */
    jne end
    cmpb $6,%al /* 接收状态有错,this shouldn't happen, but ... */
    ja end
    movl 24(%esp),%ecx /* ecx=table_list + 8 or 16 */
    pushl %edx
    subl $2,%edx /* 传递给子程序的参数edx=0x3f8(0x2f8) */
    /* al=110,接收状态有错,奇偶错etc,读线路状态寄存器复位;
     * al=100,接收数据就绪,接收器数据有效,读接收数据寄存器复位;
     * al=010,发送保持寄存器空,发送器准备就绪,写入发送保持寄存器;
     * al=000,MODEM状态有变化,输入状态有变化,读MODEM状态寄存器;
     * 
     * 根据串口中断类型跳转执行jmp_table表中相应的中断中断处理函数。 */
    call jmp_table(,%eax,2) /* NOTE! not *4, bit0 is 0 already */
    popl %edx
    jmp rep_int /* 直到串口无中断方结束 */
end:    movb $0x20,%al
    outb %al,$0x20  /* EOI,向PIC发送结束中断命令 */
    pop %ds
    pop %es
    popl %eax
    popl %ebx
    popl %ecx
    popl %edx
    addl $4,%esp # jump over _table_list entry
    iret

/* 各串扣中断对应的中断处理程序表,他们分别是
 * MODEM寄存器有变化,发送保持寄存器空,接收数据,接收状态错误
 * 的中断处理程序。*/
jmp_table:
    .long modem_status,write_char,read_char,line_status

.align 2
modem_status: /* 读MODEM状态寄存器(0x3fe或0x2fe)以让MODEM寄存器复位 */
    addl $6,%edx /* clear intr by reading modem status reg */
    inb %dx,%al
    ret

.align 2
line_status: /* 读线路状态寄存器(0x3fd或0x2fd)以让MODEM寄存器复位 */
    addl $5,%edx /* clear intr by reading line status reg. */
    inb %dx,%al
    ret

/* 读串口所接收到的数据 */
.align 2
read_char:
    inb %dx,%al    /* 读0x3f8(0x2f8), 读接收数据寄存器 */
    movl %ecx,%edx /* edx=ecx=table_list+8 or +16 */
    subl $_table_list,%edx  /* edx=8 or 16 */
    shrl $3,%edx            /* edx=1 or 2 */
    
    /* ecx=table_list[8 or 16)]即&tty_table[1 or 2].read_q,
     * 获取串口读队列地址赋给ecx。*/
    movl (%ecx),%ecx

    /* movl (%ecx+head), ebx,
     * ebx=*( (long *)(&tty_table[1 or 2].read_q + 4) )即
     * tty_table[1 or 2].read_q.head,将串口读队列中head成员赋给ebx寄存器中。*/
    movl head(%ecx),%ebx

    /* 将从串口中所读数据写入*( (char *)(&tty_table[1 or 2].read_q + 16 + head) )
     * 即tty_table[1 or 2].read_q.buf[ebx]中,即将从串口所读数据写入串口读队列的buf成员中。*/
    movb %al,buf(%ecx,%ebx)

    incl %ebx /* tty_table[1 or 2].read_q.buf数据头索引增1 */
    andl $size-1,%ebx      /* ebx=ebx & size - 1,即以循环队列的方式使用队列中的buf */
    cmpl tail(%ecx),%ebx   /* 判断tty_table[1 or 2].read_q.tail是否等于ebx即buf中数据头索引 */
    je 1f /* 若数据头索引等于数据尾索引表示buf已满则向前跳转标号1处, */
    movl %ebx,head(%ecx)   /* 将数据头索引赋值给tty_table[1 or 2].read_q.head */
1: pushl %edx              /* table_list读队列下标(1 or 2)作为do_tty_interrupt函数的参数 */
    call _do_tty_interrupt /* kernel/chr_drv/tty_io.c */
    addl $4,%esp           /* 清参数edx的栈内存 */
    ret

.align 2
write_char:
    movl 4(%ecx),%ecx /* ecx=&tty_table[1 or 2].write_q */

    /* ebx=*( (long *)(&tty_table[1 or 2].write_q + 4) )即
     * ebx=串口写队列head成员 */
    movl head(%ecx),%ebx

    /* ebx=写队列中的数据个数 */
    subl tail(%ecx),%ebx
    andl $size-1,%ebx # nr chars in queue

    je write_buffer_empty /* 若写队列为空则跳转write_buffer_empty处 */
    cmpl $startup,%ebx    /* if (写队列数据元素 < 256) 则向前跳转标号1处 */
    ja 1f

    /* ebx=*( (long *)(&tty_table[1 or 2].write_q + 12) )即
     * 取串口写队列任务指针成员proc_list赋值给ebx。*/
    movl proc_list(%ecx),%ebx # wake up sleeping process

    /* 若任务指针值为空则向前跳转到标号1处 */
    testl %ebx,%ebx # is there any?
    je 1f

    /* movl $0, *proc_list 即proc_list所指内存段首4字节置为0,
     * 即将proc_list所指任务的state成员置位0-将所指任务置为就绪状态 */
    movl $0,(%ebx)

    /* 将写队列数据尾的数据写往al,然后将其 */
1:  movl tail(%ecx),%ebx
    movb buf(%ecx,%ebx),%al
    outb %al,%dx /* 写发送器保持寄存器0x3f8(0x2f8) */
    incl %ebx    /* 数据尾索引增1 */
    andl $size-1,%ebx /* 以循环队列的方式使用队列中的buf */
    movl %ebx,tail(%ecx) /* 将数据尾索引赋给串口写队列tail成员 */
    cmpl head(%ecx),%ebx /* 比较串口写队列中指向数据头和数据尾成员,若两者相等表示队列空 */
    je write_buffer_empty /* 队列空则跳转write_buffer_empty处 */
    ret /* 将串口写队列中数据传输一个到串口让其发送出去 */
.align 2
write_buffer_empty:
    /* 将串口写队列的proc_list成员赋值给ebx,
     * 检查ebx是否为空,为空则向前跳转标号1处,
     * 若不为空则赋值0给proc_list所指任务的state成员,
     * 即将该任务置位可运行状态,即唤醒该任务。*/
    movl proc_list(%ecx),%ebx # wake up sleeping process
    testl %ebx,%ebx # is there any?
    je 1f
    movl $0,(%ebx)
    
1:  incl %edx   /* edx=0x3f9(0x2f9) */
    inb %dx,%al /* 读中断允许标志寄存器 */
    jmp 1f
1: jmp 1f
1: andb $0xd,%al /* disable transmit interrupt */
    outb %al,%dx /* 禁止串口发送寄存器空中断,因为此时写队列中的数据已发送完毕 */
    ret
