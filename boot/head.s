/* 
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */
/* head.s包含了32位启动代码。
 * 注,启动代码从物理地址0x00000000处开始。从0x00000000开始的部分启动代码将会被页目录覆盖。*/

# head.s中包含了32位初始化程序. #
# 注: 32位启动程序head.s始于物理内存地址0x00000000处, 
# 随着head.s的执行, 页目录相关数据结构将覆盖head.s中的部分代码。#

.text
# 声明head.s中的以下标号为全局符号, 供后续C程序使用。
.globl _idt,_gdt,_pg_dir,_tmp_floppy_area
# 
# 页表目录(数据结构)起始处。
_pg_dir:
# 
startup_32:
# 进入保护模式后, 首先需将段描述符选择符加载给各段寄存器。
# 在给各数据段寄存器赋值时, 若合法的话, 
# CPU会将段描述符选择符10h对应的段描述符的内容隐式加载到
# 数据段寄存器的隐藏部分。
    movl $0x10,%eax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%fs
    mov %ax,%gs
# 设置ss:esp维护的栈内存,
# 将_stack_start内存处的低32位赋给esp, 高16位赋给ss。
# _stack_start高16位值为0x10, 低32位为&user_stack[PAGE_SIZE>>2],
# 即usser_stack数组末尾在其数据段中的偏移地址。
# ss:esp=0x10:&user_stack[PAGE_SIZE>>2]
# 表明二者维护的栈内存为段描述符选择符0x10对应内存段中的user_stack数组。
    lss _stack_start,%esp
# _stack_start在kernel/sched.c中定义(stack_start, 为拥有一页大小内存的全局数组),
# 当时的编译器在将C程序被转换为汇编时, 会在全局标识符前加上前缀符'_'。


# 重新设置IDT和GDT到head.s所在内存段中(0x0)。
    call setup_idt
    call setup_gdt
# 与setup.s中的设置相比, 
# 此处更新了可执行程序段和数据段的长度(由8Mb到16Mb),
# linux 0.11操作系统程序只读取了192Kb,
# 操作系统程序之后的内存(到16Mb处)用于外设缓冲区和
# 内核数据结构的动态分配等。

# 重新设置GDT后, 数据段描述符更新了内存段的长度,
# 重新加载数据段描述符到各数据段寄存器中。
    movl $0x10,%eax # reload all the segment registers
    mov %ax,%ds     # after changing gdt. CS was already
    mov %ax,%es     # reloaded in 'setup_gdt'
    mov %ax,%fs
    mov %ax,%gs
    lss _stack_start,%esp
# cs还没有被CPU重新隐式加载, 由于只更新了可执行内存段的长度,
# 只要不访问可执行段8Mb以外的内存(OS <= 192Kb), 
# 暂时不更新cs也没有关系。

# 若A20选通失败, 
# 那么0x100000这个地址的最高位不会在A20地址线上体现,
# 即0x100000被截断为0x00000, 因eax值和0x0地址内的值相同从而进入死循环。
    xorl %eax,%eax
1:  incl %eax           # check that A20 really IS enabled
    movl %eax,0x000000  # loop forever if it isn't
    cmpl %eax,0x100000
    je 1b # 向后跳转到标号1处(b: backward)。
/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
# 若协处理器存在则置CR0 MP位(bit[1])告知CPU协处理器存在。
    movl %cr0,%eax          # check math chip
    andl $0x80000011,%eax   # Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
    orl $2,%eax     # set MP
    movl %eax,%cr0
    call check_x87

# 最后设置页表目录和页表,
# 页表目录_pg_dir将覆盖内存[0x0, 0x1000),
# 4个页表pg0-pg3将覆盖占用内存[0x1000, 0x5000), 
# 1个页表目录和1个页表各占4Kb内存, 它们会覆盖操作系统程序前20Kb内容。
    jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
# 若协处理器不存在则恢复CR0 MP的置位。
check_x87:
    fninit      # 向协处理器发初始化命令
    fstsw %ax   # 取协处理器状态字
    cmpb $0,%al # 若状态字为0则表示存在
    je 1f       /* no coprocessor: have to set bits */
    movl %cr0,%eax
    xorl $6,%eax    /* reset MP, set EM */
    movl %eax,%cr0
    ret
.align 2
1:  .byte 0xDB,0xE4 /* fsetpm for 287, ignored by 387 */
    ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
# setup_idt #
# 在IDT即标号_idt中设置256个IDT描述符
# 并用定义在本文件中的ignore_int子程序将他们初始化为IDT中断门描述符。#
# 初始化完成后将IDT的地址和长度加载到IDTR寄存器。#
# 
# 各IDT描述符被初始化的结果如下所示 #
# ---------------------------------------------------------------
# | ignore_int offset[31..16] | 1 | 00 | 01110 | 000 | Reserved |
# --------------------------------------------------------------- 4
# |              8            |     ignore_int offset[15..0]    |
# --------------------------------------------------------------- 0
# IDT描述符位含义可参考setup.s。
# 在后续C程序中, 会根据需求陆续设置相应的IDT描述符。#
setup_idt:
    lea ignore_int,%edx
    movl $0x00080000,%eax
    movw %dx,%ax        /* selector = 0x0008 = cs */
    movw $0x8E00,%dx    /* interrupt gate - dpl=0, present */

    lea _idt,%edi  # IDT首地址, 见_idt标号处。
    mov $256,%ecx
rp_sidt:
    movl %eax,(%edi)
    movl %edx,4(%edi)
    addl $8,%edi
    dec %ecx
    jne rp_sidt
    lidt idt_descr # 加载IDT(_idt)的长度和基址到IDTR寄存器, 见idt_descr标号处。
    ret
# 
# 粗略理解IDT, CPU通过IDTR访问IDT,
# 虽然IDT可以设置在任何空闲内存中,
# 但需将其所在内存地址和长度赋值给IDTR寄存器。
#
# IDT               idt descriptor offset
# |--------------|   0
# |idt descriptor|
# |--------------|   1
# |idt descriptor|
# |--------------|   ...
# |   ...        |
# |--------------|   8191
# |idt descriptor|
# |--------------|
# IDT由IDT描述符组成, 每个描述符占8个字节。
# IDTR低16位限长65536字节, 即IDT最多能有8192个段描述符。
#
# 此处粗略了解两类IDT描述符,
# 这两类IDT描述符用于描述中断或异常的处理程序所在内存段及访问信息。
# [1] IDT中断门描述符。
# |31                           |15         |7           0
# --------------------------------------------------------
# |    routine offset[31..16]   |P|DPL|01110|000|Reserved|
# -------------------------------------------------------- 4
# | segment descriptor selector | routine offset[15..0]  |
# -------------------------------------------------------- 0
#
# [2] IDT陷阱门描述符。
# |31                           |15             |7        0
# ---------------------------------------------------------
# |    routine offset[31..16]   |P|DPL|01111|000|Reserved |
# --------------------------------------------------------- 4
# | segment descriptor selector |  routine offset[15..0]  |
# --------------------------------------------------------- 0
# DPL: IDT描述符特权级等级,
# DPL=00, 最高特权级(系统级),
# DPL=xx, ..., 
# DPL=11,最低特权级(用户级)。
# 由于80x86特权级会影响某些指令的行为, 此处粗略关注下IDT描述符的DPL。
# 只有通过int/into指令让CPU引用IDT描述符时才会检查IDT描述符中的DPL
# 需满足CPL <= IDT.DPL, 其余中断(如外中断)引用IDT描述符时将免去这一检查。
# 另外, 通过IDT描述符访问GDT段描述符时, 需满足CPL >= GDT.DPL。
#
# P(Present): P=0, IDT描述符无效; P=1-有效。
# routine offset: 处理程序在其可执行段中的偏移地址。
# 段描述符选择符(segment descriptor selector): 处理程序所在可执行段的段描述符选择符。
#
# 当CPU引用IDT描述符时, CPU通过类似引用GDT段描述符的检查后,
# 将根据其中的段描述符选择符获取到其处理程序可执行段的内存基址,
# 结合其处理程序偏移地址得到处理函数在其可执行段中的内存地址,
# 从而跳转执行该内存地址上的指令。
# 
# 先粗略了解IDT描述符的位格式,
# 在阅读kernel/traps.c/trap_init()函数时再粗略理解触发CPU引用IDT描述符的机制吧。

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
# setup_gdt #
# 重新设置GDT(见标号_gdt处)并将其基址和长度加载到GDTR寄存器种。#
# 此处只在GDT中设置了两个GDT描述符, 除了内存段长度外, #
# 这两个GDT描述符的内容跟setup.s中设置的一样。#
#
# 后续C程序将会引用head.s中设置的GDT以及IDT, #
# 之前在setup.s中设置的GDT和IDT将被抛弃不用了。#
setup_gdt:
    # 加载GDT的长度和基址到GDTR寄存器中。
    lgdt gdt_descr
    ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
# head.s将内核页表数据结构紧跟在页表目录_pg_dir之后, #
# 此处一共设置了4个页表, 对应[0x0, 0x1000000)这16Mb物理内存。#
# 开始的16Mb内存刚好是操作系统程序即内核所使用的内存。#
.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
# tm_floppy_area这1Kb内存用于当DMA获取不到buffer时供软驱使用的场景。#
# 由于需要内存对齐, 该段内存并没有横跨以64Kb对齐的内存边界。#
_tmp_floppy_area:
    .fill 1024,1,0

# 在页表相关数据结构设置完毕后, 跳转执行main函数。
# 在栈中压入
# 3个0作为main函数参数(未使用),
# main执行完毕后的返回地址, 
# main函数在其内存段中的偏移地址,
# 
# 然后跳转执行setup_paging,
# 依靠setup_paging中的ret指令
# 从栈中弹出main函数偏移地址到eip从而跳转执行main。
after_page_tables:
    pushl $0        # These are the parameters to main :-)
    pushl $0
    pushl $0
    pushl $L6       # return address for main, if it decides to.
    pushl $_main
    jmp setup_paging
L6:
    jmp L6  # main should never return here, but
            # just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
int_msg:
    .asciz "Unknown interrupt\n\r"
.align 2
#
# ignore_init子程序
# 调用printk(定义在kernel/prink.c)打印
# 用于提示未知中断("Unknown interrupt\n\r")的字符串。
# ignore_init函数被本程序用于以中断门的格式初始化IDT。
ignore_int:
    pushl %eax
    pushl %ecx
    pushl %edx
    push %ds
    push %es
    push %fs
    movl $0x10,%eax # GDT[2]
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%fs
    pushl $int_msg
    call _printk # 定义在kernel/printk.c中
    popl %eax
    pop %fs
    pop %es
    pop %ds
    popl %edx
    popl %ecx
    popl %eax
    iret


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */
# setup_paging #
# 子程序通过设置CR0寄存器PG位开启页机制, #
# 并设置4个页表对应开始的16Mb内存。#
#
# 注：尽管所有的物理内存都可以通过页表目录、页表数据结构映射, #
# 但只有操作系统内核中的页操作函数才会直接使用扩展内存(1M以外的)。#
# 其余函数所使用的内存将会由内存管理模块mm内的函数管理分配。#
#
 .align 2
# 设置页表目录和页表, 开启页机制。
setup_paging:
#
# 以下这段汇编指令将[0x0, 0x5000)这20k内存初始化为0,
# 这会覆盖head.s中_pg_dir到_tmp_floppy_area之间的代码。
    movl $1024*5,%ecx   /* 5 pages - pg_dir+4 page tables */
    xorl %eax,%eax
    xorl %edi,%edi      /* pg_dir is at 0x000 */
    cld;rep;stosl
# cld; rep; stosl相当于
# while (ecx--)
#     movl eax, es:edi
#     edi += 4
#
# 将页表pg0-pg3的信息设置在页表目录_pg_dir中, 
# 将这4个页表都设置为 可读可写&&任何特权级的程序都可以访问。
    movl $pg0+7,_pg_dir     /* set present bit/user r/w */
    movl $pg1+7,_pg_dir+4   /*  --------- " " --------- */
    movl $pg2+7,_pg_dir+8   /*  --------- " " --------- */
    movl $pg3+7,_pg_dir+12  /*  --------- " " --------- */
# 页表目录中的页表信息共4个字节, 其在内存中的位格式为
# |31                     12|11   9               0
# -------------------------------------------------
# |                         |     |  | | |  |U|R| |
# | page table addr[31..12] | AVL |00|D|A|00|/|/|P|
# |                         |     |  | | |  |S|W| |
# -------------------------------------------------
# 页表信息用于描述页表及其访问属性。
# 页表地址(page table addr): 页表基址(以4k对齐, 低12位由CPU自动补0)。
# AVL: 编程自定义使用。
# D: 已修改位, 由硬件置位。
# A: 已访问位, 由硬件置位。
# R/W: 读/写位。1-可读可写; 0-仅可读或可执行。
# U/S: 用户/超级用户位,
# 1-运行在任何特权级的程序皆可访问;0-超级用户特权级(0,1,2)程序可访问。
# P: 页表是否可用; 1-可用;0-不可用。
#
# 在pg0 - pg3 4个页表中设置页表项描述内存页。
# 页表项用于描述一页内存, 其位格式同页目录中的页表信息。
#
# 将页表项设置为
# 都有效, 其记录的内存页都可读可写。
# 
# 页表项与内存页的对应关系为:
# 页表    页表项偏移               内存页
# pg3    4095       [0x00fff000, 0x00ffffff]
# pg3    4094       [0x00ffe000, 0x00ffefff]
#         ...                ...
# pg0      1        [0x00001000, 0x00001fff]
# pg0      0        [0x00000000, 0x00000fff]
    movl $pg3+4092,%edi
    movl $0xfff007,%eax /*  16Mb - 4096 + 7 (r/w user,p) */
    std
1:  stosl   /* fill pages backwards - more efficient :-) */
    subl $0x1000,%eax
    jge 1b
# 1个页表有4Kb, 按照1个页表项4字节计算,
# 1个页表共可设置1Kb个页表项, 4个页表可设4Kb个页表项,
# 若每个页表项记录的一页内存大小4Kb, 则一共可以记录16Mb内存。
#
# 此处设置4个页表是为了映射操作系统内核所使用的16Mb内存,
# 对于后续内存地址>16Mb内存的访问,
# 操作系统内核程序会为其创建页表和页目录项来映射该段内存。
#
# 将页表目录_pg_dir地址(0x0)加载到CR3。
    xorl %eax,%eax  /* pg_dir is at 0x0000 */
    movl %eax,%cr3  /* cr3 - page directory start */
#
# 设置CR0 PG位(bit[31])开启分页机制。
    movl %cr0,%eax
    orl $0x80000000,%eax
    movl %eax,%cr0  /* set paging (PG) bit */
    ret             /* this also flushes prefetch-queue */
# ret指令相当于popl eip, 这将从栈顶弹出main函数偏移地址到eip,
# 即跳转执行init/main.c中的main函数。
#
# 分页机制开启后, 内存地址除了需要根据段描述符中的分段计算外, 还涉及页变换。
# 
# 以ret指令跳转执行main函数为例, 粗略理内存地址的换算过程。
# [1] 获取内存线性地址: cs隐藏部分中的内存基址+偏移地址eip(_main)。
#
# [2] 页变换。
# 启动页变换机制后, CPU将内存线性地址划分为几个部分。
# 一部分用作 描述页表的 页表信息在页目录中的索引,
# 一部分用作 描述内存页的 页表项在页表中的索引,
# 一部分用作 在内存页中的偏移。
# 
# 对于任意一个32位内存地址,
# 可将该内存地址按如下划分映射到一页物理内存。
# |31                22|21              12|11             0|
# ----------------------------------------------------------
# |       DT_IDX       |      DTI_IDX     |     OFFSET     |
# ----------------------------------------------------------
# DT_IDX:  页表信息在页目录中的索引。
# DTI_IDX: 页表项在页表中的索引。
# OFFSET:  在内存页内的偏移。
#
# 一个32位内存地址映射到物理内存页的过程为,
# CPU从CR3中读取页目录基址, 结合32位内存地址高10位获取到页表信息地址,
# 通过页表信息地址可获取到页表信息, 页表信息高20位保存了一个页表的地址,
# 在获取到页表地址后, 再根据32位内存地址中间10位可获得其页表项在页表中的偏移地址,
# 通过页表地址+页表项偏移地址得到该32位内存地址的页表项地址,
# 通过页表项地址可获取到页表项内容, 页表项内容的高20位存储了物理内存页的首地址
# 最后通过物理内存页首地址和32位线性地址的低12位得到得到该32位所映射的物理地址。
#
# 假设根据段描述符选择符cs和eip计算得到一个32位内存地址为0x00001000
# 获取页表信息地址。
# 通过32位内存地址高10位得到页表信息在页目录中的偏移0000000000b,
# 结合CR3中保存的页目录地址0可得到相应页表信息的实际地址为0x0,
# 
# 获取页表地址。
# 从0x0处取出页表信息高20位得到页表地址0x1000。
# 
# 获取页表项地址。
# 根据32位内存地址中间10位获取到其页表项偏移值0000000001b,
# 由此得到页表项地址0x1000+0x1 * 4=0x1004, 
# 即页表项地址0x1004高20位存储了物理内存页的首地址, 为0x1000。
#
# 获取最终的物理内存地址。
# 最终物理内存地址为物理内存页首地址加上32位内存地址低12位,
# 即0x1000。
#
# 若页表0 页表项1所映射的内存页为其他内存页如[0x0, 0x00000fff],
# 则32位内存地址0x00001000经过页变换后将得到物理内存地址0x00000000。
# 
# 在经历实模式和保护模式初始化的汇编代码后, 
# 终于可 以C语言为主来实现操作系统程序了。
# 
# 回想从bootsect.s开始到head.s结束
# 根据这一路所经历的汇编程序所学到的其背后所隐藏的计算机基础知识, 还是有些振奋。


# (1) head.s运行结束, 概括其主要功能。
# -----------------------------------
# [1] 在[0x0, ..]内存中重新设置IDT和GDT;
# [2] 设置页表目录、页表并开启页机制, 随后跳转执行init/main.c中的main函数。
# [3] 将操作系统数据段加载到各数据段寄存器, 设置ss:esp所维护的栈内存。
#
#
# (2) head.s执行后, 内存地址空间的大体分布。
# 0x0000000------------ (head.s)
#          | _pg_dir  |
# 0x0001000|==========|
#          | pg0 - pg3|
# 0x0005000|==========|
#          |   ...    |
#          |   IDT    |
#          |   GDT    |
#          |          |
# cs=0x08  |   ...    |
#    eip → |----------| (*.c)
#          |   main   |
#          |          |
# ss=0x10  |user_stack|
#    esp → |          |
#          |   ...    |
# 0x0090000|----------| (bootsect.s)
#          | infos by |
#          |   BIOS   |
# 0x0090200|----------|
#          | setup.s  |
#          |----------|
#          |   ...    |
# 0x1000000|==========|
#
# (3) 粗略理解操作系统前期使用汇编语言的主要原因。
# 有的功能只能用汇编指令及相应标识符完成, 
# 即这些汇编指令在C中无对应的语句。
# 如访问寄存器,
# 如in/out/int/iret/lgdt/lidt/lss等指令所完成的功能,
# 
# 当需要频繁使用这些指令或标识符时就可以直接使用汇编程序, 而非内联汇编。
# 2019.05.26

# idt_descr处的6字节内容充当lidt指令操作数加载给IDTR,
# 低16位为IDT长度,  高32位为IDT地址(_idt)。
.align 2
.word 0
idt_descr:
    .word 256*8-1   # idt contains 256 entries
    .long _idt

# gdt_descr处的6字节内容充当lgdt指令操作数加载给GDTR,
# 低16位为GDT长度, 高32位为GDT地址(_gdt)。
.align 2
.word 0
gdt_descr:
    .word 256*8-1   # so does gdt (not that that's any
    .long _gdt      # magic number, but it works for me :^)

    .align 3
_idt:   .fill 256,8,0   # idt is uninitialized

# 对照setup.s中GDT段描述符位格式,
# GDT[0] 为保留的空描述符。
#
# GDT[1]描述[0x0, 0x1000000)内存段, 16Mb,
# TYPE=0x9a, P=1(有效), S=T=1(可执行段描述符), C=A=0, R=1(可读);
# 0xc0, G=D=1, 内存段颗粒度为4Kb, 默认操作数为32位。
# 
# GDT[2]描述[0x0, 0x1000000)内存段, 16Mb,
# TYPE=0x92, P=1(有效), S=1&&T=0&&W=1(可写数据内存段), E=A=0;
# 0xc0, G=D=1, 内存段颗粒度为4Kb, 默认操作数为32位。
#
# GDT[3]保留。
# GDT[4..]供后续设置LDT或TSS描述符。
_gdt:   .quad 0x0000000000000000    /* NULL descriptor */
        .quad 0x00c09a0000000fff    /* 16Mb */
        .quad 0x00c0920000000fff    /* 16Mb */
        .quad 0x0000000000000000    /* TEMPORARY - don't use */
        .fill 252,8,0               /* space for LDT's and TSS's etc */
