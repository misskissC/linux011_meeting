/* 目前还未在程序中使用过该宏, 顺便也阅读下outb宏吧。
 * 
 * outb(value, port),
 * 将1字节数据value输出到端口地址port处。
 * 
 * 内联汇编输入。
 * "a"(value), 将value赋给eax;
 * "d"(port),  将port赋给edx;
 * 
 * outb %%al, %%dx, 将al赋给dx值表示的端口处。
 * */
#define outb(value,port) \
__asm__ ("outb %%al,%%dx"::"a" (value),"d" (port))


/* 功能: 读端口。
 *
 * 参数:
 * port - 端口地址,
 * 
 * 内联汇编指令描述。
 * 输入: edx = port,
 * 输出: _v = eax。
 *
 * 将port输入edx, 读端口dx数据到al, 
 * 将eax输出到_v, 并将_v作为"返回值"。*/
#define inb(port) ({ \
unsigned char _v; \
__asm__ volatile ("inb %%dx,%%al":"=a" (_v):"d" (port)); \
_v; \
})

/* outb_p(value, port),
 * 写1字节数据value到指定端口地址port处。
 *
 *
 * 内联汇编。
 * edx = port, eax = value,
 * 将al写往端口dx, 向前跳转执行标号1。
 *
 * 跳转语句用作延时供写端口操作完成/稳定,
 * outb_p用于之后会紧跟与port相关的i/o指令。*/
#define outb_p(value,port) \
__asm__ ("outb %%al,%%dx\n" \
    "\tjmp 1f\n" \
    "1:\tjmp 1f\n" \
    "1:"::"a" (value),"d" (port))

/* inb_p(port),
 * 从指定端口地址port处读取1字节内容。
 *
 * 内联汇编。
 * volatile其告知编译器不要优化内联汇编中的代码
 * (如不共享寄存器, 不改变指令顺序等, 不删除未使用的函数)。
 * 
 * 内联汇编指令。
 * edx = port,
 * 读端口地址dx处内容到al,
 * 向前跳转执行标号1(用作延时待读操作完成/稳定)
 * _v = eax.
 *
 * _v为表达式最终值。*/
#define inb_p(port) ({ \
unsigned char _v; \
__asm__ volatile ("inb %%dx,%%al\n" \
    "\tjmp 1f\n" \
    "1:\tjmp 1f\n" \
    "1:":"=a" (_v):"d" (port)); \
_v; \
})
