/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * system_call.s 包含了系统调用入口处理程序。定时器,硬盘中断,软盘中断入口处理函数
 * 也在本文件中,因为他们有部分代码和系统调用是相同的。
 *
 * 注,在每次定时器中断和系统调用完毕后会检查并处理进程信号。不在其他中断中处理检查
 * 或处理信号,以免把信号处理整得很凌乱。
 *
 * Stack layout in 'ret_from_system_call':
 * 在 ret_from_sys_call 中时,栈中内容如下:
 *
 *  0(%esp) - %eax
 *  4(%esp) - %ebx
 *   8(%esp) - %ecx
 *   C(%esp) - %edx
 *  10(%esp) - %fs
 *  14(%esp) - %es
 *  18(%esp) - %ds
 *  1C(%esp) - %eip
 *  20(%esp) - %cs
 *  24(%esp) - %eflags
 *  28(%esp) - %oldesp
 *  2C(%esp) - %oldss
 */
/* 在用户程序中(CPL=3)发生中断进入内核代码段涉及特权级变化,
 * 所以在中断发生时CPU进行现场保护时涉及栈变换,即CPU的现场
 * 保护相当于 push ss; push esp; pushf; push cs; push eip */
SIG_CHLD = 17

/* 执行到ret_from_system_call程序时,寄存器在栈中的备份情况如下 */
EAX = 0x00 /* ss:esp处备份了eax */
EBX = 0x04 /* ss:esp[0x04]处备份了ebx */
ECX = 0x08 /* ... */
EDX = 0x0C
FS  = 0x10
ES  = 0x14
DS  = 0x18
EIP = 0x1C
CS  = 0x20
EFLAGS  = 0x24
OLDESP  = 0x28
OLDSS   = 0x2C

/* struct task_struct结构体内各成员偏移 */
state       = 0 # these are offsets into the task-struct.
counter     = 4
priority    = 8
signal      = 12
sigaction   = 16 # MUST be 16 (=len of sigaction)
blocked = (33*16)

# offsets within sigaction
/* struct sigaction 结构体内各成员偏移 */
sa_handler = 0
sa_mask = 4
sa_flags = 8
sa_restorer = 12

/* 系统调用个数 */
nr_system_calls = 72

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error

/* 非法系统调用,置-1于eax(返回-1)然后中断返回 */
.align 2
bad_sys_call:
    movl $-1,%eax
    iret

/* 跳转执行任务调度函数 */
.align 2
reschedule:
# schedule函数返回执行ret语句时将从栈中弹出此处压入
# 栈中的地址从而跳转执行ret_from_sys_call以从内核中返回
    pushl $ret_from_sys_call
    jmp _schedule

# _system_call系统调用入口函数
# 当通过int 80h指令从用户程序中调用系统API时,
# CPU在进行现场保护时会将以下寄存器依次压入栈中
# ss esp eflag cs eip
# -------------------
.align 2
_system_call:
/* 在进行系统调用时,eax=系统调用号。
 * 此处判断eax是否超过系统调用最大个数,
 * 若超过则跳转执行bad_sys_call */
    cmpl $nr_system_calls-1,%eax
    ja bad_sys_call

/* 依次备份用户程序的数据段寄存器和普通寄存器,
 * 除fs段寄存器仍指向用户程序内存段外,其余段寄
 * 存器皆指向内核段;即在内核态时, 通过fs段寄存
 * 器可以实现用户内存段和内核内存段的数据拷贝。*/
    push %ds
    push %es
    push %fs
    pushl %edx
    pushl %ecx  # push %ebx,%ecx,%edx as parameters
    pushl %ebx  # to the system call
    movl $0x10,%edx # set up ds,es to kernel space
    mov %dx,%ds
    mov %dx,%es
    movl $0x17,%edx # fs points to local data space
    mov %dx,%fs

/* call (4 * eax + _sys_call_table),
 * 根据系统调用号eax调用相应系统调用,
 * 如系统调用号eax=2将调用sys_fork。*/
    call _sys_call_table(,%eax,4)
    pushl %eax # 将系统调用返回值压入栈中备份

    /* 若当前任务为不可运行状态则调用任务调度函数进行任务切换 */
    movl _current,%eax
    cmpl $0,state(%eax) # if (0 != _current->state) reschedule();
    jne reschedule

    /* 检查当前任务时间片,若运行完毕则进行任务调度 */
    cmpl $0,counter(%eax)
    je reschedule

/* 这是每个系统调用和中断处理程序执行完毕后都会跳转执
 * 行的子程序,该子程序用于处理当前任务所需处理的信号。*/
ret_from_sys_call:
/* 若当前任务为初始任务,则向前跳转到3标号处 */
    movl _current,%eax
    cmpl _task,%eax
    je 3f

/* 若进入内核态前的任务为超级任务(内核段选择符不为0xf)则向前跳转到标号3处 */
    cmpw $0x0f,CS(%esp) # was old code segment supervisor
    jne 3f

/* 若进入内核态前的任务栈段不为用户态栈(不等于0x17)则向前跳转到标号3处 */
    cmpw $0x17,OLDSS(%esp)  # was stack segment = 0x17
    jne 3f

/* 若进入内核的任务为应用程序任务,则处理任务中非
 * 被屏蔽的信号。blocked nr位为1时表屏蔽nr信号。*/
    movl signal(%eax),%ebx  # task[0].signal
    movl blocked(%eax),%ecx # task[0].blocked
    notl %ecx       # blocked各位取反
    andl %ebx,%ecx  # 取没被阻塞的signal
    bsfl %ecx,%ecx  # 从低到高(0->31)取第一位为1的位的索引存到ecx中
    je 3f           # 若全为0则不进行信号处理向前跳转到标号3处
    btrl %ecx,%ebx  # 将ebx的ecx清0, 并存入signal中即清信号 ecx + 1
    movl %ebx,signal(%eax)
    incl %ecx       # 对应的信号值 ecx + 1
    pushl %ecx      # 信号值入栈作为_do_signal函数最后/左一个参数
    call _do_signal # 处理任务的信号,kernel/signal.c
    popl %eax

# 恢复存在栈中的寄存器
3:  popl %eax
    popl %ebx
    popl %ecx
    popl %edx
    pop %fs
    pop %es
    pop %ds
    iret   # 恢复系统调用和中断现场

/* _coprocessor_error,
 * 协处理器中断处理入口程序。
 * 当协处理器发生错误时向CPU发送中断号为16的中断信息,
 * 让CPU调用IDT[16]中的中断处理程序。*/
.align 2
_coprocessor_error:
    push %ds
    push %es
    push %fs
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    movl $0x10,%eax # 内核数段描述符
    mov %ax,%ds
    mov %ax,%es
    movl $0x17,%eax # 用户数据段描述符
    mov %ax,%fs
    # _math_error执行RET指令时将跳转执行ret_from_sys_call
    pushl $ret_from_sys_call
    jmp _math_error

/* _device_not_available,
 * 若CR0中EM置位,CPU执行协处理器指令时会处理IDT[7]中断
 * 以让device_not_available模拟协处理器执行协处理器指令。*/
.align 2
_device_not_available:
    push %ds
    push %es
    push %fs
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    movl $0x10,%eax
    mov %ax,%ds
    mov %ax,%es
    movl $0x17,%eax
    mov %ax,%fs
    pushl $ret_from_sys_call
    clts  # clear TS so that we can use math
    movl %cr0,%eax
    testl $0x4,%eax # EM (math emulation bit)
    je _math_state_restore
    pushl %ebp
    pushl %esi
    pushl %edi
    call _math_emulate
    popl %edi
    popl %esi
    popl %ebp
    ret

/* 经过对定时器8254和中断控制器8259A的配置,
 * 定时器中断(IDT[20h])约10ms产生一次。*/
.align 2
_timer_interrupt:
/* 备份中断前的数据段寄存器和普通寄存器 */
    push %ds    # save ds,es and put kernel data space
    push %es    # into them. %fs is used by _system_call
    push %fs
    pushl %edx  # we save %eax,%ecx,%edx as gcc doesn't
    pushl %ecx  # save those across function calls. %ebx
    pushl %ebx  # is saved as we use that in ret_sys_call
    pushl %eax

/* 除fs段寄存器外,其余段寄存器指向内核段;
 * 通过指向用户态的fs段寄存器可实现内核和
 * 用户程序之间的数据拷贝。*/
    movl $0x10,%eax # 加载内核数据段描述符到数据段寄存器。
    mov %ax,%ds
    mov %ax,%es
    movl $0x17,%eax # 0x17为用户程序数据段描述符。
    mov %ax,%fs

    /* 系统时间片增1,即系统时间片时间又增加10ms */
    incl _jiffies # _jiffies定义在kernel/sched.c中(jiffies, 初始值为0)。

/* 往PIC发送EOI命令以结束本次定时器中断,见setup.s中对PIC的设置 */
    movb $0x20,%al  # EOI to interrupt controller
    outb %al,$0x20

    /* 检查定时器中断时所在任务的特权级 */
    movl CS(%esp),%eax  # 发生定时器中断时被压入栈时的cs寄存器。
    andl $3,%eax        # %eax is CPL (0 or 3, 0=supervisor)

/* 将eax入栈作为do_timer的参数并调用do_timer函数 */
    pushl %eax  # 请求特权级入栈作为do_timer的参数
    call _do_timer  # 'do_timer(long CPL)' does everything from
    addl $4,%esp    # task switching to accounting ...

/* 跳转执行ret_from_sys_call*/
    jmp ret_from_sys_call

/* _sys_execve,
 * 系统调用execve函数入口处。*/
.align 2
_sys_execve:
    /* 取 发生系统调用execve()时eip寄存器被CPU备份在栈中的 地址,
     * 并将该地址压入栈中作为do_execve函数最后一个参数,前三个函数
     * 在 _system_call 中被入栈。
    lea EIP(%esp),%eax
    pushl %eax
    call _do_execve # fs/exec.c/ do_execve
    addl $4,%esp    # 回收do_execve参数栈内存
    ret

/* _sys_fork,
 * 系统调用fork()内核代码入口处。*/
.align 2
_sys_fork:
    /* 调用fork.c/find_empty_process以获取未用进程
     * 号和管理进程的空闲结构体元素下标于eax。若当
     * 前无管理进程的空闲结构体则向前跳转标号1处。*/
    call _find_empty_process
    testl %eax,%eax
    js 1f
    /* 将gs esi等寄存器压入栈中以作为fork.c/copy_process参数 */
    push %gs
    pushl %esi
    pushl %edi
    pushl %ebp
    pushl %eax
    call _copy_process
    addl $20,%esp # 回收copy_process实参栈内存
# 返回到_system_call中call _sys_call_table(,%eax,4)之后语句处
1:  ret

/* _hd_interrupt,
 * 设置在IDT[0x2e]中的硬盘中断入口程序。
 * 当通过hd_out函数向硬盘下发读写等命令后,
 * 在硬盘准备好被读写后就会向PIC IRQ14输出
 * 相应的硬盘中断,从而让CPU执行IDT[0x2e]中
 * 的处理函数即此处的_hd_interrupt。
 *
 * CPU跳转执行_hd_interrupt时,在栈中依次保存
 * 了以下寄存器(即中断现场保护) ss esp flag cs eip。
 *
 * 在_hd_interrupt中继续依次入栈的寄存器有
 * eax ecx edx ds es fs。*/
_hd_interrupt:
    pushl %eax
    pushl %ecx
    pushl %edx
    push %ds
    push %es
    push %fs
    movl $0x10,%eax # ds和es切换到内核数据段GDT[1]
    mov %ax,%ds
    mov %ax,%es
    movl $0x17,%eax # 加载用户数据段LDT[2]到fs
    mov %ax,%fs
    movb $0x20,%al  # 见setup.s中对8259A的设置
    outb %al,$0xA0  # EOI to interrupt controller #1
    jmp 1f          # give port chance to breathe
1:  jmp 1f
1:  xorl %edx,%edx    # 异或操作将edx清0
    xchgl _do_hd,%edx # 交换do_hd和edx的内容,do_hd的赋值见hd_out函数
    testl %edx,%edx
    jne 1f # 若hd_out非空则向前跳转到标号1处
    movl $_unexpected_hd_interrupt,%edx # 若do_hd函数指针为NULL,则赋值此函数给do_hd。
1:  outb %al,$0x20
    call *%edx  # "interesting" way of handling intr,如read_intr, write_intr etc.
# 中断函数执行完毕后, 从栈中恢复寄存器的值, 同时执行iret恢复中断现场。
    pop %fs
    pop %es
    pop %ds
    popl %edx
    popl %ecx
    popl %eax
    iret

/* _floppy_interrupt,
 * 设置在IDT[26h]中软盘中断入口处理程序。
 *
 * 在向软盘下发复位,校正,DMA读写命令并在软盘完成
 * 这些命令后,软盘将引发软盘中断即会让CPU直行本函数,
 * 本函数将调用do_floppy函数,do_floppy函数挂载了复位,
 * 校正,DMA读写等中断处理C函数。
 *
 * 其余一些信息同_hd_interrupt。*/
_floppy_interrupt:
    pushl %eax
    pushl %ecx
    pushl %edx
    push %ds
    push %es
    push %fs
    movl $0x10,%eax
    mov %ax,%ds
    mov %ax,%es
    movl $0x17,%eax
    mov %ax,%fs
    movb $0x20,%al
    outb %al,$0x20  # EOI to interrupt controller #1
    xorl %eax,%eax
    xchgl _do_floppy,%eax
    testl %eax,%eax
    jne 1f
    movl $_unexpected_floppy_interrupt,%eax
1:  call *%eax  # "interesting" way of handling intr.
    pop %fs
    pop %es
    pop %ds
    popl %edx
    popl %ecx
    popl %eax
    iret

/* _parallel_interrupt,
 * 并行口中断处理入口程序IDT[39]。
 * 该程序向PIC发送EOI命令以结束并口中断,其他功能还未实现。*/
_parallel_interrupt:
    pushl %eax
    movb $0x20,%al
    outb %al,$0x20
    popl %eax
    iret
