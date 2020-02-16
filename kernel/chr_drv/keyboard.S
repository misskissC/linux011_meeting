/*
 *  linux/kernel/keyboard.S
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * Thanks to Alfred Leung for US keyboard patches
 *  Wolfgang Thiel for German keyboard patches
 *  Marc Corsini for the French keyboard
 */
/* 感谢 Alfred Leung      为美式键盘相关程序的修正;
 *      Wolfgang Thiel  为德式键盘相关程序的修正;
 *      Marc Corsini    为法式键盘做的贡献。*/

#include <linux/config.h>

.text
.globl _keyboard_interrupt

/*
 * these are for the keyboard read functions
 */
size = 1024  /* must be a power of two ! And MUST be the same
                as in tty_io.c !!!! */
head = 4
tail = 8
proc_list = 12
buf = 16

mode:   .byte 0 /* caps, alt, ctrl and shift mode */
leds:   .byte 2 /* num-lock, caps, scroll-lock mode (nom-lock on) */
e0: .byte 0

/*
 *  con_int is the real interrupt routine that reads the
 *  keyboard scan-code and converts it into the appropriate
 *  ascii character(s).
 */
/* con_int 是读取键盘扫描码并将键盘扫描码转换为对应 asscii 字符(集)的中断C处理程序。*/

/* _keyboard_interrupt,
 * 键盘(8255A)中断处理入口程序。
 *
 * 键盘输入 -> 键盘控制器 -> PIC -> CPU调用IDT[21h]中处理程序
 * _keyboard_interrupt。_keyboard_interrupt从键盘控制器输出缓
 * 冲器中读取键盘输入码,并根据键盘输入码调用相应子程序将键盘码
 * 存入控制台终端读队列中,然后结束键盘中断并调用键盘中断C处理函
 * 数do_tty_interrupt将键盘码转换到控制台终端辅助队列中。当键盘
 * 码为0xe0或0xe1时直接结束键盘中断而将读队列中的键盘码转换到辅助队列中。*/
_keyboard_interrupt:
/* 在栈中备份键盘中断前程序所使用的寄存器 */
    pushl %eax
    pushl %ebx
    pushl %ecx
    pushl %edx
    push %ds
    push %es

/* 将内核数据段加载给数据段寄存器 */
    movl $0x10,%eax
    mov %ax,%ds
    mov %ax,%es
    xorl %al,%al    /* %eax is scan code */

/* 读键盘输出缓冲器,
 * 内容为0xe0时则提前跳转set_e0处处理;
 * 内容为0xe1时则提前跳转set_e1处处理。*/
    inb $0x60,%al
    cmpb $0xe0,%al
    je set_e0
    cmpb $0xe1,%al
    je set_e1

/* 若键盘输入为其他字符,
 * 则调用eax * 4 + key_table处函数处理。*/
    call key_table(,%eax,4)
    movb $0,e0 /* 恢复e0值 */

/* 通过8255A 61h端口对键盘进行复位再
 * 使能,以对收到键盘扫描码作出应答。*/
e0_e1:  inb $0x61,%al
    jmp 1f
1:  jmp 1f
1:  orb $0x80,%al
    jmp 1f
1:  jmp 1f
1:  outb %al,$0x61
    jmp 1f
1:  jmp 1f
1:  andb $0x7F,%al
    outb %al,$0x61
    movb $0x20,%al
    outb %al,$0x20 /* 向PIC发送EOI结束键盘中断 */

/* 调用键盘中断C处理函数do_tty_interrupt
 * 将键盘读队列中的字符转换到其辅助队列中。*/
    pushl $0
    call _do_tty_interrupt
    addl $4,%esp /* do_tty_interrrupt参数回收 */
    
    pop %es
    pop %ds
    popl %edx
    popl %ecx
    popl %ebx
    popl %eax
    iret
    
/* 键盘扫描码为0xe0和0xe1时,将
 * 该扫描码后跟随的字符数写在e0
 * 处,然后跳转e0_e1处继续执行。*/
set_e0: movb $1,e0
    jmp e0_e1
set_e1: movb $2,e0
    jmp e0_e1

/*
 * This routine fills the buffer with max 8 bytes, taken from
 * %ebx:%eax. (%edx is high). The bytes are written in the
 * order %al,%ah,%eal,%eah,%bl,%bh ... until %eax is zero.
 */
/* put_queue,
 * 将存在寄存器中的键盘输入写入控制台终端读队列中。*/
put_queue:
    pushl %ecx
    pushl %edx

    /* edx = table_list地址中的内容(直接寻址),即中端读队列首地址;
     * ecx = *((long *)(edx+head))即为终端读队列中的head成员值;
     * 
     * *((char *)(edx+buf+ecx))=al即read_q.buf[head]=al
     * 即将字符al写入终端读队列buf成员的head偏移处。*/
    movl _table_list,%edx   # read-queue for console
    movl head(%edx),%ecx
1: movb %al,buf(%edx,%ecx)

    /* read_q.head++;以循环队列方式使用buf */
    incl %ecx
    andl $size-1,%ecx

    /* 若read_q.head == read_q.tail(队列满)则向前跳转3f处 */
    cmpl tail(%edx),%ecx # buffer full - discard everything
    je 3f

    /* eax = (ebx << 32 + eax) >> 8,
     * 若eax=0则表明无字符则向前跳转到2f处 */
    shrdl $8,%ebx,%eax
    je 2f

    /* ebx = ebx >> 8,
     * 向后跳转1b标号处继续将字符保存在read_q.buf中 */
    shrl $8,%ebx 
    jmp 1b

    /* 更新read_q.head成员值,
     * ecx=read_q.pro_lsit,
     * 若等待reqd_q的进程不为空则将该进程的
     * state成员置为0即将该进程置为可运行状态。*/
2:  movl %ecx,head(%edx)
    movl proc_list(%edx),%ecx
    testl %ecx,%ecx
    je 3f
    movl $0,(%ecx)
    
3:  popl %edx
    popl %ecx
    ret

/* 处理各键盘码输入的子程序 */

/* ctrl && alt 按下键码 处理子程序 */
ctrl: movb $0x04,%al /* 左ctrl */
    jmp 1f
alt:  movb $0x10,%al /* 左alt */
1: cmpb $0,e0 /* 若e0 != 0则表明收到右ctrl或右alt */
    je 2f
    addb %al,%al /* 右ctrl或右alt */
2: orb %al,mode  /* 记录当前收到的ctrl或alt键是键盘左边的还是右边的 */
    ret

/* ctrl && alt松开键码 处理子程序 */
unctrl: movb $0x04,%al /* 左ctrl */
 jmp 1f
unalt: movb $0x10,%al  /* 左alt */
1: cmpb $0,e0 /* e0处被置位表右ctrl或右alt */
    je 2f
    addb %al,%al
2: notb %al /* ctrl或alt键被松开,复位mode相应位 */
    andb %al,mode
    ret

/* 左右 shift按下松开键码处理程序 */
lshift:
    orb $0x01,mode /* 置mode bit[0]标识左shift按下 */
    ret
unlshift:
    andb $0xfe,mode /* 左shift松开则复位mode bit[0] */
    ret
rshift:
    orb $0x02,mode /* 置mode bit[1]标识右shift按下 */
    ret
unrshift:
    andb $0xfd,mode /* 右shift松开则复位mode bit[1] */
    ret

/* CapsLock键码处理子程序 */
caps: testb $0x80,mode /* CapsLock是否被按下,若按下则向前跳转1f处 */
    jne 1f
    xorb $4,leds    /* CapsLock没有处于被按下状态则翻转leds bit[2]以记录led灯的亮灭 */
    xorb $0x40,mode /* 翻转设置mode bit[6]以记录CapsLock锁定与否 */
    orb $0x80,mode  /* 设置mode bit[7]以记录capslock处于按下状态 */
/* 根据leds处的标志位,设置led指示灯 */
set_leds:
    call kb_wait /* 等待键盘控制器输入缓冲器空闲 */
    movb $0xed,%al  /* set leds command */
    outb %al,$0x60
    call kb_wait
    movb leds,%al   /* 发送命令参数以设置led灯 */
    outb %al,$0x60
    ret
/* capslock键松开则复位mode bit[7] */
uncaps: andb $0x7f,mode
    ret

/* scroll键按下则翻转leds bit[0]并设置led灯状态 */
scroll:
    xorb $1,leds
    jmp set_leds
/* 小键盘中的num键被按下则翻转led bit[1]并设置led灯状态 */
num: xorb $2,leds
     jmp set_leds

/*
 *  curosr-key/numeric keypad cursor keys are handled here.
 *  checking for numeric keypad etc.
 */
/* 方向键以及小键盘按键处理子程序 */
cursor:
    /* 47h <= 数字键码 <= 53h */
    subb $0x47,%al
    jb 1f
    cmpb $12,%al  
    ja 1f

    jne cur2    /* check for ctrl-alt-del */
    testb $0x0c,mode /* del and ctrl? */
    je cur2
    testb $0x30,mode /* del and ctrl and alt:重启 */
    jne reboot
cur2: cmpb $0x01,e0 /* e0 forces cursor movement */
    je cur
    testb $0x02,leds    /* not num-lock forces cursor */
    je cur
    testb $0x03,mode    /* shift forces cursor */
    jne cur
    xorl %ebx,%ebx
    movb num_table(%eax),%al /* 取数字键码于al中 */
    jmp put_queue /* 将得到的数字键存入读终端对队列中 */
1:  ret

/* 光标移动&&插入&&删除按键处理子程序 */
cur: movb cur_table(%eax),%al /* 光标字符于al */
    cmpb $'9,%al 
    ja ok_cur
    movb $'~,%ah
ok_cur: shll $16,%eax
    movw $0x5b1b,%ax
    xorl %ebx,%ebx
    jmp put_queue /* 将eax中的2个字符存入终端读队列中 */

#if defined(KBD_FR)
num_table: /* 数字小键盘上数字键ASCII码 */
    .ascii "789 456 1230."
#else
num_table:
    .ascii "789 456 1230,"
#endif
cur_table: /* 小键盘上方向键 插入 删除键表征的移动功能的字符表 */
    .ascii "HA5 DGC YB623"

/*
 * this routine handles function keys
 */
/* 功能键处理子程序 */
func:
    pushl %eax
    pushl %ecx
    pushl %edx
    call _show_stat
    popl %edx
    popl %ecx
    popl %eax
    subb $0x3B,%al /* F1 */
    jb end_func
    cmpb $9,%al /* F1-F10 */
    jbe ok_func
    subb $18,%al
    cmpb $10,%al /* F11 */
    jb end_func
    cmpb $11,%al /* F12 */
    ja end_func
ok_func:
    cmpl $4,%ecx    /* check that there is enough room */
    jl end_func
    movl func_table(,%eax,4),%eax /* 取功能键对应序列 */
    xorl %ebx,%ebx
    jmp put_queue
end_func:
    ret

/*
 * function keys send F1:'esc [ [ A' F2:'esc [ [ B' etc.
 */
/* 功能键序列表,F1-F10:'esc [[ A-J' */
func_table:
    .long 0x415b5b1b,0x425b5b1b,0x435b5b1b,0x445b5b1b
    .long 0x455b5b1b,0x465b5b1b,0x475b5b1b,0x485b5b1b
    .long 0x495b5b1b,0x4a5b5b1b,0x4b5b5b1b,0x4c5b5b1b

/* ASSCII字符映射表 */
#if defined(KBD_FINNISH) /* 芬兰语键盘扫描码映射表 */
key_map:
    .byte 0,27 /* 按键扫描码0,1对应的ASCII */
    .ascii "1234567890+'" /* 2,3..0xd对应ASCII,...*/
    .byte 127,9
    .ascii "qwertyuiop}"
    .byte 0,13,0
    .ascii "asdfghjkl|{"
    .byte 0,0
    .ascii "'zxcvbnm,.-"
    .byte 0,'*,0,32		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte '-,0,0,0,'+	/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '<
    .fill 10,1,0

/* 同时按下shift按键的ASCII映射表 */
shift_map:
    .byte 0,27
    .ascii "!\"#$%&/()=?`"
    .byte 127,9
    .ascii "QWERTYUIOP]^"
    .byte 13,0
    .ascii "ASDFGHJKL\\["
    .byte 0,0
    .ascii "*ZXCVBNM;:_"
    .byte 0,'*,0,32		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte '-,0,0,0,'+	/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '>
    .fill 10,1,0

/* 同时按下alt的ASCII映射表 */
alt_map:
    .byte 0,0
    .ascii "\0@\0$\0\0{[]}\\\0"
    .byte 0,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte '~,13,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte 0,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte 0,0,0,0   /* 36-39 */
    .fill 16,1,0    /* 3A-49 */
    .byte 0,0,0,0,0 /* 4A-4E */
    .byte 0,0,0,0,0,0,0 /* 4F-55 */
    .byte '|
    .fill 10,1,0

#elif defined(KBD_US) /* 美式键盘扫描码同ASCCI映射表 */

key_map:
    .byte 0,27
    .ascii "1234567890-="
    .byte 127,9
    .ascii "qwertyuiop[]"
    .byte 13,0
    .ascii "asdfghjkl;'"
    .byte '`,0
    .ascii "\\zxcvbnm,./"
    .byte 0,'*,0,32		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte '-,0,0,0,'+	/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '<
    .fill 10,1,0


shift_map:
    .byte 0,27
    .ascii "!@#$%^&*()_+"
    .byte 127,9
    .ascii "QWERTYUIOP{}"
    .byte 13,0
    .ascii "ASDFGHJKL:\""
    .byte '~,0
    .ascii "|ZXCVBNM<>?"
    .byte 0,'*,0,32		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte '-,0,0,0,'+	/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '>
    .fill 10,1,0

alt_map:
    .byte 0,0
    .ascii "\0@\0$\0\0{[]}\\\0"
    .byte 0,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte '~,13,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte 0,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte 0,0,0,0		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte 0,0,0,0,0		/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '|
    .fill 10,1,0

#elif defined(KBD_GR) /* 德语键盘扫描码同ASCII映射表 */

key_map:
    .byte 0,27
    .ascii "1234567890\\'"
    .byte 127,9
    .ascii "qwertzuiop@+"
    .byte 13,0
    .ascii "asdfghjkl[]^"
    .byte 0,'#
    .ascii "yxcvbnm,.-"
    .byte 0,'*,0,32		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte '-,0,0,0,'+	/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '<
    .fill 10,1,0


shift_map:
    .byte 0,27
    .ascii "!\"#$%&/()=?`"
    .byte 127,9
    .ascii "QWERTZUIOP\\*"
    .byte 13,0
    .ascii "ASDFGHJKL{}~"
    .byte 0,''
    .ascii "YXCVBNM;:_"
    .byte 0,'*,0,32		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte '-,0,0,0,'+	/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '>
    .fill 10,1,0

alt_map:
    .byte 0,0
    .ascii "\0@\0$\0\0{[]}\\\0"
    .byte 0,0
    .byte '@,0,0,0,0,0,0,0,0,0,0
    .byte '~,13,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte 0,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte 0,0,0,0		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte 0,0,0,0,0		/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '|
    .fill 10,1,0


#elif defined(KBD_FR) /* 法语键盘...*/

key_map:
    .byte 0,27
    .ascii "&{\"'(-}_/@)="
    .byte 127,9
    .ascii "azertyuiop^$"
    .byte 13,0
    .ascii "qsdfghjklm|"
    .byte '`,0,42		/* coin sup gauche, don't know, [*|mu] */
    .ascii "wxcvbn,;:!"
    .byte 0,'*,0,32		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte '-,0,0,0,'+	/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '<
    .fill 10,1,0

shift_map:
    .byte 0,27
    .ascii "1234567890]+"
    .byte 127,9
    .ascii "AZERTYUIOP<>"
    .byte 13,0
    .ascii "QSDFGHJKLM%"
    .byte '~,0,'#
    .ascii "WXCVBN?./\\"
    .byte 0,'*,0,32		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte '-,0,0,0,'+	/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '>
    .fill 10,1,0

alt_map:
    .byte 0,0
    .ascii "\0~#{[|`\\^@]}"
    .byte 0,0
    .byte '@,0,0,0,0,0,0,0,0,0,0
    .byte '~,13,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte 0,0
    .byte 0,0,0,0,0,0,0,0,0,0,0
    .byte 0,0,0,0		/* 36-39 */
    .fill 16,1,0		/* 3A-49 */
    .byte 0,0,0,0,0		/* 4A-4E */
    .byte 0,0,0,0,0,0,0	/* 4F-55 */
    .byte '|
    .fill 10,1,0

#else
#error "KBD-type not defined"
#endif
/*
 * do_self handles "normal" keys, ie keys that don't change meaning
 * and which have just one character returns.
 */
/* do_self,
 * 接收普通按键的子程序。
 *
 * 根据mode中的位标志分别从alt_map或shift_map或key_map
 * 键盘码映射表中取相应按键扫描码的ASCII,再根据诸如caplock
 * 是否处于锁定状态而将字符转换为大写字符等得到最终字符,
 * 最后将其存入终端读队列的buf中。*/
do_self:
    lea alt_map,%ebx
    testb $0x20,mode /* alt-gr */
    jne 1f
    lea shift_map,%ebx
    testb $0x03,mode
    jne 1f
    lea key_map,%ebx
1: movb (%ebx,%eax),%al
    orb %al,%al
    je none
    testb $0x4c,mode /* ctrl or caps */
    je 2f
    cmpb $'a,%al
    jb 2f
    cmpb $'},%al
    ja 2f
    subb $32,%al
2: testb $0x0c,mode /* ctrl */
    je 3f
    cmpb $64,%al
    jb 3f
    cmpb $64+32,%al
    jae 3f
    subb $64,%al
3: testb $0x10,mode /* left alt */
    je 4f
    orb $0x80,%al
4: andl $0xff,%eax
    xorl %ebx,%ebx
    call put_queue
none: ret

/*
 * minus has a routine of it's own, as a 'E0h' before
 * the scan code for minus means that the numeric keypad
 * slash was pushed.
 */
/* 处理减号的子程序 */
minus: cmpb $1,e0
    jne do_self
    movl $'/,%eax
    xorl %ebx,%ebx
    jmp put_queue

/*
 * This table decides which routine to call when a scan-code has been
 * gotten. Most routines just call do_self, or none, depending if
 * they are make or break.
 */
/* 键盘码处理子程序跳转表。
 * 键盘码将作为跳转表中各子程序的索引。*/
key_table:
    .long none,do_self,do_self,do_self  /* 00-03 s0 esc 1 2 */
    .long do_self,do_self,do_self,do_self   /* 04-07 3 4 5 6 */
    .long do_self,do_self,do_self,do_self   /* 08-0B 7 8 9 0 */
    .long do_self,do_self,do_self,do_self   /* 0C-0F + ' bs tab */
    .long do_self,do_self,do_self,do_self   /* 10-13 q w e r */
    .long do_self,do_self,do_self,do_self   /* 14-17 t y u i */
    .long do_self,do_self,do_self,do_self   /* 18-1B o p } ^ */
    .long do_self,ctrl,do_self,do_self  /* 1C-1F enter ctrl a s */
    .long do_self,do_self,do_self,do_self   /* 20-23 d f g h */
    .long do_self,do_self,do_self,do_self   /* 24-27 j k l | */
    .long do_self,do_self,lshift,do_self    /* 28-2B { para lshift , */
    .long do_self,do_self,do_self,do_self   /* 2C-2F z x c v */
    .long do_self,do_self,do_self,do_self   /* 30-33 b n m , */
    .long do_self,minus,rshift,do_self  /* 34-37 . - rshift * */
    .long alt,do_self,caps,func /* 38-3B alt sp caps f1 */
    .long func,func,func,func   /* 3C-3F f2 f3 f4 f5 */
    .long func,func,func,func   /* 40-43 f6 f7 f8 f9 */
    .long func,num,scroll,cursor    /* 44-47 f10 num scr home */
    .long cursor,cursor,do_self,cursor  /* 48-4B up pgup - left */
    .long cursor,cursor,do_self,cursor  /* 4C-4F n5 right + end */
    .long cursor,cursor,cursor,cursor   /* 50-53 dn pgdn ins del */
    .long none,none,do_self,func    /* 54-57 sysreq ? < f11 */
    .long func,none,none,none   /* 58-5B f12 ? ? ? */
    .long none,none,none,none   /* 5C-5F ? ? ? ? */
    .long none,none,none,none   /* 60-63 ? ? ? ? */
    .long none,none,none,none   /* 64-67 ? ? ? ? */
    .long none,none,none,none   /* 68-6B ? ? ? ? */
    .long none,none,none,none   /* 6C-6F ? ? ? ? */
    .long none,none,none,none   /* 70-73 ? ? ? ? */
    .long none,none,none,none   /* 74-77 ? ? ? ? */
    .long none,none,none,none   /* 78-7B ? ? ? ? */
    .long none,none,none,none   /* 7C-7F ? ? ? ? */
    .long none,none,none,none   /* 80-83 ? br br br */
    .long none,none,none,none   /* 84-87 br br br br */
    .long none,none,none,none   /* 88-8B br br br br */
    .long none,none,none,none   /* 8C-8F br br br br */
    .long none,none,none,none   /* 90-93 br br br br */
    .long none,none,none,none   /* 94-97 br br br br */
    .long none,none,none,none   /* 98-9B br br br br */
    .long none,unctrl,none,none /* 9C-9F br unctrl br br */
    .long none,none,none,none   /* A0-A3 br br br br */
    .long none,none,none,none   /* A4-A7 br br br br */
    .long none,none,unlshift,none   /* A8-AB br br unlshift br */
    .long none,none,none,none   /* AC-AF br br br br */
    .long none,none,none,none   /* B0-B3 br br br br */
    .long none,none,unrshift,none   /* B4-B7 br br unrshift br */
    .long unalt,none,uncaps,none    /* B8-BB unalt br uncaps br */
    .long none,none,none,none   /* BC-BF br br br br */
    .long none,none,none,none   /* C0-C3 br br br br */
    .long none,none,none,none   /* C4-C7 br br br br */
    .long none,none,none,none   /* C8-CB br br br br */
    .long none,none,none,none   /* CC-CF br br br br */
    .long none,none,none,none   /* D0-D3 br br br br */
    .long none,none,none,none   /* D4-D7 br br br br */
    .long none,none,none,none   /* D8-DB br ? ? ? */
    .long none,none,none,none   /* DC-DF ? ? ? ? */
    .long none,none,none,none   /* E0-E3 e0 e1 ? ? */
    .long none,none,none,none   /* E4-E7 ? ? ? ? */
    .long none,none,none,none   /* E8-EB ? ? ? ? */
    .long none,none,none,none   /* EC-EF ? ? ? ? */
    .long none,none,none,none   /* F0-F3 ? ? ? ? */
    .long none,none,none,none   /* F4-F7 ? ? ? ? */
    .long none,none,none,none   /* F8-FB ? ? ? ? */
    .long none,none,none,none   /* FC-FF ? ? ? ? */

/*
 * kb_wait waits for the keyboard controller buffer to empty.
 * there is no timeout - if the buffer doesn't empty, we hang.
 */
/* 等待键盘控制器输入缓冲器为空 */
kb_wait:
    pushl %eax
1: inb $0x64,%al
    testb $0x02,%al
    jne 1b
    popl %eax
    ret
/*
 * This routine reboots the machine by asking the keyboard
 * controller to pulse the reset-line low.
 */
/* 在向键盘控制器复位线输出负脉冲重启系统之前,
 * 先在物理内存0x472即启动模式标志内存处写入
 * 0x1234(热启动-不进行内存检测等过程;0-冷启动)
 * 最后进入linux0.11的死机程序中以等待系统热启动。*/
reboot:
    call kb_wait
    movw $0x1234,0x472 /* don't do memory check */
    movb $0xfc,%al /* pulse reset and A20 low */
    outb %al,$0x64
die:    jmp die

/* 哈哈,在linux0.11字符设备驱动及访问请求管理程序中,
 * 比如keyboard.S
 * 就是属于主参考《linux0.11内核完全注释》的典型文件,
 * 这部分内容跟键盘紧密相关,感觉不太好找准资料。
 * 
 * 注: 一旦形成主参考《linux0.11内核完全注释》一书时,
 * 就有了一种潜在的依赖感+一种完全停不下来的感觉。下
 * 一步将粗略阅读linux0.11的进程管理程序,此文将恢复最
 * 初自我的风格,会停止参考本书,以免形成过强依赖而导致
 * 丧失自己的理解(请允许凡人出错^_^)。*/
