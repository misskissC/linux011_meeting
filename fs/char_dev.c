/*
 *  linux/fs/char_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/types.h>

#include <linux/sched.h>
#include <linux/kernel.h>

#include <asm/segment.h>
#include <asm/io.h>

extern int tty_read(unsigned minor,char * buf,int count);
extern int tty_write(unsigned minor,char * buf,int count);

/* crw_ptr,
 * 字符设备读写函数指针类型。
 * 
 * rw参数携带读写标志, rw=READ表读操作, rw=WRITE表写操作;
 * minor表示一种字符设备的通道, 如串口1 串口2。
 * rw=READ时,buf内存段用于存储从字符设备中所读到的内容;
 * rw=WRITE时,buf内存段中的内容将会被写入到字符设备中;
 * count为buf的长度;
 * pos为字符设备读写起始偏移位置。*/
typedef (*crw_ptr)(int rw,unsigned minor,char * buf,int count,off_t * pos);

/* rw_ttyx,
 * 串口(设备)的读写函数。
 * minor=1对应终端,minor=1对应串口1,minor=2对应串口2。参数含义见 crw_ptr 类型。*/
static int rw_ttyx(int rw,unsigned minor,char * buf,int count,off_t * pos)
{
    /* 根据rw参数值读写串口,
     * tty_read和tty_write两个函数是字符设备驱动
     * 程序中的函数,稍作忍耐,届时再粗略阅读吧。*/
    return ((rw==READ)?tty_read(minor,buf,count):
        tty_write(minor,buf,count));
}

/* rw_tty,
 * 终端读写函数。
 * 参数含义见 crw_ptr 类型。*/
static int rw_tty(int rw,unsigned minor,char * buf,int count, off_t * pos)
{
    if (current->tty<0)
        return -EPERM;
    return rw_ttyx(rw,current->tty,buf,count,pos);
}

static int rw_ram(int rw,char * buf, int count, off_t *pos)
{
    return -EIO;
}

static int rw_mem(int rw,char * buf, int count, off_t * pos)
{
    return -EIO;
}

static int rw_kmem(int rw,char * buf, int count, off_t * pos)
{
    return -EIO;
}

/* rw_port,
 * 读写外设端口函数。
 * pos参数将承载端口起始地址。*/
static int rw_port(int rw,char * buf, int count, off_t * pos)
{
    int i=*pos;

    /* 读端口时,
     * 从端口地址空间[*pos, *pos+count)
     * 各读取一字节内容到buf内存段中。
     * 
     * 写端口时,
     * 将buf内存段count字节分别写到
     * [*pos, *pos+count)端口地址空间中。*/
    while (count-->0 && i<65536) {
        
        if (rw==READ)
            put_fs_byte(inb(i),buf++);
        else
            outb(get_fs_byte(buf++),i);
        i++;
    }
    i -= *pos;
    *pos += i;
    return i;
}

/* rw_memory,
 * 读写内存函数根据minor再细分的功能。*/
static int rw_memory(int rw, unsigned minor, char * buf, int count, off_t * pos)
{
    switch(minor) {
        case 0:
            return rw_ram(rw,buf,count,pos);
        case 1:
            return rw_mem(rw,buf,count,pos);
        case 2:
            return rw_kmem(rw,buf,count,pos);
        case 3:
            return (rw==READ)?0:count;  /* rw_null */
        case 4:
            return rw_port(rw,buf,count,pos);
        default:
            return -EIO;
    }
}

/* 编译阶段计算crw_ptr数组的元素个数 */
#define NRDEVS ((sizeof (crw_table))/(sizeof (crw_ptr)))

/* 字符设备读写函数静态数组,
 * 该数组将字符设备主设备号作为下标,
 * 即crw_table[nr]得到主设备号为nr的读写函数。
 *
 * 4是串口设备的主设备号;
 * 5是终端的主设备号。*/
static crw_ptr crw_table[]={
    NULL,       /* nodev */
    rw_memory,  /* /dev/mem etc */
    NULL,       /* /dev/fd */
    NULL,       /* /dev/hd */
    rw_ttyx,    /* /dev/ttyx */
    rw_tty,     /* /dev/tty */
    NULL,       /* /dev/lp */
    NULL};      /* unnamed pipes */

/* rw_char,
 * rw=读操作时,
 * 从dev对应设备pos偏移起读count字节内容到buf中,
 * rw=写操作时,
 * 则将buf中的count字节内容写到dev设备pos偏移处。*/
int rw_char(int rw,int dev, char * buf, int count, off_t * pos)
{
    crw_ptr call_addr;

    if (MAJOR(dev)>=NRDEVS)
        return -ENODEV;
    /* 根据主设备号映射字符设备对应的读写函数,
     * 并调用该读写函数进行相应的读写操作。*/
    if (!(call_addr=crw_table[MAJOR(dev)]))
        return -ENODEV;
    return call_addr(rw,MINOR(dev),buf,count,pos);
}
