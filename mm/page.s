/*
 *  linux/mm/page.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * page.s contains the low-level page-exception code.
 * the real work is done in mm.c
 */
/* page.s 包含了页异常入口程序,主要功能由memory.c完成。*/
 
.globl _page_fault

# 页异常(IDT[14])处理入口程序。
# 从页异常发生到此处, 栈中的内容为。
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
# ---------- <-- esp
_page_fault:
    # 将eax寄存器值入栈于页错误码所在栈内存中, eax保存页错误码
    xchgl %eax,(%esp)
    pushl %ecx
    pushl %edx
    push %ds
    push %es
    push %fs
    # 将内核数据段描述符加载给段寄存器
    movl $0x10,%edx
    mov %dx,%ds
    mov %dx,%es
    mov %dx,%fs
    # 取页面异常地址到edx中,
    # 在页面异常发生时, 引起页面异常的内存地址会存在cr2中
    movl %cr2,%edx
    # 入栈页面异常地址和页错误码
    pushl %edx
    pushl %eax
    # 如果不是缺页异常则向前跳转执行mm/memory.c中的do_wp_page
    # 实现内存页的写时拷贝。
    testl $1,%eax
    jne 1f
    # 如果是缺页异常则跳转执行mm/memory.c中的do_no_page
    # 以实现从共用可执行程序文件共享页或
    # 从磁盘上读取一页数据到内存的操作。
    call _do_no_page
    jmp 2f
1:  call _do_wp_page
2:  addl $8,%esp # 回收传递给C处理函数实参所占用的栈内存
    # 依次恢复相关寄存器
    pop %fs
    pop %es
    pop %ds
    popl %edx
    popl %ecx
    popl %eax
# 中断返回, 从内核程序返回到用户程序相当于
# popl eip, pop cs, popf, popl esp, pop ss
    iret
