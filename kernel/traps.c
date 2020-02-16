/*
 *  linux/kernel/traps.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */
/* traps.c 根据 asm.s 处理硬件陷阱和故障。目前主要用于调试,将来可扩展用
 * 来杀死不正常的进程(可能通过发送信号方式,若有必要也可直接杀死)。*/
#include <string.h>

#include <linux/head.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>

/* get_seg_byte(seg, addr),
 * 将seg段中addr处的1字节内容读出。
 * 
 * ({}), 包含在{}中的复合语句中的最后一个表达式
 * 将作为({})的最终值, 相当于get_seg_byte的返回值。
 *
 * 内联汇编的输入。
 * "0" (seg), 将seg赋给%0的约束即eax中;
 * "m" (*(addr)), 内存变量*(addr)。
 *
 * 内联汇编中的指令。
 * push %%fs, fs寄存器入栈;
 * mov %%ax, %%fs, fs=ax;
 * movb %%fs:%2, %%al, al = fs:(*(addr));
 * pop %%fs, 恢复fs寄存器的值。
 * 
 * 内联汇编的输出。
 * __res = eax。
 *
 * __res将作为该宏最终的值。*/
#define get_seg_byte(seg,addr) ({ \
register char __res; \
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
    :"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

/* get_seg_long(seg, addr),
 * 将seg段(选择符所指向段)中addr处4字节内容读出。
 *
 * ({}), 包含在{}中的复合语句中的最后一个表达式
 * 将作为({})的最终值, 相当于get_seg_long的返回值。
 *
 * 内联汇编中的输入。
 * "0" (seg), 将seg赋给%0中的约束即eax;
 * "m" (*(addr)), 内存变量*addr。
 *
 * 内联汇编中的指令。
 * push %%fs, fs寄存器入栈;
 * fs=ax;
 * mov fs:%2, eax, 将fs:*(addr)中的4字节内容赋给eax;
 * pop %%fs, 恢复fs寄存器内容。
 *
 * 内联汇编的输出。
 * 将eax的值赋值给__res变量,
 * __res变量本身也希望由某个空闲寄存器来表示。
 *
 * __res作为整个宏(表达式)的"返回值"。*/
#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
    :"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

/* 获取fs寄存器的值。
 * "=a" (__res), 将eax赋值给__res,
 * __res; 语句作为宏的最终值。*/
#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})

/* 声明kernel/exit.c中的do_exit函数 */
int do_exit(long code);

/* 以typedef void(funt)(void)函数类型声明page_exception。
 * page_exception的定义在哪里呢? */
void page_exception(void);

/* 以typedef void (funt)(void)函数类型声明divide_error,
 * 该函数定义在kernel/asm.s中。*/
void divide_error(void);

/* 除以下的page_fault外
 * (因为已经读过主存管理程序了, 了解以下页错误的处理吧),
 * 就不再此处一一跟踪每一个中断入口程序啦,
 * 若有需要时再去跟踪阅读。*/
void debug(void);
void nmi(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void double_fault(void);
void coprocessor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);
void coprocessor_error(void);
void reserved(void);
void parallel_interrupt(void);
void irq13(void);

/* die,
 * 打印一些提示信息, 然后退出程序。
 *
 * str - 提示信息,
 * esp_ptr - 中断发生时eip的入栈地址,
 * nr - 错误码。
 *
 * 此处与用户程序管理的直接耦合还是挺大,
 * 由于此程序的功能是打印一些提示信息后退出用户程序,
 * 所以涉及LDT表、任务、进程等相关部分, 可暂不细读。*/
static void die(char * str,long esp_ptr,long nr)
{
    long * esp = (long *) esp_ptr;
    int i;
    
    /* 输出提示信息, 错误号,
     * 输出中断处的cs, eip, eflag, ss, esp信息,
     * 输出fs,
     * ...
     * (printk定义在kernel/printk.c中, 暂不细读) */
    printk("%s: %04x\n\r",str,nr&0xffff);
    printk("EIP:\t%04x:%p\nEFLAGS:\t%p\nESP:\t%04x:%p\n",
            esp[1],esp[0],esp[2],esp[4],esp[3]);
    printk("fs: %04x\n",_fs());

    /* 打印当前用户程序地址空间信息,
     * 0x17为用户程序程序段描述符LDT[2]的选择符,
     * 之前在setup.s和head.s中涉及了GDT, LDT会在任务管理程序中涉及。*/
    printk("base: %p, limit: %p\n",get_base(current->ldt[1]),get_limit(0x17));
    /* 如果是用户程序,
     * 则打印用户程序中断发生处esp寄存器所指向内存中的16字节内容。
     * esp[4]为ss在用户程序程序中的值,
     * esp[3]为用户程序在中断发生时esp寄存器的值。*/
    if (esp[4] == 0x17) {
        printk("Stack: ");
        for (i=0;i<4;i++)
            printk("%p ",get_seg_long(0x17,i+(long *)esp[3]));
        printk("\n");
    }
    /* 获取当前任务TSS在GDT中的编号给变量i */
    str(i);
    /* 打印当前任务的进程号, TSS号,
     * 以及中断发生处的10字节内容,
     * esp[1]为用户程序中断发生时eip的值。*/
    printk("Pid: %d, process nr: %d\n\r",current->pid,0xffff & i);
    for(i=0;i<10;i++)
        printk("%02x ",0xff & get_seg_byte(esp[1],(i+(char *)esp[0])));
    printk("\n\r");

    /* 释放用户程序资源, 退出用户程序。*/
    do_exit(11);    /* play segment exception */
}

/* 以do开头的程序是各中断的C中断处理函数,
 * 这些函数在中断入口汇编程序中被调用,
 * 包含了真正处理中断的代码。*/

void do_double_fault(long esp, long error_code)
{
    die("double fault",esp,error_code);
}

void do_general_protection(long esp, long error_code)
{
    die("general protection",esp,error_code);
}

/* 除法溢出中断的C中断处理函数, 由asm.c/_divide_error调用。
 *
 * esp - 中断发生时eip的入栈地址,
 * error_code - 错误码, 此处为0。*/
void do_divide_error(long esp, long error_code)
{
    die("divide error",esp,error_code);
}

/* do_int3的参数很多, asm.s中记录了
 * 从中断发生到调用C中断处理函数过程中的栈内容,
 * 可根据该知识点理解这些参数。*/
void do_int3(long * esp, long error_code,
    long fs,long es,long ds,
    long ebp,long esi,long edi,
    long edx,long ecx,long ebx,long eax)
{
    int tr;

    /* 取当前任务的任务号与tr中 */
    __asm__("str %%ax":"=a" (tr):"0" (0));

    /* 打印当事程序设置断点中断发生时eax ebx ecx edx寄存器的值 */
    printk("eax\t\tebx\t\tecx\t\tedx\n\r%8x\t%8x\t%8x\t%8x\n\r",
        eax,ebx,ecx,edx);

    /* 打印当事程序设置断点中断发生时esi edi ebp esp寄存器的值 */
    printk("esi\t\tedi\t\tebp\t\tesp\n\r%8x\t%8x\t%8x\t%8x\n\r",
        esi,edi,ebp,(long) esp);
    
    /* 打印当事程序设置断点中断发生时ds es fs寄存器的值, 当事程序所在任务号 */
    printk("\n\rds\tes\tfs\ttr\n\r%4x\t%4x\t%4x\t%4x\n\r",
        ds,es,fs,tr);
    /* 打印当事程序设置断点中断发生时eip cs eflag寄存器的值 */
    printk("EIP: %8x   CS: %4x  EFLAGS: %8x\n\r",esp[0],esp[1],esp[2]);
}

void do_nmi(long esp, long error_code)
{
    die("nmi",esp,error_code);
}

void do_debug(long esp, long error_code)
{
    die("debug",esp,error_code);
}

void do_overflow(long esp, long error_code)
{
    die("overflow",esp,error_code);
}

void do_bounds(long esp, long error_code)
{
    die("bounds",esp,error_code);
}

void do_invalid_op(long esp, long error_code)
{
    die("invalid operand",esp,error_code);
}

void do_device_not_available(long esp, long error_code)
{
    die("device not available",esp,error_code);
}

void do_coprocessor_segment_overrun(long esp, long error_code)
{
    die("coprocessor segment overrun",esp,error_code);
}

void do_invalid_TSS(long esp,long error_code)
{
    die("invalid TSS",esp,error_code);
}

void do_segment_not_present(long esp,long error_code)
{
    die("segment not present",esp,error_code);
}

void do_stack_segment(long esp,long error_code)
{
    die("stack segment",esp,error_code);
}

void do_coprocessor_error(long esp, long error_code)
{
    if (last_task_used_math != current)
        return;
    die("coprocessor error",esp,error_code);
}

void do_reserved(long esp, long error_code)
{
    die("reserved (15,17-47) error",esp,error_code);
}

/* trap_init,
 * 设置IDT(见head.s)和8259A,初始化中断/异常机制。
 * 
 * 粗略理解CPU引用IDT描述符的过程。
 * 在head.s中有提到, IDT描述符描述了一段子程序的相关信息,
 * CPU引用IDT描述符是为了得到该段子程序的内存地址。
 *
 * 此文将触发CPU引用IDT描述符的方式总结为两种。
 * [1] CPU执行int指令。
 * [2] CPU在完成某条指令的执行后, 接收/检测到某中断或异常(后统称为中断)信号时。
 *
 * CPU执行int指令或检测到中断信号时具体引用哪一个IDT描述符呢。
 * [1] CPU执行"int n"指令时将其操作数n用作引用IDT描述符的索引, 即引用IDT[n]。
 * [2] IDT[0..16]是Intel为17个中断在硬件层面为CPU分配的IDT描述符,
 * 即当CPU检测到这17个中断中的一个中断发生时便自动引用其对应的IDT描述符。
 * IDT[0] ... 除法溢出
 * IDT[1] ... 调试(标志寄存器TF=1时)
 *        ...    
 * (参考《INTEL 80386 PROGRAMMER'S REFERENCE MANUAL》"9.8 Exception Conditions"章节)
 *
 * 其余IDT描述符可由编程指定给外设(I/O设备)中断。
 * 在setup.s中编程为8259A分配了IDT[20h..2fh], 键盘中断的IDT描述符为IDT[21h],
 * 键盘输入发生键盘中断发生时(键盘I/O)会向CPU发出中断信号,
 * 该中断信号包含引用IDT描述符的索引21h(俗称中断号/码), CPU从而引用IDT[21h]。
 *
 * CPU在跳转执行IDT描述符中的子程序前,
 * 会自动在ss:esp维护的栈内存中备份中断发生处的信息(中断现场保护),
 * 供中断处理程序返回(若有必要)以继续执行中断发生处的后续程序。
 *
 * 中断机制常应用于发生频率远低于CPU频率的I/O中或某些事件上,
 * 在某事件发生时以中断机制让CPU执行相应的处理程序就好,
 * 待处理程序执行完毕再返回到中断发生处。
 * 而不用CPU执行扫描低频率事件是否发生的程序。*/
void trap_init(void)
{
    int i;

    /* set_trap_gate 是include/asm/system.h中定义的宏,
     * divide_error 是kernel/asm.s中定义的一段子程序。
     * 在粗略了解IDT和中断机制后, 跟踪下IDT[0]和divide_error
     * 以了解下在IDT描述符中设置子程序的过程吧。
     * 这个过程大概是include/asm/system.h set_trap_get -> _set_gate 和 
     * divide_error -> kernel/asm.c _divide_error -> do_divide_error ->
     * die(粗略)。然后就返回到到这里了。*/
    set_trap_gate(0,&divide_error);
    /* 再粗略回味一下除法溢出中断过程。
     *
     * 当除法溢出中断发生时, CPU完成中断现场保护后引用IDT[0]。
     * IDT[0]位内容。
     * |31                         |15              |7        0
     * --------------------------------------------------------
     * |divide_error offset[31..16]| 1 | 00 | 01111 | 000 |0x0|
     * -------------------------------------------------------- 4
     * |            0x08           |divide_error offset[15..0]|
     * -------------------------------------------------------- 0
     * P=1, 不检查CPL是否小于等于DPL(非int或into指令触发CPU引用IDT[0]),
     * 检查0x08对应段描述符(P=1?DPL<=CPL? etc.)并将其加载到cs中,
     * 然后跳转执行0x08对应可执行段中的divide_error程序。*/

    set_trap_gate(1,&debug);
    set_trap_gate(2,&nmi);

/* 通过int n指令跳转执行IDT[n]中设置的处理程序时需满足
 * CPL <= IDT[n].DPL, CPL >= IDT[n].GDT.DPL。
 * 由于所有的中断处理函数都在内核可执行段中(DPL=0),
 * 所以在CPL > 0的程序中不能通过int指令跳转执行在set_trap_gate中
 * 设置的处理程序。
 * 
 * 在CPL=n的程序中, 
 * 可以通过int指令访问通过set_system_gate在IDT[n]中设置的程序
 * (通过set_system_gate设置的IDT.DPL=3),
 * 若IDT[n].GDT[n].DPL<=n, 则可跳转访问IDT中设置的处理程序。*/
    set_system_gate(3,&int3);   /* int3-5 can be called from all */
    set_system_gate(4,&overflow);
    set_system_gate(5,&bounds);
    set_trap_gate(6,&invalid_op);
    set_trap_gate(7,&device_not_available);
    set_trap_gate(8,&double_fault);
    set_trap_gate(9,&coprocessor_segment_overrun);
    set_trap_gate(10,&invalid_TSS);
    set_trap_gate(11,&segment_not_present);
    set_trap_gate(12,&stack_segment);
    set_trap_gate(13,&general_protection);
    set_trap_gate(14,&page_fault);
    set_trap_gate(15,&reserved);
    set_trap_gate(16,&coprocessor_error);

    /* 先用reserved再次初始化IDT[17..48],
     * 在head.s中, IDT表由 ignore_int子程序初始化。*/
    for (i=17;i<48;i++)
        set_trap_gate(i,&reserved);

    /* 设置协处理器中断描述符IDT[45],
     * 编程8259A开启协处理器中断。*/
    set_trap_gate(45,&irq13);
    /* out_p outb inb_p 定义在include/asm/io.h中 */
    outb_p(inb_p(0x21)&0xfb,0x21);
    outb(inb_p(0xA1)&0xdf,0xA1);
/* 0x21/0xA1是中断控制器8259A的端口地址
 * (见setup.s中的I/O端口地址空间)。
 * 在完成对中断控制器8259A-1/2的初始化后(见setup.s),
 * 8259A进入接收操作命令状态。
 *
 * 当端口地址最低位为0时,
 * 表示下发OCW1操作命令到8259A, 即操作中断屏蔽寄存器IMR(P68)。
 * 保持IMR其他位不变,
 * 设置8259A-1允许IRQ2中断, 即允许来自从片8259A-2的中断(P65),
 * 设置8259A-2允许IRQ13中断, 即协处理器中断
 * (P65中IRQ13标注的年代较早, 可能有误),
 * 根据8259A引脚图和中断号的分配, 协处理器中断码为45(2dh)。*/

    set_trap_gate(39,&parallel_interrupt);
}
/* 关于当时的链接器???
 * 在本文件中将divide_error声明为函数类型void divide_error(void)后,
 * 再引用divide_error时它就应该被解析成函数在OS代码段中的偏移地址。
 * 而在以上set_trap_gate和set_system_gate中,
 * 需在divide_error前加取地址符&, 
 * 又像是在将divide_error当成全局变量在对待。
 * 不过只是一个语法问题, 没有太大关系。
 * 2019.06.08 */