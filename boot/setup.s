!
!   setup.s (C) 1991 Linus Torvalds
!
! setup.s is responsible for getting the system data from the BIOS,
! and putting them into the appropriate places in system memory.
! both setup.s and system has been loaded by the bootblock.
!
! This code asks the bios for memory/disk/other parameters, and
! puts them in a "safe" place: 0x90000-0x901FF, ie where the
! boot-block used to be. It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.
!
! setup.s的主要功能是通过BIOS中断调用获取一些操作系统程序 !
! 要使用的系统参数并存于[0x90000, 0x90200)内存段中, !
! 如实模式以外的内存大小、硬盘参数、显示相关信息等。!
! 随后便设置CPU进入保护模式机制下运行。!

! NOTE! These had better be the same as in bootsect.s
INITSEG  = 0x9000 ! we move boot here - out of the way
SYSSEG   = 0x1000 ! system loaded at 0x10000 (65536).
SETUPSEG = 0x9020 ! this is the current segment

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

! 由0x9000内存段中的bootsect.s
! 执行段间跳转指令跳转执行到此处指令。
entry start
start:
!
! ok, the read went well so we get current cursor position and save it for
! posterity.
! 通过BIOS 10h中断调用获取光标当前所在位置,
! 将光标所在行列信息存于始于0x90000 2字节内存中。
! 这将覆盖0x90000内存处的bootsect.s代码,
! 由于bootsect.s已经运行完毕且不会再运行, 其所在内存可作他用。
    mov ax,#INITSEG ! this is done in bootsect already, but...
    mov ds,ax
    mov ah,#0x03 ! read cursor pos
    xor bh,bh
    int 0x10   ! save it in known place, con_init fetches
    mov [0],dx ! it from 0x90000.

! Get memory size (extended mem, Kb)
! 通过BIOS 15h中断调用获取扩展内存(实模式之外的内存大小, 单位Kb)
! 存于始于0x90002的 2字节内存中。
    mov ah,#0x88
    int 0x15
    mov [2],ax

! Get video-card data:
! 通过BIOS 10h获取显卡信息,
! 将当前显示页存于始于0x90004的 2字节内存中,
! 将当前显示模式(如320x200 4色)和窗口宽度(屏幕显式字符的列数)
! 分别存在0x90006和0x90007内存处。
    mov ah,#0x0f
    int 0x10
    mov [4],bx  ! bh = display page
    mov [6],ax  ! al = video mode, ah = window width

! check for EGA/VGA and some config parameters
! 通过BIOS 10h获取EGA/VGA的一些配置参数,
! BH = 0/1: 彩色/字符显示模式; 存于0x9000B内存处,
! BL = 0/1/2/3/: 64k/128k/192k/256k EGA显存, 存于0x9000A内存处,
! CH = 特征位, CL = 开关设置, 分别存于0x9000D和0x9000C内存处,
! AX = ??, 存于0x90008内存处。
    mov ah,#0x12
    mov bl,#0x10
    int 0x10
    mov [8],ax
    mov [10],bx
    mov [12],cx

! Get hd0 data
! 获取第1个硬盘的参数信息, 1个硬盘参数信息共16字节。
! lds si, [4*0x41] 将ds:4*0x41处的低2字节赋给si,
! 高2字节赋给ds。
! 
! 后续指令将ds:si指向的16字节内容拷贝到
! 始于es:si的内存段[0x90080, 0x90090)中。
! BIOS在0:4 * 0x41(中断号0x41对应内存)处存了 
! 存储第1个硬盘参数信息的内存地址。
    mov ax,#0x0000
    mov ds,ax
    lds si,[4*0x41]
    mov ax,#INITSEG
    mov es,ax
    mov di,#0x0080
    mov cx,#0x10
    rep
    movsb

! Get hd1 data
! 获取第2个硬盘的参数信息存于[0x90090, 0x900a0)内存段中,
! 猜测：第1个硬盘参数信息地址与第2个硬盘参数信息地址刚好相隔了20个字节,
! BIOS将硬盘信息存于其地址之后的16字节内存中。
    mov ax,#0x0000
    mov ds,ax
    lds si,[4*0x46]
    mov ax,#INITSEG
    mov es,ax
    mov di,#0x0090
    mov cx,#0x10
    rep
    movsb

! Check that there IS a hd1 :-)
! 检查第2个硬盘是否存在,
! 若不存在则将获取到的参数信息[0x90090, 0x900a0)清0。
    mov ax,#0x01500
    mov dl,#0x81
    int 0x13
    jc  no_disk1
    cmp ah,#3
    je  is_disk1
no_disk1:
    mov ax,#INITSEG
    mov es,ax
    mov di,#0x0090
    mov cx,#0x10
    mov ax,#0x00
    rep
    stosb
is_disk1:


! 关于linux 0.11 CPU参考手册。
! ---------------------------
! linux 0.11约在1991年12月改版诞生。
! Intel 80486于1989年改版诞生, 1993年改版诞生Intel Pentium。
! 所以linux 0.11 CPU参考手册应尽可能参考80486,
! 其次是80386, 80286。
! 不过没有搜到80486参考手册, 就参考80386吧。
! 也可以参考Pentium及以上版本文档中的一些说辞。

! now we want to move to protected mode ...
! 为支持80x86CPU在保护模式下运行, 先做好以下准备。!
!
! BIOS初始化完成后, I/O硬件处于已启动状态,
! 为防止在设置IDT和初始化PIC后有中断放生, 
! 在操作系统初始化完成之前, 先禁止所有中断。
! 直到在main函数中做了足够初始化后, 才重新允许中断得哦。
    cli ! no interrupts allowed !

! first we move the system to it's rightful place
! 以段为单位(64Kb)将[0x10000, 0x90000)
! 内存段中的代码拷贝至[0x0, 0x80000)内存段。
! 
! bootsect.s从启动盘中只读取了192Kb的操作系统程序存于
! [0x10000, 0x40000)内存段中。
! 同时linux 0.11 给操作系统程序预留了512Kb内存,
! 此处以512Kb的容量对操作系统程序进行拷贝。
! 如此, 待操作系统扩展到512Kb之后, 也不用来改这里的代码了,
! 只需修改一下读取操作系统程序的大小即可。
! 
! 在bootsect.s中有提到,
! [0x0, 0x400)内存段存储了BIOS中断向量表等信息,
! 由于linux 0.11在进入保护模式后不会再使用BIOS中断调用,
! 所以将该内存段可用作存储操作系统程序了。
! 除了能节约内存外, 从0x0开始存储操作系统程序还有一些其他
! 的小小的好处, 比如段基址和偏移地址相等。
    mov ax,#0x0000
    cld ! 'direction'=0, movs moves forward
do_move:
    mov es,ax   ! destination segment
    add ax,#0x1000
    cmp ax,#0x9000
    jz  end_move
    mov ds,ax   ! source segment
    sub di,di
    sub si,si
    mov cx,#0x8000
    rep
    movsw
    jmp do_move

!
! then we load the segment descriptors
end_move:
!
! 之前都是在引用其他内存段中的数据, 
! 如存储系统参数于0x9000段中, 
! 读取0x1000段中操作系统程序到0x0段中。
! 
! 由于现在要访问setup.s程序所在的0x9020内存段,
! 所以需将数据段寄存器ds赋值为0x9020。
    mov ax,#SETUPSEG    ! right, forgot this at first. didn't work :-)
    mov ds,ax
!
! 为保护模式准备中断描述符表(Interrupt Descriptor Table)和
! 全局描述符表(Global Descriptor Table)。
! 
! 将IDT和GDT的基址和长度分别加载给IDTR和GDTR寄存器。
! GDTR/IDTR寄存器位格式。
! |47                   16|15                         0|
! ------------------------------------------------------
! | descriptor table addr | length of descriptor table |
! ------------------------------------------------------
! bit[15..0]: 存储IDT或GDT长度(字节); 
! bit[47..16]: 存储IDT或GDT的内存地址。
!
! 相继看看idt_48和gdt_48处的6字节信息。
    lidt    idt_48  ! load idt with 0,0
    lgdt    gdt_48  ! load gdt with whatever appropriate

! that was painless, now we enable A20
! 设置IDT和GDT后, 为访问扩展内存, 再来开启第21根地址线吧。
    call empty_8042
    mov  al,#0xD1 ! command write
! 写命令(0xD1)到输入寄存器,
! 0xD1是一个命令, 表示即将写数据让8042输出到P2系列引脚,
! 该数据随后通过60h端口写入。
    out #0x64,al
    call empty_8042 ! 等待0xD1命令被8042读取。
    mov  al,#0xDF   ! A20 on
    out  #0x60,al   ! 往60h端口地址下发数据0xDF(P2-1输出高)。
    call empty_8042 ! 等待0xDF被8042读取输出给P2系列引脚。
! 此处应该是在失去BIOS中断调用后,
! 首次直接使用I/O指令编程的地方,
! 先粗略理解下I/O编程相关知识, 为阅读后续I/O程序铺下路吧。
! 
! [1] I/O端口地址。
! 从计算机硬件组成角度理解, CPU可直接访问的器件有:
! CPU内部寄存器; 内存; I/O接口芯片中端口(寄存器)。
! CPU与他们通过总线直接相连。
! 
! CPU通过内存地址寻址内存单元, 
! I/O端口地址与内存地址相似, CPU用I/O端口地址寻址I/O端口。
! 对CPU来说, 内存也算一种I/O器件, 可能是其比较核心速度也比其他I/O快,
! 硬件连接和通信方式也所有差异,
! 所以x86用内存地址空间和I/O端口地址空间将二者区分开。
!
! 一个I/O端口地址也被俗称为端口号, 80x86使用in/out系列指令引用端口号。
! 
! 现从《微型机（PC系列）接口控制教程》- 张载鸿
! 一书中将PC/AT机的I/O端口地址的分配摘抄如下,
! 以对I/O端口地址有个临场感受 —— 
! 像理解内存地址空间那样理解I/O端口地址空间吧。
! |I/O port addr   |I/O接口芯片                          |
! |================|=================================|
! |000 - 01FH      |DMA Controller 1                 |
! |0C0 - 0DFH      |DMA Controller 2                 |
! |080 - 09FH      |DMA Page Register                |
! |----------------|---------------------------------|
! |020 - 03FH      |Interrupt controller 1           |
! |0A0 - 0BFH      |Interrupt controller 2           |
! |----------------|---------------------------------|
! |040 - 05FH      |timer controller                 |
! |----------------|---------------------------------|
! |060 - 06FH      |Keyboard Controller              |
! |----------------|---------------------------------|
! |070 - 07FH      |RT / CMOS RAM                    |
! |----------------|---------------------------------|
! |070H            |NM1 mask register                |
! |----------------|---------------------------------|
! |0F0 - 0FFH      |Coprocessor                      |
! |================|=================================|
! |1F0 - 1FFH      |Drive Control Card               |
! |200 - 20FH      |Game Control Card                |
! |----------------|---------------------------------|
! |370 - 37FH      |Parallel Control Card 1          |
! |270 - 27FH      |Parallel Control Card 2          |
! |----------------|---------------------------------|
! |3F8 - 3FFH      |Serial Control Card 1            |
! |2F0 - 2FFH      |Serial Control Card 2            |
! |----------------|---------------------------------|
! |300 - 31FH      |Prototype Plug-in Board          |
! |----------------|---------------------------------|
! |3A0 - 3AFH      |Synchronized Communication Card 1|
! |380 - 38FH      |Synchronized Communication Card 2|
! |----------------|---------------------------------|
! |3B0 - 3BFH      |mono MDA                         |
! |3D0 - 3DFH      |color CGA                        |
! |3C0 - 3CFH      |color EGA/VGA                    |
! |----------------|---------------------------------|
! |3F0 - 3F7H      |Floppy Drive Control Card        |
! ----------------------------------------------------
! 
! [2] I/O接口芯片的编程模式。
! (在计算机上电BIOS运行后, I/O处于已启动状态, 能与CPU通信)
! I/O提供的一般编程模式: 拟定一系列命令和数据,
! 由CPU通过out指令往指定I/O端口下发拟定命令或数据, 让I/O执行。
!
! 粗略理解这些概念后, 看看地址线A20的选通过程。
! A20由键盘控制接口芯片8042的P2-1引脚输出高电平选通,
! 即编程目标为在不破坏其他引脚状态情况下,
! 往8042端口发送命令或数据让8042往P2-1输出高电平。
!
! CPU分配给键盘控制器端口地址空间为[60H, 6FH],
! 8042只用到了60H和64H两个端口地址寻址其内的端口。
! 
! 端口地址60H和64H呈现出来的编程功能。
! CPU执行"in al, port"指令即读端口, 
! port=60H表示读输出寄存器到al; 
! port=64H表示读状态寄存器到al。
! 
! CPU执行"out port, al"指令即写端口,
! port=60H表示写al(数据)到输入寄存器; 
! port=64H表示写al(命令)到输入寄存器。
!
! 8042键盘控制芯片信息见
! 《微型机（PC系列）接口控制教程》 - 张载鸿书本页码P157-P166。
! 8042芯片引脚分布-P158; 8042控制选通A20文字介绍-P160; 
! 端口和寄存器的介绍-P161_162; 0xD1命令-P165_166。
! (后续I/O程序阅读也会参考此书^_^)

! well, that went ok, I hope. Now we have to reprogram the interrupts :-(
! we put them right after the intel-reserved hardware interrupts, at
! int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
! messed this up with the original PC, and they haven't been able to
! rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
! which is used for the internal hardware interrupts as well. We just
! have to reprogram the 8259's, and it isn't fun.
! 初始化可编程中断控制芯片8259A, 为其中断向量[0x20, 0x2F]。
!
! 中断控制器1和2的端口地址空间分别为[20H,3FH]/[A0H, BFH]。
! 8059A-1和8059A-2通过将端口地址最低位用于寄存器的选择,
! 只分别使用了前两个端口地址(P64,PC/AT)。
!
! 在初始化8259A之前, 写端口表明设置初始化命令(ICW)寄存器组。
! 0x20和0xA0分别为8259A-1和8259A-2的端口地址,
! 端口地址最低位为0且0x11的bit[4]=1则表示
! 0x11为第一条初始化命令ICW1, ICW1 bit[0]=1表明需初始化命令ICW4, 
! bit[1](SNGL)=0表示8259A为多片, bit[3](LTIM)=0表示边沿触发中断。
    mov   al,#0x11      ! initialization sequence
    out   #0x20,al      ! send it to 8259A-1. 
    .word 0x00eb,0x00eb ! jmp $+2, jmp $+2, 延时
    out   #0xA0,al      ! and to 8259A-2.
    .word 0x00eb,0x00eb

! 0x21和0xA1分别为8259A-1和8259A-2的端口地址, 
! 端口地址最低位为1, 表明下发第2条初始化命令ICW2=0x20/0x28,
! 即8259A-1/2的起始中断号分别为0x20和0x28。
    mov al,#0x20    ! start of hardware int's (0x20)
    out #0x21,al
    .word 0x00eb,0x00eb
    mov al,#0x28    ! start of hardware int's 2 (0x28)
    out #0xA1,al
    .word 0x00eb,0x00eb

! 在下发ICW2初始化命令后且端口地址最低位为1, 
! 则表示下发ICW3初始化命令(ICW1的SNGL=0表示有多片中断控制器级联)
! ICW3=0x04表明主中断控制芯片8259A-1的IR2上连了一个从片,
! ICW3=0x02表明从中断控制芯片8259A-2的中断级为0x02(用于主选从)。
    mov al,#0x04    ! 8259-1 is master
    out #0x21,al
    .word   0x00eb,0x00eb
    mov al,#0x02    ! 8259-2 is slave
    out #0xA1,al
    .word   0x00eb,0x00eb

! ICW1 bit[0]=1且端口地址最低位为1且
! 下发数据高3位为0表明下发初始化命令ICW4,
! ICW4 bit[0]=1表示当前CPU为8086;
! bit[1](AEOI)=0表示8259A-1/2不自动结束中断,
! 即需中断程序给8259A发结束命令EOI才能清当前中断,...
    mov al,#0x01    ! 8086 mode for both
    out #0x21,al
    .word   0x00eb,0x00eb
    out #0xA1,al
    .word   0x00eb,0x00eb
 
! 按照初始化流程设置ICW初始化8259A后, 
! 再写端口表明操作OCW1-OCW3寄存器组。
! 端口地址最低位为1, 表示下发OCW1操作命令, 
! 即操作中断屏蔽寄存器IMR, 
! IMR bit[n]置1表示屏蔽引脚n上的中断请求, 置0表允许。
    mov al,#0xFF    ! mask off all interrupts for now
    out #0x21,al
    .word   0x00eb,0x00eb
    out #0xA1,al

! 8259A其他信息参考。
! 内部结构图及寄存器选择原理(P56_57);
! PC/AT两片8259A相连硬件图(P65, 图2.11); 
! 8259A初始化流程(P62, 图2.9);
! 8259A端口地址与初始化命令字ICW寄存器组的对应关系(P61_63);
! 何时表明是在操作OCW1-OCW3寄存器组(P68_69)。

! well, that certainly wasn't fun :-(. Hopefully it works, and we don't
! need no steenking BIOS anyway (except for the initial loading :-).
! The BIOS-routine wants lots of unnecessary data, and it's less
! "interesting" anyway. This is how REAL programmers do it.
!
! Well, now's the time to actually move into protected mode. To make
! things as simple as possible, we do no register set-up or anything,
! we let the gnu-compiled 32-bit programs do that. We just jump to
! absolute address 0x00000, in 32-bit protected mode.
! 使用lmsw指令设置CR0寄存器的PE(保护模式位bit[0])为1
! 开启80x86保护模式运行机制。
    mov  ax,#0x0001 ! protected mode (PE) bit
    lmsw ax         ! This is it!
! 
! 设置CR0 PE位后, 80x86 CPU进入保护模式运行机制,
! 当前程序特权级(CPL)被初始为0。
! 
! 开启80x86保护模式后, 
! 需立即运行一条跳转指令以刷新CPU预取指队列中的指令
! 同时, 由于进入了保护模式, CPU会丢弃实模式预取指令的相关信息。
! 
! 因为跳转指令紧跟在开启80x86CPU保护模式的lmsw指令之后,
! 所以跳转指令肯定已在预取指队列中, 
! 所以CPU能运行到lmsw之后的跳转指令而不用到内存中读取该指令。
! 而用跳转指令刷新预取指队列后, 
! 之后的程序指令皆以保护模式读取指令的方式读取。
!
! 以上描述就是以下跳转指令其中的一个作用,
! 它的另一个作用是跳转执行操作系统程序, 即head.s程序中的入口指令。
    jmpi 0,8    ! jmp offset 0 of segment 8 (cs)

! 该段间跳转指令是在80x86保护模式下运行的, 粗略理解其跳转机制。
! jmpi 0, 8指令执行后将改写cs=8, eip=0;
! 这将使得CPU跳转执行0x00000000:0处的程序指令。
! 
!欲理解这个结果可能需要积攒一些基础概念。
! (1) 相关概念。
! [1] 寄存器位扩展。
! 在保护模式下, 除段寄存器外, 
! 其余提供给编程使用的寄存器被扩展为32位, 
! 寄存器名在实模式寄存器名基础上添加前缀e。
! 如ip --> eip。
! 
! [2] 段寄存器。
! |15         0|
! -----------------------
! |16位可见部分|隐藏部分|
! -----------------------
! 编程指令只能访问段寄存器可见部分, 其隐藏部分由CPU隐式访问。
! 段选择器可见16位充当段描述符选择符, 隐藏部分被CPU用于加载段描述符的内容。
!
! [3] 段描述符选择符。
! |15              3| 2|  0|
! --------------------------
! |      index      |TI|RPL|
! --------------------------
! index[15..0]: 为段描述符在段描述符表中的偏移, 低3位[3..0]被CPU自动补0。
! TI(Table Indicator): TI=0, 选择GDT段描述符; TI=1, 选择LDT段描述符。
! RPL(Requestor Privalege Level): 请求特权级, 用于保护机制,
! 如, 在需满足RPL<=DPL时, CPU才允许访问段描述符描述的内存段。
! 
! CPU用段寄存器充当段描述符选择符。
!
! (2) CPU通过段描述符选择符和偏移地址计算出内存地址。
! jmpi 0, 8 指令执行后, cs=08, eip=0, 
! cs将被CPU作为选择段描述符的选择并选择到GDT[1], 
! CPU通过一系列检查(如GDT[1]中的P=1?TYPE?CPL<=DPL? etc.)后将将GDT[1]加载到
! cs的隐藏部分(若后续代码都是访问cs=8对应内存段时, 便可避免每次都从内存中访问GDT[1]),
! 遂根据cs隐藏部分中"内存段基址(base addr)"加上偏移地址eip(0)
! 计算得到cs:eip=8:0对应内存地址为0x0, 即跳转执行head.s入口指令。


! (1) setup.s运行结束, 尝试概括其主要功能。
! ---------------------------------------
! [1] 通过BIOS中断调用获取一些I/O相关信息存储在始于0x90000的内存段中, 
! 供操作系统程序使用。如实模式以外的内存大小、硬盘参数、显示相关信息。
! [2] 将操作系统程序拷贝到[0x0, 0x80000)内存段中。
! [3] 在[0x90200, 0x90a00)内存段中设置GDT并将其地址和限长加载到GDTR中。
! [4] 选通A20地址线以支持实模式以外内存的寻址。
! [5] 初始化中断控制器8259A, 为其分配中断向量[0x20, 0x2F]。
! [6] 设置CR0 PE位开启80x86保护模式, 
! 随即通过跳转指令跳转执行操作系统程序(head.s处)及 刷新CPU的预取指队列和预取指信息。
!
! (2) 粗略关注下80x86保护模式下的特权级, 特权级会影响机器指令的行为。
! DPL-描述符特权级, 段描述符中的字段, 代表所描述内存段的特权级。
! RPL-请求特权级, 段描述符选择符中的字段(段寄存器最低2位), 用于请求段描述符。
! CPL-当前程序特权级, 由CPU内部某寄存器记录。
! 通常, CPL和当前正运行的可执行代码段的DPL相等, 当转换到具不同DPL的段
! 时, CPL会随之改变。
!
! 在进行段转换时, CPU会自动评估各特权级是否合法, 不合法则抛出一个保护异常。
! 
! 数据段转换。
! 通过引用数据段寄存器(ds, es, fs, gs, ss)实现数据段转换,
! 转换条件需满足
! DPL >= CPL && DPL >= RPL即DPL >= max(CPL, DPL)
!
! 可执行段转换。
! 在80386手册中查阅以下2种可执行段转换的特权级检查。
! [1] 通过将段描述符选择符作为段间跳转指令(jmpi, call, retf, int, iret)
! 操作数实现转换到另一个可执行段中时, 当可执行段为非一致性代码段时需满足
! DPL == CPL
! 当可执行段为一致性代码段时, 需满足 DPL <= CPL,
! 在这种情况下, 从一个可执行段转换到一个 一致性可执行段(其段描述符置位C)时,
! CPL不发生改变。
!
! [2] 转换到中断处理程序所在段执行中断处理程序。
! CPU不允许中断发生时转换到特权级更低的可执行段中执行中断处理程序,
! 即需满足CPL >= DPL。
! 若中断处理程序的段描述符C置位(一致性可执行段), 则不存在以上限制。
! 
! (3) 附。
! [1] I/O编程参考书籍
! 《微型机（PC系列）接口控制教程》- 张载鸿。
!
! [2] 替代80486 CPU的参考手册
!《INTEL 80386 PROGRAMMER'S REFERENCE MANUAL》
!
! 其中信息不太全面或信息之间的链接不太紧密且可能有错误(如Figure 6-1),
! 但仍旧是一个不错的参考手册。也可结合
! 《Intel® 64 and IA-32 Architectures Software Developer’s Manual》
! 参考手册 参考, 但此版本文档所描述CPU的最低版本为Intel Pentium。
!
! 在阅读此类源码时, 硬件参考资料的重要性就体现出来了。
! 2019.05.25
! --------------------------------------------------


! This routine checks that the keyboard command queue is empty
! No timeout is used - if this hangs there is something wrong with
! the machine, and we probably couldn't proceed anyway.
! 此段子程序用作开启A20时调用,
! 其功能为等待8042输入寄存器为空。
empty_8042:
! jmp #2的机器码, 即跳转执行下一条语句, 用作延时。
    .word   0x00eb,0x00eb
! 读8042状态寄存器。
    in  al,#0x64    ! 8042 status port
! 状态寄存器bit[1]=1则表示输入寄存器满。
    test al,#2          ! is input buffer full?
    jnz  empty_8042     ! yes - loop
    ret

! GDT[0]为保留项, 需用0填充。
!
! GDT[1]为可执行段描述符。
! 0x9A: S=T=1,可执行段描述符;
! C=0, 非一致性可执行代码段, 更低特权级代码段不能访问本描述符所描述的内存段;
! R=1, 内存段可读可执行, A=0;
! P=1, 内存段存在;
! DPL=00, 内存段特权级最高。
! 0xC0 && 0x07FF: 所描述内存段为[0x0, 0x800000)。
!
! GDT[2])为数据段描述符。
! 0x92: S=1&&T=0, 段描述符为数据段描述符;
! 低特权级数据段不能访问本描述符所描述的内存段;
! E=0, 所描述内存段的内存地址偏移范围为[0x0, limit);
! W=1, 内存段可写, A=0;
! P=1, 内存段存在;
! DPL=00, 内存段特权级最高。
! 0xC0 && 0x07FF&&E=0: 所描述内存段为[0x0, 0x800000)。
gdt:
    .word 0,0,0,0 ! dummy
    .word 0x07FF  ! 8Mb - limit=2047 (2048*4096=8Mb)
    .word 0x0000  ! base address=0
    .word 0x9A00  ! code read/exec
    .word 0x00C0  ! granularity=4096, 386
    .word 0x07FF  ! 8Mb - limit=2047 (2048*4096=8Mb)
    .word 0x0000  ! base address=0
    .word 0x9200  ! data read/write
    .word 0x00C0  ! granularity=4096, 386
! 粗略理解80x86保护模式下的GDT。
! 
! CPU根据GDTR寄存器访问GDT。
! 虽然GDT可设置在任何空闲内存中, 
! 但需将GDT长度和内存地址赋给GDTR寄存器。
!
! GDT                    segment descriptor offset
! |------------------|    0
! |segment descriptor|
! |------------------|    1
! |segment descriptor|
! |------------------|   ...
! |       ...        |
! |------------------|   8191
! |segment descriptor|
! |------------------|
! GDT由段描述符组成, 每个描述符占8个字节。
! GDTR低16位限长65536字节, 即GDT最多能有8192个段描述符。
!
! GDT段描述符用于描述某内存段在保护模式下的访问约束/机制,
! 共有以下几种类型。
! [1] 可执行段 描述符。
! |31           |23              |15             |7         0
! |----------------------------------------------------------
! | base addr   | | | |A|  limit | |   | | TYPE  | base addr|
! | [31..24]    |G|D|0|V|[19..16]|P|DPL|S|T|     | [23..16] |
! |             | | | |L|        | |   |1|1|C|R|A|          |
! |---------------------------------------------------------|4
! |                              |                          |
! |      base addr[15..0]        |      limit[15..0]        |
! |                              |                          |
! -----------------------------------------------------------0
! S=T=1时, CPU将段描述符视为可执行段描述符。
! 粗略理解可执行段描述符TYPE剩余各位的含义。
! C(conforming): 可执行(代码)内存段的一个属性 - 一致性,
! C=0, 非一致性代码段, 允许相同或更高特权级的代码段访问;
! C=1, 一致性代码段, 低特权级的代码可访问该代码段,
! 低特权级代码段不因访问了一致性代码段而改变当前特权级。
!
! D(Default): 默认操作数 大小,
! D=0, 默认地址 为16位, 默认操作数 为16位或8位;
! D=1, 默认地址 为32位, 默认操作数 为32位或8位。
! 
! R(Readable): 段描述符所描述内存段是否可读,
! R=0, 内存段不可读只可执行;
! R=1, 内存段可读可执行。
! 
! [2] 数据段 描述符。
! |31           |23              |15             |7         0
! |----------------------------------------------------------
! | base addr   | | | |A|  limit | |   | | TYPE  | base addr|
! | [31..24]    |G|B|0|V|[19..16]|P|DPL|S|T|     | [23..16] |
! |             | | | |L|        | |   |1|0|E|W|A|          |
! |---------------------------------------------------------|4
! |                              |                          |
! |      base addr[15..0]        |      limit[15..0]        |
! |                              |                          |
! -----------------------------------------------------------0
! S=1&&T=0时, CPU将段描述符视为数据段描述符。
! 粗略理解数据段描述符TYPE剩余各位的含义。
! B(Big-bit), 标识维护栈内存的寄存器,
! B=0, 使用ss:sp维护栈内存(栈顶最大地址为0xffff);
! B=1, 使用ss:esp维护栈内存(栈顶最大地址为0xffffffff)。
! 
! E(Expand-direction), 内存段扩展方向,
! E=0, 段描述所描述内存段为[base addr, base addr + limit);
! E=1, 数据段描述符描述的内存段为[limit+1, 0xffff(B=0)或0xffffffff(B=1)]。
! 
! W(Writable), 标识所描述内存段是否可写,
! W=0, 不能写段描述符所描述的内存段;
! W=1, 允许写段描述符所描述的内存段。
!
! 代码和数据段描述符的A标志位。
! A(Access), 内存段访问标志, 初始置位0, 由CPU置位。
!
! [3] 系统段 描述符。
! |31           |23              |15             |7         0
! |----------------------------------------------------------
! | base addr   | | | | |  limit | |   | | TYPE  | base addr|
! | [31..24]    |G| |0| |[19..16]|P|DPL|S|       | [23..16] |
! |             | | | | |        | |   |0|       |          |
! |---------------------------------------------------------|4
! |                              |                          |
! |      base addr[15..0]        |      limit[15..0]        |
! |                              |                          |
! -----------------------------------------------------------0
! S=0时, CPU将段描述符视为系统段描述符。
! 系统段描述符的TYPE类型较多, 只列描述LDT和TSS描述符的TYPE值。
! TYPE=0010(0x2), 本段描述符描述LDT所在内存段;
! TYPE=1001(0x9), 本段描述符描述32位可用TSS描述符所在内存段。
! ...
!
! 三种段描述符中含义相同的字段。
! base addr[31..0]: 端描述符所描述内存段的起始地址。
! AVL: 供编程自定义使用。
! limit[19..0]: 段限长, 段限长与G有关。
!
! G(Granularity): 内存段颗粒度, 
! G=0, limit[19..0]值为段限长, 单位字节; 
! G=1, CPU会自动补12位1到limit的低12位中, 
! 即段限长=limit[19..0] << 12 + 0xfff, 单位字节。
!
! P(Segment-Preent), 所描述内存段是否存在,
! P=0, 段描述符无效, 引用P=0的段描述符时CPU会发出异常;
! P=1, 段描述符有效。
!
! DPL(Descriptor privilege Level):
! GDT段描述符特权级,
! DPL=00, 最高特权级(系统级),
! DPL=xx, ...,
! DPL=11, 最低特权级(用户级)。
! 
! 80x86在保护模式下, 
! CPU不再以实模式下的"段基址 << 16 + 偏移地址"方式计算内存地址,
! 而是用"段描述符选择符"和"偏移地址"计算内存地址。
! "段描述符选择符"用于选择具体的段描述符, 
! 根据段描述中的基址base addr字段加上偏移地址, 就可以得到一个内存地址。
! 
! 此处就先粗略了解这么多吧, 
! 待在保护模式下首次引用(本文件"jmpi	0,8"处)GDT段描述符时,
! 再进一步理解相关机制。


! IDT长度和内存地址, 暂不使用IDT, 长度和基址用0填充。
idt_48:
    .word 0   ! idt limit=0
    .word 0,0 ! idt base=0L

! 低16位为GDT长度 - 2048字节; 
! 高32位为GDT内存地址: 0x9 << 16 + 0x200 + gdt = 0x90200 + gdt,
! 即GDT在setup.s中的gdt标号处, 过去瞧瞧。
gdt_48:
    .word 0x800         ! gdt limit=2048, 256 GDT entries
    .word 512+gdt,0x9   ! gdt base = 0X9xxxx

.text
endtext:
.data
enddata:
.bss
endbss:
