/* move_to_user_mode,
 * 从CPU内核模式(最高特权级0)转移到CPU的用户模式(最低特权级3)。
 * 
 * pushl指令往栈中写入的内容依次为
 * |---------------|
 * |0x17           |
 * |---------------|
 * |current_esp    |
 * |---------------|
 * |eflag          |
 * |---------------|
 * |0x0f           |
 * |---------------|
 * |标号1处偏移地址|
 * |---------------|
 *
 * iret指令被执行时,栈中内容依次被弹出赋给以下寄存器
 * eip=标号1处偏移地址
 * cs=0xf
 * 标志寄存器eflag=eflag
 * esp=current_esp
 * ss=0x17
 *
 * cs=0x0f
 * |15              3| 2|  0|
 * |-----------------|--|---|
 * |      index      |TI|RPL|
 * |------------------------|
 *                  1| 1| 11|
 * 即为GDT[LDTR][1]选择符,LDTR在sched_init被装载为初始任务LDT的选择
 * 符,所以0x0f充当初始任务init_task LDT[1](代码段)选择符。
 *
 * 同理,0x17为初始任务init_task LDT[2](数据段选择符)。
 * 
 * iret指令被执行后, CPU跳转执行初始任务init_taskLDT[1]所描述内存段
 * 中eip偏移处指令即move_to_user_mode中标号1处指令。在标号1处及之后
 * 的指令完成将初始任务LDT[2]加载到各数据段寄存器后就完成了从内核程
 * 序转移到用户程序了。程序特权级0到3的转换,GDT描述程序和数据内存段
 * 到LDT描述程序和内存段的转移。*/
#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \
    "pushl $0x17\n\t" \
    "pushl %%eax\n\t" \
    "pushfl\n\t" \
    "pushl $0x0f\n\t" \
    "pushl $1f\n\t" \
    "iret\n" \
    "1:\tmovl $0x17,%%eax\n\t" \
    "movw %%ax,%%ds\n\t" \
    "movw %%ax,%%es\n\t" \
    "movw %%ax,%%fs\n\t" \
    "movw %%ax,%%gs" \
    :::"ax")

/* 允许中断; 禁止中断; 空指令。*/
#define sti() __asm__ ("sti"::)
#define cli() __asm__ ("cli"::)
#define nop() __asm__ ("nop"::)

/* 中断返回指令。*/
#define iret() __asm__ ("iret"::)

/* _set_gate(gate_addr,type,dpl,addr),
 * 设置IDT描述符。
 *
 * 参数。
 * gate_addr - IDT描述符首地址;
 * type - IDT描述符类型;
 * dpl -  IDT描述符特权级;
 * addr - IDT所描述处理程序在其可执行段的偏移地址。
 *
 * 粗略理解此处内联汇编含义。
 * [1] 占位符与输入。
 * %0 - "i" ((short) (0x8000+(dpl<<13)+(type<<8))),
 * "i"表示将引用立即数((short) (0x8000+(dpl<<13)+(type<<8)))
 * (立即数一般会被编译器存在某空闲通用寄存器中)。
 *
 * %1 - "o" (*((char *) (gate_addr))),
 * %2 - "o" (*(4+(char *) (gate_addr))),
 * "o"表示引用内存变量(char类型),
 * 且变量所在内存地址加一点正偏移所得到的内存地址也是有效可用的。
 * 在C语言中, 变量作为左值时表示往其所在内存赋值, 变量作为右值时表示取其值。
 *
 * "d" ((char *) (addr)),"a" (0x00080000),
 * "d"表示 edx = ((char *) (addr)), 保存处理函数在其可执行段中的偏移地址;
 * "a"表示 eax = (0x00080000)。
 * 
 * [2] 无输出部分。
 *
 * [3] 汇编语句。
 * 将含addr低16位的dx赋值给eax低16位ax;
 * 将IDT描述符第5和6字节内容((short) (0x8000+(dpl<<13)+(type<<8)))赋给edx低16位dx;
 * 将含段描述符选择符0x8和addr低16位的eax赋给%1(IDT描述符低4字节);
 * 将含addr高16位和type等的edx赋给%2(IDT描述符高4字节)。
 *
 * _set_gate执行后, gate_addr处IDT描述符的内容如下。
 * |31          |15                |7        0
 * -------------------------------------------
 * |addr[31..16]| 1 | dpl | 0 type | 000 |0x0|
 * -------------------------------------------4
 * |    0x08    |       addr[15..0]          |
 * -------------------------------------------0
 * P=1, IDT描述符有效。
 * segment descriptor selector = 0x8, 为操作系统代码可执行内存段的选择符。
 * dpl=0, IDT描述符特权级为最高特权级(系统级);
 * dpl=3, IDT描述符特权级为最低特权级(用户级)。
 * type=15时, IDT描述符为陷阱门描述符;
 * type=14时, IDT描述符为中断门描述符。*/
#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("movw %%dx,%%ax\n\t" \
    "movw %0,%%dx\n\t" \
    "movl %%eax,%1\n\t" \
    "movl %%edx,%2" \
    : \
    : "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
    "o" (*((char *) (gate_addr))), \
    "o" (*(4+(char *) (gate_addr))), \
    "d" ((char *) (addr)),"a" (0x00080000))
/* GCC内联输入输出中的约束含义可参考 
 * http://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html。
 * 不过此文至今也没有真正明白 "o" "m" "v"三个约束之间的区别。*/


/* 在IDT表偏移n处设置中断门描述符, 
 * 14: IDT描述符类型 = 中断门描述符;
 * 0: dpl = 系统级;
 * addr: 中断函数偏移地址。
 *
 * set_intr_gate得到的IDT描述符位格式。
 * |31          |15              |7       0|
 * ------------------------------------------
 * |addr[31..16]| 1 | 00 | 01110 | 000 |0x0|
 * ------------------------------------------4
 * |    0x08    |       addr[15..0]        |
 * -----------------------------------------0 */
#define set_intr_gate(n,addr) \
    _set_gate(&idt[n],14,0,addr)

/* set_trap_gate(n,addr),
 * 将IDT[n]设置为陷阱门描述符(位格式见head.s中的记录)。
 * 
 * 参数。
 * n - IDT描述符索引(中断码);
 * addr - IDT[n]中处理程序在其可执行段中的偏移地址。
 * 15 - IDT[n]中的TYPE字段, 表征IDT[n]为陷阱门描述符类型;
 * 0 - IDT[n]中的dpl, 表示IDT[n]的特权级为最高特权级(系统级)。
 *
 * set_trap_gate所设置IDT[n]的位格式。
 * |31          |15              |7       0|
 * -----------------------------------------
 * |addr[31..16]| 1 | 00 | 01111 | 000 |0x0|
 * -----------------------------------------4
 * |    0x08    |       addr[15..0]        |
 * -----------------------------------------0 */
#define set_trap_gate(n,addr) \
    _set_gate(&idt[n],15,0,addr)

/* 将IDT[n]设置为陷阱门描述符。
 *
 * 参数。
 * n - IDT描述符索引;
 * addr - IDT[n]中处理程序在其可执行段中的偏移地址。
 * 15 - IDT[n]的TYPE, 表示描述符类型为陷阱门描述符;
 * 3 - IDT[n]的dpl, 表示特权级为最低特权级(用户级)。
 *
 * set_system_gate所设置IDT[n]的位格式。
 * |31          |15              |7       0|
 * ------------------------------------------
 * |addr[31..16]| 1 | 11 | 01111 | 000 |0x0|
 * ------------------------------------------4
 * |    0x08    |       addr[15..0]        |
 * -----------------------------------------0 */
#define set_system_gate(n,addr) \
    _set_gate(&idt[n],15,3,addr)

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

/* _set_tssldt_desc(n,addr,type),
 * 设置系统段描述符。
 * 
 * 参数。
 * n: GDT段描述符首地址;
 * addr: 系统内存段首地址;
 * type: 系统内存段类型。
 *
 * 内联汇编。
 * "a" (addr), eax=addr;
 * %1 - "m"(*(n)),   内存变量(*(n));
 * %2 - "m"(*(n+2)), 内存变量(*(n+2));
 * %3 - "m"(*(n+4)), 内存变量(*(n+4));
 * %4 - "m"(*(n+5)), 内存变量(*(n+5));
 * %5 - "m"(*(n+6)), 内存变量(*(n+6));
 * %6 - "m"(*(n+7)), 内存变量(*(n+7).
 * "m"约束, 
 * 内存变量的地址$$$$$$$
 * 
 * 将$104(tss描述符或ldt表长度)2字节内容赋给%1(limit[15..0]),
 * 将ax赋给%2(addr[15..0]),
 * 将eax循环右移16位后将低8位赋给%3(addr[23..16]),
 * 将type赋给%4(P && DPL && TYPE), type自带的双引号跟前后双引号结合,
 * 将0x00赋给%5(G && X && AVL && limit[19..16]),
 * 将ah赋给%6(addr[31..24])
 *
 * GDT段描述符n的格式。
 *|31            |23        |15      |7             0
 *|--------------------------------------------------
 *| addr[31..24] | 00000000 |  type  | addr[23..16] |
 *|-------------------------------------------------| 4
 *|          addr[15..0]    |         104           |
 *--------------------------------------------------- 0 
 * type=0x89, tss系统段描述符;
 * type=0x82, ldt系统段描述符。*/
#define _set_tssldt_desc(n,addr,type) \
__asm__ ("movw $104,%1\n\t" \
	"movw %%ax,%2\n\t" \
	"rorl $16,%%eax\n\t" \
	"movb %%al,%3\n\t" \
	"movb $" type ",%4\n\t" \
	"movb $0x00,%5\n\t" \
	"movb %%ah,%6\n\t" \
	"rorl $16,%%eax" \
	::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)

/* set_tss_desc(n, addr),
 * 设置描述tss的系统段描述符。
 * 
 * set_ldt_desc(n,addr),
 * 设置描述ldt的系统段描述符。
 * 
 * (曾在head.s中设置过可执行段和数据段描述符)
 * 
 * 参数。
 * n: GDT段描述符首地址;
 * addr: 系统内存段tss或ldt内存基址。
 * "0x89"和"0x82"将充当系统段描述符的类型
 * (双引号在_set_tssldt_desc中将被消除),
 * 0x8: P=1, DPL=00;
 * 0x09: TYPE=1001, 系统段描述符n 描述addr处的tss;
 * 0x02: TYPE=0010, 系统段描述符n 描述addr处的ldt。*/
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x89")
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x82")
