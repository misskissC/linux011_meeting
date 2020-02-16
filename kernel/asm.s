/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */

/* asm.s包含了大部分硬件错误的底层代码。page_exception 由mm模块完成,
 * 所以该异常处理函数不在本文件中。本文件还应处理由TS位所表征的fpu异
 * 常,因为fpu状态必须被正确的保存或重新存储,该功能还未经过测试。*/

.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved

# IDT[0]中的子程序divide_error。
#
# 粗略理解除法溢出中断发生过程。
# 当在内核(CPL=0)中执行除法指令发生溢出时,
# 由于divide_error也在内核可执行段中,
# 跳转执行divide_error时不涉及特权级转换, 此时的中断行为相当于
# pushf, TF=IF=0,
# push cs
# pushl eip
#
# 当CPU执行move_to_user_mode转到用户模式(CPL=3)中执行除法指令发生溢出时,
# 涉及特权级转换, 此时的中断行为相当于
# push  ss
# pushl esp
# pushf, TF=IF=0,
# push cs
# pushl eip
#
# 在备份各寄存器后,
# CPU根据除法溢出的中断码0引用IDT[0],
# 由于非int 0h指令触发中断, 此时CPU会免去CPL是否小于等于IDT[0].DPL的检查
# 而将IDT[0]中段描述符选择符字段(0x08)指向的GDT段描述符加载给cs。
# 即GDT[8h]被加载到cs的隐藏部分(在加载GDT[8h]到cs时, CPU会做CPL >= GDT[8h].DPL的检查),
# 并将IDT[0].divide_error在其可执行段0x08中的偏移地址赋值给eip,
# 最终使得CPU跳转执行divide_error。
# 
# 从当前任务TSS中的ss0:esp0取得内核栈,从而进行栈切换
#
# 内核初始化完成后会通过move_to_user_mode转到CPU用户模式下运行程序,
# 此处设置在IDT[0]中的divide_error是为用户模式编写的。
# 看看从用户程序中发生除法溢出时到
# 刚跳转到处理函数divide_error这个过程中的栈内容吧。
# | ... |
# -------
# | ss  |
# -------
# | esp |
# ------|
# |eflag|
# -------
# | cs  |
# -------
# | eip |
# -------
# 16位段寄存器ss和cs以占32位(以32位对齐存储)。

_divide_error:
# 将用C语言编写的do_divide_error函数的地址保存在栈中。
#
# _do_divide_error是在traps.c中定义的C函数do_divide_error,
# 该C函数才是除法溢出中断的处理函数。
# _divide_error只是充当了C处理函数的入口地址,
# 其中所包含的内容属于 用汇编指令更易或才能实现的程序。
# 在调用该程序时, 可以跳转到traps.c中看其定义。
    pushl $_do_divide_error

# CPU不会产生错误码中断处理汇编代码段。
# no_error_code:
#
# 在调用C中断处理函数前的栈内容。
# |  ...   |
# ----------
# |  ss    |
# ----------
# |  esp   |
# ---------|
# | eflag  |
# ----------
# |  cs    |
# ----------
# |  eip   | 基于eip所在栈内存地址的偏移
# ========== 0 <--|
# |  eax   |      |
# ---------- -4   |
# |  ebx   |      |
# ---------- -8   |
# |  ecx   |      |
# ---------- -12  |
# |  edx   |      |
# ---------- -16  |
# |  edi   |      |
# ---------- -20  |
# |  esi   |      |
# ---------- -24  |
# |  ebp   |      |
# ---------- -28  |
# |   ds   |      |
# ---------- -32  |
# |   es   |      |
# ---------- -36  |
# |   fs   |      |
# ---------- -40  |
# |   0    |      |
# ---------- -44  |
# |eip_addr| -----| 该4字节栈内存中保存了eip寄存器的栈地址
# ---------- -48
no_error_code:
# 在此语句处即刚执行完将C中断处理函数压栈指令时,
# esp指向C函数的栈地址, xchagl将eax和esp栈中的内容进行交换,
# 即eax值被保存到当前栈内存中, eax的值为C处理函数地址。
    xchgl %eax,(%esp)
# 依次将以下寄存器入栈保存
    pushl %ebx
    pushl %ecx
    pushl %edx
    pushl %edi
    pushl %esi
    pushl %ebp
    push %ds
    push %es
    push %fs
# 将0和发生中断时eip的入栈地址依次保存在栈中,
# 在将0压栈后, esp+44为发生中断时eip的入栈地址。
    pushl $0    # "error code"
    lea 44(%esp),%edx
    pushl %edx

# 将内核数据段描述符GDT[2]加载给各数据段寄存器,
# 如此才能正确访问内核数据段中的数据。
    movl $0x10,%edx
    mov %dx,%ds
    mov %dx,%es
    mov %dx,%fs
# 调用C中断处理函数(如do_divide_error)
    call *%eax
# 回收C中断处理函数实参栈内存, 回收实参栈内存后,
# esp指向fs栈内存处。
    addl $8,%esp
# 实参以从右向左顺序入栈, 实参栈内存由调用者回收,
# 即函数调用约定为__stdcall哦。

# 中断处理程序执行完毕, 依次恢复栈中所备份的寄存器。
    pop %fs
    pop %es
    pop %ds
    popl %ebp
    popl %esi
    popl %edi
    popl %edx
    popl %ecx
    popl %ebx
    popl %eax
# 恢复因中断行为入栈的寄存器,
# 从内核返回到用户模式时, 此语句相当于
# popl eip, pop cs, popf, popl esp, pop ss
    iret

_debug:
    pushl $_do_int3		# _do_debug
    jmp no_error_code

_nmi:
    pushl $_do_nmi
    jmp no_error_code

_int3:
    pushl $_do_int3
    jmp no_error_code

_overflow:
    pushl $_do_overflow
    jmp no_error_code

_bounds:
    pushl $_do_bounds
    jmp no_error_code

_invalid_op:
    pushl $_do_invalid_op
    jmp no_error_code

_coprocessor_segment_overrun:
    pushl $_do_coprocessor_segment_overrun
    jmp no_error_code

_reserved:
    pushl $_do_reserved
    jmp no_error_code

_irq13:
    pushl %eax
    # F0h为协处理器端口地址, 该设置确保CPU可以响应协处理器中断
    xorb %al,%al
    outb %al,$0xF0
    # 向8259A发中断结束EOI信号, setup.s中设置8259A时, 其不会自动结束中断
    movb $0x20,%al
    outb %al,$0x20
    jmp 1f
    1:  jmp 1f
    1:  outb %al,$0xA0
    popl %eax
    jmp _coprocessor_error

_double_fault:
    pushl $_do_double_fault
# CPU会产生错误码的中断入口处理程序。
# 对于CPU会产生错误码的中断,
# 当发生到此处时栈内存中的内容为
# |  ...   |
# ----------
# |  ss    |
# ----------
# |  esp   |
# ---------|
# | eflag  |
# ----------
# |  cs    |
# ----------
# |  eip   |
# ----------
# |err_code|
# ----------
# |  do_*  |
# ---------- <-- esp
# 在进入error_code子程序后的入栈及其他操作同no_error_code。
error_code:
    xchgl %eax,4(%esp)  # error code <-> %eax
    xchgl %ebx,(%esp)   # &function <-> %ebx
    pushl %ecx
    pushl %edx
    pushl %edi
    pushl %esi
    pushl %ebp
    push %ds
    push %es
    push %fs
    pushl %eax          # error code
    lea 44(%esp),%eax   # offset
    pushl %eax
    movl $0x10,%eax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%fs
    call *%ebx
    addl $8,%esp
    pop %fs
    pop %es
    pop %ds
    popl %ebp
    popl %esi
    popl %edi
    popl %edx
    popl %ecx
    popl %ebx
    popl %eax
    iret

_invalid_TSS:
    pushl $_do_invalid_TSS
    jmp error_code

_segment_not_present:
    pushl $_do_segment_not_present
    jmp error_code

_stack_segment:
    pushl $_do_stack_segment
    jmp error_code

_general_protection:
    pushl $_do_general_protection
    jmp error_code

