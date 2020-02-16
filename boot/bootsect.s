!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux
!
! SYSSIZE为从启动盘加载linux 0.11操作系统程序(指head.s及之后的C程序)的大小。!
! 该数值以x86实模式下的段(省略为0的最低4位)为单位, !
! 即SYSSIZE实际值为0x30000, 即为192Kb(0x30000 >> 10 ), !
! 对于linux 0.11来说, 192Kb比其操作系统程序实际尺寸还要大一些。!
SYSSIZE = 0x3000
!
! bootsect.s    (C) 1991 Linus Torvalds
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
!
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts. 
!
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.
!
! bootsect.s将被BIOS启动程序加载到[0x7c00,0x7e00)内存段中,bootsect.s开头部分
! 指令包含了将[0x7c00, 0x7e00)内存段内容拷贝到[0x90000, 0x90200)内存段并跳转
! 0x9000段继续执行后续指令。随着这些指令的执行,setup.s程序将会被拷贝到
! [0x90200, 0x90a00)内存段, system会被拷贝到[0x10000, 0x40000)。
!
! 注,当前的system最多只有8*65536字节(512Kb)。即使在将来,512Kb也应该够内核程序霍霍了,
! 尤其是当内核不包含minix文件系统时。此文想保持操作系统的简单性。
!
! 此文已可能地让启动程序(bootsect.s)简单,若出现读盘错误时即刻进入死循环中。此时只有
! 手动启动计算机。启动程序一次获取一整个扇区内容,如此以保证能尽快地将操作系统加载到指
! 定的空闲内存中。

! 位于BIOS启动盘(当时为软盘)引导区的bootsect.s共512字节。!
! 在计算机上电BIOS完成初始化执行后, BIOS会自动读取启动盘引导区的512字节内容到 !
! [0x07c00, 0x07e00)内存地址段对应的内存段中, 随即在检查到0x07dfe和0x07dff !
! 两个字节为0xAA55时便跳转执行0x07c00内存中的指令即执行bootsect.s程序。!
! 
! 随着bootsect.s的被执行,
! 他首先将[0x07c00, 0x07e00)内存段的内容复制到[0x90000, 0x90200)内存段中, !
! 然后跳转到0x9000段中执行bootsect.s在[0x07c00, 0x07e00)中未被执行的后续代码。!
! 
! 在0x9000段中执行bootsect.s主要完成 !
! [1] 将启动盘上的setup.s程序读取到[0x90200, 0x90a00)内存段中; !
! [2] 将启动盘上的操作系统程序拷贝到[0x10000, 0x40000)内存段中。!
!
! 虽然对于linux 0.11来说,
! bootsect.s最多从启动盘中读取0x30000(192Kb)操作系统程序到内存中,
! 但作者为操作系统程序预留了8*65536=2^19字节即512Kb内存空间, 待将来扩展。!
!
! bootsect.s想尽可能简单地从启动盘中读取操作系统程序到内存中, !
! 在读取过程中一旦出错, 那么bootsect.s将会进入重读或某个纯粹的死循环中, !
! 如此就只有重启电脑来化解这个尴尬局面了。!


! .globl为(as86的)汇编伪指令: 声明begtext等为全局标号。
! .text .data .bss 分别为(as86)汇编程序中标识 代码段 数据段 数据附加段的伪指令。
! 他们告知(as86/ld86)汇编编译和链接器,
! begtext和endtext标号分别为代码段的开始和结束处(地址), 
! begdata和enddata标号分别为数据段的开始和结束地址, 
! begbss和endbss标号分别为数据附加段的开始和结束地址。
! 当在程序中引用这些标号时, 链接器将会把他们转换为合适的地址值。
! 
! 虽然bootsect.s将程序指令和程序数据揉在了一块, 
! 也没有其他程序引用这些伪指令定义的标号, 但是依旧可以这样理解他们。
.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

! setup.s程序在启动盘中的扇区数。
SETUPLEN = 4    ! nr of setup-sectors
!
! 段基址, 省略低4位的0。
BOOTSEG  = 0x07c0   ! original address of boot-sector
INITSEG  = 0x9000   ! we move boot here - out of the way
SETUPSEG = 0x9020   ! setup starts here
SYSSEG   = 0x1000   ! system loaded at 0x10000 (65536).
ENDSEG   = SYSSEG + SYSSIZE ! where to stop loading

! ROOT_DEV: 0x000 - same type of floppy as boot.
!   0x301 - first partition on first drive etc
! 设置默认的根文件系统设备(分区), 它被存储在bootsect.s的508偏移处。
! linux 0.11用逻辑编号映射外设备及分区, 0x306对应第2个硬盘的第1个分区。
! 可了解设备驱动程序如kernel/blk_drv/hd.c(sys_setup)后再回头理解此处程序。
ROOT_DEV = 0x306

! 伪指令entry, 
! 告知汇编链接器start为bootsect.s程序的指令入口。
entry start
!
! 将[0x07c00, 0x07e00)内存段内容复制到[0x90000, 0x90200)内存段中。
! (cld) rep movw(应该是rep movsw?) 语句相当于 
! while (cx--) 
!    movw es:di, ds:si
!    si += 2
!    di += 2
start:
    mov ax,#BOOTSEG
    mov ds,ax
    mov ax,#INITSEG
    mov es,ax
    mov cx,#256
    sub si,si
    sub di,di
    rep
    movw
! 
! 完成引导程序的拷贝后, 跳转到新内存段中执行后续程序。
! jmpi offset, seg 实现段间跳转即使得 cs = seg, ip = offset。
! seg为段基址, offset为基于seg段的偏移。
!
! jmpi go, INITSEG 跳转执行0x9000:go处指令。
! 此处, go在0x7c00段和0x9000段中的偏移相同(所以才能正确跳转哦)。
    jmpi    go,INITSEG
!
! 将ds和es数据段寄存器赋值段基址0x9000, 这样才能正确访问0x9000段中的数据。
go: mov ax,cs
    mov ds,ax
    mov es,ax
! 将用于维护栈内存的ss:sp寄存器置为0x9000:0x9ff0, 
! sp=0x9ff0会给程序代码预留足够内存空间, 合理的栈操作不会覆盖内存中的程序代码。
! bootsect.s和setup.s在[0x90000, 0x90a00)内存段, 
! 后续读取的操作系统代码将临时存储在[0x10000, 0x40000)内存段中。
! put stack at 0x9ff00.
    mov ss,ax
    mov sp,#0xFF00  ! arbitrary value >>512

! load the setup-sectors directly after the bootblock.
! Note that 'es' is already set up.
! 在完成数据和堆栈寄存器的设置后, 
! 接下来读取位于启动盘(软盘)中[1..5]扇区的setup.s程序到[0x90200, 0x90a00)内存段,
! 若读取失败则重置软盘再读, 直到读取成功为止。
load_setup:
    mov dx,#0x0000      ! drive 0, head 0
    mov cx,#0x0002      ! sector 2, track 0
    mov bx,#0x0200      ! address = 512, in INITSEG
    mov ax,#0x0200+SETUPLEN ! service 2, nr of sectors
    int 0x13            ! read it
    jnc ok_load_setup   ! ok - continue
    mov dx,#0x0000
    mov ax,#0x0000      ! reset the diskette
    int 0x13
    j   load_setup
! 试查BIOS 13h中断调用手册。
! 入参。
! ah = 02h, 调用BIOS 13h中(子程序号为02h)的读磁盘子程序;
! al = 欲读扇区数, SETUPLEN=4;
! es:bx = 存储所读内容的内存首地址, 0x9000:200;
! dh = 磁头号, 0x00; dl = 驱动号, 0x00为软盘驱动;
! cx = 0x0002, 从柱面0的第2扇区开始读。
! |F|E|D|C|B|A|9|8|7|6|5-0|
! | | | | | | | | | |   `----- 欲读起始扇区
! | | | | | | | | `---------   欲读起始柱面高2位
! `------------------------    欲读起始柱面低8位
!
! 出参和 标志寄存器的置位。
! ah = 状态号(00h-无错误..., );
! al = 成功读取扇区数;
! 标志寄存器CF=0, 读取成功; CF=1, 读取失败。

! 读取setup.s成功后, 通过BIOS 13h获取软盘参数,
! 将磁道(柱面)扇区数存储在cs(0x9000):sectors处,
! 供bootsect.s读取操作系统程序使用。
ok_load_setup:
! Get disk drive parameters, specifically nr of sectors/track
    mov dl,#0x00
    mov ax,#0x0800  ! AH=8 is get drive parameters
    int 0x13
    mov ch,#0x00
    seg cs
    mov sectors,cx
    mov ax,#INITSEG
    mov es,ax

! Print some inane message
! 通过BIOS 10h中断调用显示 读取操作系统程序到内存的提示信息。
    mov ah,#0x03    ! read cursor pos
    xor bh,bh
    int 0x10

    mov cx,#24
    mov bx,#0x0007  ! page 0, attribute 7 (normal)
    mov bp,#msg1
    mov ax,#0x1301  ! write string, move cursor
    int 0x10

! ok, we've written the message, now
! we want to load the system (at 0x10000)
! 一切准备就绪, 
! 通过BIOS 13h读取操作系统程序到始于0x10000的内存段中, 共读0x30000字节。
! 读取成功后, 关闭软盘马达。
! 关于read_it —— 可略过从软盘读取操作系统程序的逻辑细节。
    mov  ax,#SYSSEG
    mov  es,ax   ! segment of 0x010000
    call read_it
    call kill_motor

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.
! 在成功读取操作系统到内存后, 接下来检查用哪个设备作为根文件系统。!
! 若root_dev处所存值不为0, !
! 则使用此处默认存储设备号(ROOT_DEV=0x306)对应的设备作为根文件系统设备。!
! 若root_dev处所存值为0, !
! 则根据启动盘软盘磁道上的扇区总数决定使用
! /dev/PS0(2, 28)还是/dev/at0 (2, 8)对应的设备作为根文件系统设备。!
! (待了解设备驱动程序和文件系统后再理解此处的/dev/PS0(2, 28)之类的吧)
    seg cs
    mov ax,root_dev
    cmp ax,#0
    jne root_defined
    seg cs
    mov bx,sectors
    mov ax,#0x0208      ! /dev/ps0 - 1.2Mb
    cmp bx,#15
    je  root_defined
    mov ax,#0x021c      ! /dev/PS0 - 1.44Mb
    cmp bx,#18
    je  root_defined
undef_root:
    jmp undef_root
root_defined:
    seg cs
    mov root_dev,ax

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:
! 所有的程序已被读取到指定内存段中, 
! 还为根文件系统设置了默认设备(号), 
! 现在跳转跳转执行cs:ip=0x9020:0处指令即跳转执行setup.s入口处指令吧。
    jmpi    0,SETUPSEG


! (1) bootsect.s到此运行结束, 概括其主要功能。
! -------------------------------------------
! [1] 将内存段[0x07c00, 0x07e00)内容拷贝到[0x90000, 0x90200)内存段,
! 随后跳转0x9000段中执行bootsect.s后续未被执行的程序。
! [2] bootsect.s后续程序将启动盘(软盘)中的setup.s程序到[0x90200, 0x90a00)内存段中。
! [3] 接着读取启动盘中的操作系统程序到[0x10000, 0x40000)内存段中。
! [4] 检查并根据检查结果设置根文件系统设备号供后续操作系统程序使用
! (bootsect.s 508偏移处默认设置了根文件设备号0x306)。
! [5] 最后跳转执行setup.s程序。
!
! (2) 粗略理解80x86计算机上电到bootsect.s(引导程序)被执行过程吧。
! 80x86计算机硬件上电后, CPU默认在实模式机制下运行, BIOS是CPU最先执行的程序。
!
! 简化理解BIOS: 完成I/O硬件和软件信息的初始化工作。
!
! BIOS I/O硬件初始化。
! BIOS完成I/O硬件初始化后, I/O硬件进入已启动状态, 
! 即进入能和CPU进行通信的状态, 后续CPU对各I/O的设置或访问正是基于此状态。
! 
! BIOS软件信息初始化。
! 在内存中[0x0, 0x00400)设置BIOS中断向量表以提供BIOS中断调用, 
! 同时提供一些I/O设备参数信息到内存。
! 随后BIOS调用其19h号中断程序读取启动盘启动区内容到[0x07c00, 0x07e00)内存段中, 
! 若启动区最后两字节为0xAA55则跳转执行0x7c00:0处指令。
! 
! bootsect.s经转换为机器码后被写入启动盘的启动区, 从而充当了启动区代码。
! 在BIOS界面中, 可设置启动盘或为软盘、硬盘等。
! 
! (3) 粗略了解bootsect.s执行前后的内存地址空间分布。
! BIOS完成初始化后, 加载启动区代码之前的内存地址空间大体分布。
! 0x00000|----------------------------------|
!        |            1KB RAM               |
!        | BIOS Interrupt vector table etc. |
! 0x003FF|==================================|----
!        |                                  |  ↑
!        |                                  |  |
!        |             639KB                | available
!        |         RAM addr space           |  |
!        |                                  |  |
!        |                                  |  ↓
! 0x9FFFF|==================================|----
!        |                                  |
!        |              128K                |
!        |    video card ram addr space     |
! 0xBFFFF|==================================|
!        |                                  |
!        |             256KB                |
!        |      BIOS ROM addr space         |
!        |                                  |
!        |                                  |
! 0xFFFFF|==================================|
! bootsect.s在访问内存或读取启动盘程序到内存中时,
! 是有参考该内存地址空间分布的。
! 如
! 由于后续还要使用BIOS中断调用程序, 所以内存地址空间
! [0x0, 0x400)暂不用作代码或数据的存储;
! 如
! [0xC0000, 0x100000)为各板卡ROM内存条的地址空间,
! 这段内存地址空间不支持写操作;
! 如
! [0x400, 0xa0000)为(各)RAM内存条的地址空间,
! 空闲部分可用于存储程序代码或数据。
!
! bootsect.s执行完毕后，内存分布大体如下。
! 0x00000|----------------------------------|
!        |           1KB RAM                |
!        | BIOS Interrupt vector table etc. |
! 0x003FF|==================================|
!        |             ...                  |
! 0x10000|----------------------------------|
!        |     OS routines(system)          |
! 0x40000|==================================|
!        |             ...                  |
! 0x90000|----------------------------------|← ss(0x9000)
!        |           bootsect.s             |
! 0x90200|==================================|← cs:ip(0x9020:0)
!        |             setup.s              |
! 0x90A00|==================================|
!        |               ...                |← sp(0xff00)
! 0x9FFFF|==================================|
!        |                                  |
!        |              128K                |
!        |    video card ram addr space     |
! 0xBFFFF|==================================|
!        |                                  |
!        |              256KB               |
!        |      BIOS ROM addr space         |
!        |                                  |
!        |                                  |
! 0xFFFFF|==================================|
!
! (4) 粗略理解80x86实模式运行机制合成内存地址的方式。
! 在80x86实模式下, 内存地址线只有前20根可用,
! 即内存地址空间范围为[0x0, 0x100000)。
! CPU供编程的寄存器只有16位,
! CPU使用"段基址 << 4 + 偏移地址"的方式合成一个20位内存地址
! 再传送到内存地址线上寻址物理内存单元。
! 
! 如
! 用ds存段基址0x07c0, 用si存偏移地址3,
! 则CPU用"段基址 << 4 + 偏移地址"方式
! 将ds:si=0x07c0:3合成的内存地址为 0x07c0 << 4 + 3 = 0x07c03。
! 即ds:si最终寻址到0x07c03的内存地址。
!
! 从段基址开始的64Kb(16位寄存器最大可偏移64Kb)内存被称作一个段。
!
! (5) 粗略理解一些指令在80x86实模式下的行为。
! [1] 段内跳转指令。
! 无条件或条件跳转指令, 如 jmp/j/je/jnc label
! 功能: 跳转执行label处指令。
! 跳转方式: ip += label到ip值(取当前指令后)的偏移。
!
! call sub_fun
! 功能: 跳转执行sub_fun处指令。
! 跳转方式: push ip, ip += sub_fun到ip值(取当前指令后)的偏移。
!
! ret - 跳转执行栈顶处指令, 常与call连用。
! 跳转方式: pop ip。
!
! 段内跳转跟段基址和偏移起始值无关,
! 段内跳转程序在内存任何位置都可正常运行。
! 
! [2] 段间跳转指令。
! jmpi offset, seg
! 功能: 跳转执行seg:offset处指令。
! 跳转方式: cs=seg, ip=offset。
! 
! 段间跳转跟段基址无关, 但需核算出正确的偏移地址才能正确跳转。
! 如本程序中的jmpi go, #INITSEG 指令, 
! 若go在当前程序中的偏移值为start_offset,
! 则在将当前程序加载到内存中时, 
! 需核算跳转执行go时段基址, 使得go基于该段基址的偏移也为start_offset.
! 
! 汇编链接器会根据各指令含义将各汇编指令的目标操作数转换为相应值。
! 如汇编链接器将j label中的label转换为label与当前ip(取当前指令后)的偏移值。
!
! [3] 显式栈操作指令。
! push ax
! 功能: 将ax压入栈顶。
! 压栈过程: sp -= 2, (sp) = ax;
!
! pop ax
! 功能: 将栈顶内容赋值给ax。
! 出栈过程: ax = (sp), sp += 2。
!
! 此处的(sp),
! (sp)为左值表示sp指向的内存地址接受赋值;(sp)作为右值表示sp指向内存中的内容。
! 
! (6) 补码相关理解。
! 对于k进制n位数, 两个相加刚好能溢出的正整数互为补码数。
! 如10进制3位数, 和为10000的两个数互补, 如7000和3000, 9999和1。
! 对于k进制n位数m, 其补码等于(k^n - m)。
!
! 编译器采用补码原理用2进制n位数的较大一半数表示负数。
! 编译器在程序中遇到负数时, 
! 用该负数绝对值的补码代替该负数存于内存或寄存器中。
! 求一个二进制数的补码比求其他进制数的补码都方便, 
! 将该数各位取反再加1即可得到, 
! 对于寄存器来说, 取反和加法属于基本操作。
! 
! 以16位二进制数示例, 列表对应一下吧。
! | 0  ... 32767 | -32768 ...  -1   | 有符号十进制数
! |--------------|-------------------
! | 0  ... 32767 | 32768  ... 65535 | 无符号十进制数
! |--------------|-------------------
! |0x0 ... 0x7fff| 0x8000 ... 0xffff| 二进制数
! |--------------|-------------------
! 
! (7) 跟bootsect.s内容相关的一些参考资料。
! [1] 80x86汇编指令含义的启蒙参考书籍
! 《汇编语言》- 王爽。
! 
! [2] BIOS中断调用参考手册。
! 找1453406200@qq.com邮箱发一份;
! 或从以下地址下载一份(此文上传, 但此平台默认下载时需5个积分)
! https://download.csdn.net/download/misskissc/10997523
!
! [3] 软盘介绍参考资料。
! 若欲细读
! 读取操作系统代码到[0x10000, 0x40000)内存的代码即read_it, 
! 除BIOS中断调用参考手册外, 可能还需要了解软盘组织数据的格式,
! 可参考《30天自制操作系统》- 川合秀实 第三天P47_49。
!
! 若需了解软盘诸如信息存储层面上的原理可参考计算机组成原理一类书籍。
!
! (8) 粗略理解启动盘中操作系统程序的制作过程。
!       编译器+链接器                       去掉编译链接信息并将其组织到启动盘上的工具
!             ↓                              ↓
! 源程序文件 ---> 编译链接信息+机器指令+数据--->机器指令+数据 到启动盘。
!
! (9) bootsect.s和setup.s使用as86汇编编译器和ld86链接器的主要原因可能是
! 当时用他们能更方便编译出16位可执行程序吧。
!
! (10) 关于AT&T和Intel汇编格式。
! bootsect.s和setup.s类似Intel汇编程序, 操作系统程序中所涉及到的汇编都是AT&T格式的。
! Intel汇编指令中, 源操作数在右边, 目的操作数在左边; 
! 而AT&T的目的操作数在右边, 源操作数在左边。
! 对于阅读汇编源程序来说, 这是两者最主要的区别。
! 如 sub ax, bx
! ax = ax - bx ! intel汇编
! bx = bx - ax ! AT&T汇编
!
! 2019.05.25
! ------------------------


! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:   es - starting address segment (normally 0x1000)
!
sread: .word 1+SETUPLEN ! sectors read of current track
head:  .word 0          ! current head
track: .word 0          ! current track

! reat_it及后续子程序通过BIOS 13h将启动盘上操作系统程序读取到[0x10000, 0x40000)内存段中。
! 在阅读read_it相关子程序前, 先在BIOS层面粗略了解下启动盘-软盘的相关参数吧。
! 以1.4Mb软盘为例, 它共有2个面, BIOS 13h用一个磁头号(0开始)对应一个面,
! 每个面有80个磁道/柱面, BIOS 13h用一个磁道号(0开始)对应一个磁道/柱面,
! 每个磁道有18个扇区, 1个扇区有512字节空间, BIOS 13h用一个扇区号(1开始)对应一个扇区。
! 即2 * 80 * 18 * 512 = 1.4Mb。
! 
! 通过在这些子程序中不断组织磁头、磁道、扇区等参数给BIOS 13h才将操作系统程序读到内存中。
! 从启动盘中读取操作系统的大体逻辑是: 
! for (track = 0; track < 80; ++track) 
!    for (head = 0; head < 2; ++head) 
!       while (未读完当前track和head下的扇区)
!           if ((未读扇区字节数 + 当前内存段已读字节数) < 内存段总字节数大小)
!               读 当前未读扇区内容到当前内存段
!           else 
!               读取部分或全部未读扇区内容到当前内存段让当前内存段被读满
!               if (读满0x30000字节)
!                   读取结束
!               else 
!                   往下一个内存段继续读取
! 反推一下, 当初将操作系统程序写入启动盘的时候也是按照这个逻辑写入的。
! 
! sread, head和track是读取操作系统程序的初始参数, 它们的含义分别为
! 当前磁道已读扇区数(bootsect.s的1个扇区和setup.s的4个扇区), 磁头号和磁道号。
! 
!
! reat_it, 
! 读取启动盘-软盘上的0x30000字节的操作系统程序到[0x10000, 0x40000)内存段中。
! BIOS读取磁盘的中断调用(int 13h)会将所读内容保存到es:bx指向的内存中。
! es以64Kb边界对齐(es << 4后低16位为0), 若初始值不满足该条件则进入死循环。
! bx为段内偏移地址, 初值为0。
read_it:
    mov ax,es
    test ax,#0x0fff
die: jne die    ! es must be at 64kB boundary
    xor bx,bx   ! bx is starting address within segment

! 当es值小于0x4000即未读满0x30000字节时则跳转ok1_read处继续读取,
! 否则返回-读取操作系统程序完成。
rp_read:
    mov ax,es
    cmp ax,#ENDSEG  ! have we loaded all yet?
    jb  ok1_read
    ret

ok1_read:
! 计算还应读取的扇区数。
! al = 未读扇区数 = (柱面扇区数 - 已读扇区数)
    seg cs
    mov ax,sectors
    sub ax,sread
! 计算未读扇区数对应字节数(左移9位即乘512)和已读扇区字节数之和是否超过64Kb
! (16位寄存器发生进位则表示超过0xffff, 若刚好超过0xffff时16位寄存器值为0),
! 若未超过则跳转ok2_read处读取, 若超过则计算当读满64Kb所需的扇区数。
    mov cx,ax
    shl cx,#9
    add cx,bx
    jnc ok2_read
    je ok2_read
! 这里利用了编译器采用补码表示数的原理,
! 16位负数(-bx)的补码为0x10000(64Kb) - bx, 即计算出还差多少字节满64Kb, 
! 将结果以扇区为单位换算并存储到al寄存器中。
    xor ax,ax
    sub ax,bx
    shr ax,#9
    
! 调用read_track从启动盘当前位置读取指定扇区数后,
! 检查是否已经读完当前磁道的所有扇区, 
! 若没有读完当前磁道的所有扇区则跳转ok3_read处继续读取扇区内容到下一内存段,
! 若已读取完毕则检查当前磁头号是否为1, 不为1则跳转ok4_read处读取磁头号为1面上
! 当前磁道上的所有扇区, 若磁头号已为1则增加磁道号已读取下一磁道上的扇区。
ok2_read:
    call read_track
    mov cx,ax      ! cx=刚刚在read_track中所读扇区数
    add ax,sread   ! ax += sread, 当前磁道和磁头下已读扇区数
    seg cs
    cmp ax,sectors
    jne ok3_read   ! 比较当前已读扇区总数和磁道扇区总数, 不等(不足)则跳转ok3_read
    mov ax,#1
    sub ax,head    ! 若当前磁头号不为1, 则跳转执行ok4_head处读取启动盘磁头号为1的一面
    jne ok4_read
    inc track      ! 若当前磁道正反面(磁头head=0/1)都已经读取完毕, 则读取下一磁道

! 重置磁头号,
! 若head原来的值为0, 则ax在ok2_read中保留的值为1,
! 若head原来的值为1, 则ax在ok2_read中的值已为0。
ok4_read:
    mov head,ax
    xor ax,ax

! 更新读取启动盘和内存段的相关参数后,
! 跳转rp_read继续读取启动盘。
ok3_read:
    mov sread,ax  ! 重置当前磁道和磁头下的已读扇区数(0或已读扇区总数)
    shl cx,#9
    add bx,cx     ! 更新当前内存段 段内偏移
    jnc rp_read   ! 若16位寄存器未发生进位, 则表示未读满当前段, 则跳转rp_read继续读取
    mov ax,es
    add ax,#0x1000
    mov es,ax     ! 若当前内存段已满则让es指向下一个内存段
    xor bx,bx     ! 段内偏移重置为0
    jmp rp_read   ! 跳转rp_read处往当前内段读取启动盘上的内容

! 从启动盘当前位置读取指定扇区数(由ok1_read计算并存储在al中)
read_track:
    push ax
    push bx
    push cx
    push dx
    mov dx,track
    mov cx,sread
    inc cx         ! 从cl[5..0]扇区号对应扇区开始读取
    mov ch,dl      ! cl[7..6] ch=磁道号
    mov dx,head
    mov dh,dl      ! 将磁头号赋给dh
    mov dl,#0      ! dl=驱动号, 0为软盘驱动号
    and dx,#0x0100 ! 软盘只有0/1两个磁头号, 确保dh的最大值为1
    mov ah,#2      ! ah=BIOS 13h功能号, 2h表示读操作
    int 0x13
    jc bad_rt
    pop dx
    pop cx
    pop bx
    pop ax
    ret
! 若读取失败则重置软盘后跳转到read_track处继续读取
bad_rt: mov ax,#0
    mov dx,#0
    int 0x13
    pop dx
    pop cx
    pop bx
    pop ax
    jmp read_track

/*
 * This procedure turns off the floppy drive motor, so
 * that we enter the kernel in a known state, and
 * don't have to worry about it later.
 */
kill_motor:
    push dx
    mov dx,#0x3f2
    mov al,#0
    outb
    pop dx
    ret

sectors:
    .word 0

msg1:
    .byte 13,10
    .ascii "Loading system ..."
    .byte 13,10,13,10

! org 告知汇编编译器在bootsect.s偏移508处的2字节
! 存储根文件设备root_dev, 以留出启动盘的最后两个字节。
.org 508
root_dev:
    .word ROOT_DEV

! 当启动盘最后两个字节为0xaa55时表明启动盘内容有效,
! BIOS读取引导程序到[0x7c00, 0x7e00)内存段后, 当启动盘有效时便跳转执行0x7c00。
boot_flag:
    .word 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:
