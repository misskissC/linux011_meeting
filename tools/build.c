/*
 *  linux/tools/build.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This file builds a disk-image from three different files:
 *
 * - bootsect: max 510 bytes of 8086 machine code, loads the rest
 * - setup: max 4 sectors of 8086 machine code, sets up system parm
 * - system: 80386 code for actual system
 *
 * It does some checking that all files are of the correct type, and
 * just writes the result to stdout, removing headers and padding to
 * the right amount. It also writes some system data to stderr.
 *
 * 本文件根据以下三个不同的文件构建linux0.11磁盘映像文件:
 * - bootsect:最多包含510字节8086机器码,用于加载后续两个文件内容
 * - setup:最多包含4个扇区8086机器码,用于设置系统启动所需参数
 * - system:80386机器码,真正的操作系统代码,前面两个文件是system的过度
 *
 * 根据以上三个文件制作磁盘映像文件时,本文件会检查这三个文件格式是否正
 * 确并将检查结果显示在标准输出上,然后将这三个文件中的编译链接头部信息
 * 移除并作适当填充(如setup不足4扇区时填充0补齐4扇区)。 若出错则将错误
 * 信息显示在标准输出上。
 */

/*
 * Changes by tytso to allow root device specification
 */

#include <stdio.h> /* fprintf */
#include <string.h>
#include <stdlib.h> /* contains exit */
#include <sys/types.h> /* unistd.h needs this */
#include <sys/stat.h>
#include <linux/fs.h>
#include <unistd.h> /* contains read/write */
#include <fcntl.h>

/* bootsect.s,setup.s符合MINIX OS上可执行文件格式,头部为32字节;
 * system由gcc编译链接而来,头部有1024字节。*/
#define MINIX_HEADER 32
#define GCC_HEADER 1024

/* 0x20000为system最大尺寸 */
#define SYS_SIZE 0x2000

/* 默认根文件系统设备(第2个硬盘第1分区)的主次设备号 */
#define DEFAULT_MAJOR_ROOT 3
#define DEFAULT_MINOR_ROOT 6

/* max nr of sectors of setup: don't change unless you also change
 * bootsect etc 
 *
 * setup最多所占的扇区数,在没有修改跟加载setup相关代码时勿修改该宏值 */
#define SETUP_SECTS 4

#define STRINGIFY(x) #x

/* die,
 * 往错误输出写字符串str并终止当前程序。*/
void die(char * str)
{
    fprintf(stderr,"%s\n",str);
    exit(1);
}

/* usage,
 * 提示build程序的用法。*/
void usage(void)
{
    die("Usage: build bootsect setup system [rootdev] [> image]");
}

/* build工具主程序,
 * build工具将bootsect setup system
 * 三个文件的编译链接头部去除后依次
 * 写入image文件中得到磁盘映像文件。
 * 
 * 将该磁盘映像文件写入计算机的启动
 * 设备中,计算机在开机后,BIOS将读取
 * 并执行启动设备启动区内容即磁盘映
 * 像中的前512字节内容。*/
int main(int argc, char ** argv)
{
    int i,c,id;
    char buf[1024];
    char major_root, minor_root;
    struct stat sb;

    /* 在MINIX OS上运行build可执行程序制作linux0.11磁盘映像时,
     * 其用法为
     * build bootsect setup system [rootdev] [> image]
     * [rootdev]是可选参数,代表根文件系统设备名。
     * build往标准输出写入的内容将被被重定向到image文件中。*/
    if ((argc != 4) && (argc != 5))
        usage();

    /* 命令行参数有5个时,标识指定根文件系统设备 */
    if (argc == 5) {
        /* 若根文件系统设备不为软盘则通过stat
         * 获取根文件系统设备主次设备号。*/
        if (strcmp(argv[4], "FLOPPY")) {
            if (stat(argv[4], &sb)) {
                perror(argv[4]);
                die("Couldn't stat root device.");
            }
            major_root = MAJOR(sb.st_rdev);
            minor_root = MINOR(sb.st_rdev);
        } else {
            /* 标识根文件系统就是当前磁盘映像 */
            major_root = 0;
            minor_root = 0;
        }
    /* 命令行只有3个参数则使用默认设备作为根文件系统 */
    } else {
        major_root = DEFAULT_MAJOR_ROOT;
        minor_root = DEFAULT_MINOR_ROOT;
    }
    fprintf(stderr, "Root device is (%d, %d)\n", major_root, minor_root);
    /* 根文件主设备号不为软盘,硬盘,磁盘映像则出错退出 */
    if ((major_root != 2) && (major_root != 3) &&
        (major_root != 0)) {
        fprintf(stderr, "Illegal root device (major = %d)\n",
            major_root);
        die("Bad root device --- major #");
    }

    /* 初始化buf内存块 */
    for (i=0;i<sizeof buf; i++) buf[i]=0;

    /* 打开bootsect机器码文件 */
    if ((id=open(argv[1],O_RDONLY,0))<0)
        die("Unable to open 'boot'");
    /* 读取32字节即编译链接器头部信息到buf中 */
    if (read(id,buf,MINIX_HEADER) != MINIX_HEADER)
        die("Unable to read header of 'boot'");
    /* 判断魔数值(0x0301),可执行文件标志(0x10),机器码类型(0x04)是否正确 */
    if (((long *) buf)[0]!=0x04100301)
        die("Non-Minix header of 'boot'");
    /* 判断bootsect头部大小是否为32字节 */
    if (((long *) buf)[1]!=MINIX_HEADER)
        die("Non-Minix header of 'boot'");
    /* 分别判断bootsetc数据段,附加段,程序入口,符号表长度是否为0;
     * 因为在源码bootsect.s中,这些字段都是为0的。*/
    if (((long *) buf)[3]!=0)
        die("Illegal data segment in 'boot'");
    if (((long *) buf)[4]!=0)
        die("Illegal bss in 'boot'");
    if (((long *) buf)[5] != 0)
        die("Non-Minix header of 'boot'");
    if (((long *) buf)[7] != 0)
        die("Illegal symbol table in 'boot'");

    /* 从bootsect中读取所有内容,判断bootsect内容
     * 是否为512字节以及最后两字节是否为0xAA55则。*/
    i=read(id,buf,sizeof buf);
    fprintf(stderr,"Boot sector %d bytes.\n",i);
    if (i != 512)
        die("Boot block must be exactly 512 bytes");
    if ((*(unsigned short *)(buf+510)) != 0xAA55)
        die("Boot block hasn't got boot flag (0xAA55)");
    /* 在bootsect 508和509两字节处分别写入跟文件系统设备主次设备 */
    buf[508] = (char) minor_root;
    buf[509] = (char) major_root;

    /* 将bootsect内容写入标准输出 */
    i=write(1,buf,512);
    if (i!=512)
        die("Write call failed");
    close (id);

    /* 同理,判断setup头部是否符合预期格式并去除头部内容后写到标准输出 */
    if ((id=open(argv[2],O_RDONLY,0))<0)
        die("Unable to open 'setup'");
    if (read(id,buf,MINIX_HEADER) != MINIX_HEADER)
        die("Unable to read header of 'setup'");
    if (((long *) buf)[0]!=0x04100301)
        die("Non-Minix header of 'setup'");
    if (((long *) buf)[1]!=MINIX_HEADER)
        die("Non-Minix header of 'setup'");
    if (((long *) buf)[3]!=0)
        die("Illegal data segment in 'setup'");
    if (((long *) buf)[4]!=0)
        die("Illegal bss in 'setup'");
    if (((long *) buf)[5] != 0)
        die("Non-Minix header of 'setup'");
    if (((long *) buf)[7] != 0)
        die("Illegal symbol table in 'setup'");
    /* 将setup可执行文件除去头部内容后写到标准输出 */
    for (i=0 ; (c=read(id,buf,sizeof buf))>0 ; i+=c )
        if (write(1,buf,c)!=c)
            die("Write call failed");
    close (id);

    /* 若setup可执行文件内容超过4扇区则提示并终止程序 */
    if (i > SETUP_SECTS*512)
        die("Setup exceeds " STRINGIFY(SETUP_SECTS)
            " sectors - rewrite build/boot/setup");
    /* 若setup小于4扇区则用0补齐4扇区 */
    fprintf(stderr,"Setup is %d bytes.\n",i);
    for (c=0 ; c<sizeof(buf) ; c++)
        buf[c] = '\0';
    while (i<SETUP_SECTS*512) {
        c = SETUP_SECTS*512-i;
        if (c > sizeof(buf))
            c = sizeof(buf);
        if (write(1,buf,c) != c)
            die("Write call failed");
        i += c;
    }

    /* 判断system可执行文件格式并除去头部内容后写到标准输出 */
    if ((id=open(argv[3],O_RDONLY,0))<0)
        die("Unable to open 'system'");
    if (read(id,buf,GCC_HEADER) != GCC_HEADER)
        die("Unable to read header of 'system'");
    if (((long *) buf)[5] != 0)
        die("Non-GCC header of 'system'");
    for (i=0 ; (c=read(id,buf,sizeof buf))>0 ; i+=c )
        if (write(1,buf,c)!=c)
            die("Write call failed");
    close(id);
        
    fprintf(stderr,"System is %d bytes.\n",i);
    if (i > SYS_SIZE*16)
        die("System is too big");
    return(0);

/* build将可执行文件bootsect,setup,system的头部信息去掉后, 将
 * 他们的内容依次写入标准输出,由于标准输出被重定向到image文件,
 * 所以image文件内容为
 * ---------------------------
 * |bootsect| setup | system |
 * ---------------------------
 * 0        0x200   0x800    0x20800
 * image就是所谓的磁盘映像文件了。
 * 可以跟包含文件系统的haribote磁盘映像做个简单对比,以进一步理
 * 解磁盘映像概念。*/
}
