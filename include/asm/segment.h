
/* 从用户态到内核态时,_system_call让fs寄存器
 * 保持了本进程用户态数据段描述符LDT[2]内容。
 *
 * 当然,fs也可以指向内核数据段,如 printk()。*/

/* 此处就暂不具体理解static inline, extern inline和inline了吧,
 * 他们在不同阶段或不同编译器下的含义可能会有所不同。*/

/* get_fs_byte,
 * 将用户空间内存地址addr中的1字节内容读出并返回。*/
extern inline unsigned char get_fs_byte(const char * addr)
{
    unsigned register char _v;

    __asm__ ("movb %%fs:%1,%0":"=r" (_v):"m" (*addr));
    return _v;
}

/* get_fs_word,
 * 将用户空间内存地址addr中的sizeof(short)字节内容读出并返回。*/
extern inline unsigned short get_fs_word(const unsigned short *addr)
{
    unsigned short _v;

    __asm__ ("movw %%fs:%1,%0":"=r" (_v):"m" (*addr));
    return _v;
}

/* get_fs_long,
 * 将用户空间内存地址addr中的sizeof(long)字节内容读出并返回。*/
extern inline unsigned long get_fs_long(const unsigned long *addr)
{
    unsigned long _v;

    __asm__ ("movl %%fs:%1,%0":"=r" (_v):"m" (*addr)); \
    return _v;
}

/* put_fs_byte,
 * 将1字节val拷贝到用户内存地址addr处。*/
extern inline void put_fs_byte(char val,char *addr)
{
__asm__ ("movb %0,%%fs:%1"::"r" (val),"m" (*addr));
}

/* put_fs_word,
 * 将sizeof(short)字节val拷贝到用户内存地址addr处。*/
extern inline void put_fs_word(short val,short * addr)
{
__asm__ ("movw %0,%%fs:%1"::"r" (val),"m" (*addr));
}
 
/* put_fs_long,
 * 将sizeof(long)字节val拷贝到用户内存地址addr处。*/
extern inline void put_fs_long(unsigned long val,unsigned long * addr)
{
__asm__ ("movl %0,%%fs:%1"::"r" (val),"m" (*addr));
}

/*
 * Someone who knows GNU asm better than I should double check the followig.
 * It seems to work, but I don't know if I'm doing something subtly wrong.
 * --- TYT, 11/24/91
 * [ nothing wrong here, Linus ]
 */
/* 如果有人比我更了解GNU的话就好好检查下以下代码吧,他们运行起来
 * 似乎没问题,但我并不确定以下代码是否包含了一些微妙的错误。^_^*/

/* get_fs,
 * 获取fs寄存器值并返回。*/
extern inline unsigned long get_fs() 
{
    unsigned short _v;
    __asm__("mov %%fs,%%ax":"=a" (_v):);
    return _v;
}

/* get_ds,
 * 获取ds寄存器值并返回。*/
extern inline unsigned long get_ds() 
{
    unsigned short _v;
    __asm__("mov %%ds,%%ax":"=a" (_v):);
    return _v;
}

/* set_fs,
 * 设置fs寄存器的值为val。*/
extern inline void set_fs(unsigned long val)
{
    __asm__("mov %0,%%fs"::"a" ((unsigned short) val));
}

