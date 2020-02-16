/*
 * This file contains some defines for the AT-hd-controller.
 * Various sources. Check out some definitions (see comments with
 * a ques).
 */
#ifndef _HDREG_H
#define _HDREG_H

/* Hd controller regs. Ref: IBM AT Bios-listing */
/* 硬盘端口地址0x1f0-0x1f7,不同访问操作呈现不同功能,
 * 此处宏定义用作之后注释作用。*/
#define HD_DATA     0x1f0   /* 硬盘数据寄存器(扇区读/写) */
#define HD_ERROR    0x1f1   /* see err-bits */
#define HD_NSECTOR  0x1f2   /* nr of sectors to read/write */
#define HD_SECTOR   0x1f3   /* starting sector */
#define HD_LCYL     0x1f4   /* starting cylinder */
#define HD_HCYL     0x1f5   /* high byte of starting cyl */
#define HD_CURRENT  0x1f6   /* 101dhhhh , d=drive, hhhh=head */
#define HD_STATUS   0x1f7   /* see status-bits */
#define HD_PRECOMP HD_ERROR /* same io address, read=error, write=precomp */
#define HD_COMMAND HD_STATUS    /* same io address, read=status, write=cmd */

/* 写端口地址0x3f6-写硬盘控制寄存器位,其各位含义。
 * |7                                0
 * -----------------------------------
 * |**1*|**1*|**|**|***1**|**1*|**|**|
 * -----------------------------------
 * |禁止|禁止|未|未|多磁头|复位|未|未|
 * |重试|重读|用|用| 选择 |允许|用|用|
 * ----------------------------------- */
#define HD_CMD  0x3f6

/* Bits of HD_STATUS */
/* 读端口地址0x1f7-读硬盘主状态控制器,其各位含义。
 * ---------------------------------------------------
 * |控制器|驱动器|驱动器|寻道|请求| ECC  |收到| 命令 |
 * | 忙碌 | 就绪 | 故障 |结束|服务|检验错|索引|执行错|
 * |  =1  |  =1  |  =1  | =1 | =1 |  =1  | =1 |  =1  |
 * --------------------------------------------------- */
#define ERR_STAT    0x01
#define INDEX_STAT  0x02
#define ECC_STAT    0x04 /* Corrected error */
#define DRQ_STAT    0x08
#define SEEK_STAT   0x10
#define WRERR_STAT  0x20
#define READY_STAT  0x40
#define BUSY_STAT   0x80

/* Values for HD_COMMAND */
#define WIN_RESTORE  0x10 /* 驱动器重新校准 */
#define WIN_READ     0x20 /* 扇区读 */
#define WIN_WRITE    0x30 /* 扇区写 */
#define WIN_VERIFY   0x40 /* 扇区检验 */
#define WIN_FORMAT   0x50 /* 格式化磁道 */
#define WIN_INIT     0x60 /* ?? */
#define WIN_SEEK     0x70 /* 寻道 */
#define WIN_DIAGNOSE 0x90 /* 控制器诊断 */
#define WIN_SPECIFY  0x91 /* 建立驱动器参数 */

/* Bits for HD_ERROR */
/* 读0x1f1-读错误寄存器,其各位含义。
 *
 * 诊断模式下
 * 01h无错误;02h控制器错;03h扇区缓冲器错;04hECC部件错;05h控制处理器错;
 * 
 * 操作模式下
 * 01h数据标志丢失;02h磁道0错;04h命令放弃;10hID未找到;40hECC错误;80h坏扇区。*/
#define MARK_ERR    0x01    /* Bad address mark ? */
#define TRK0_ERR    0x02    /* couldn't find track 0 */
#define ABRT_ERR    0x04    /* ? */
#define ID_ERR      0x10    /* ? */
#define ECC_ERR     0x40    /* ? */
#define BBD_ERR     0x80    /* ? */

/* struct partition,
 * 硬盘分区表结构体类型,
 * 用于描述硬盘引导区中偏移[0x1BE, 0x1FD]的分区表信息。*/
struct partition {
    unsigned char boot_ind; /* 引导标志,0x80-从该分区开始引导 */
    unsigned char head;     /* 分区起始磁头号 */
    unsigned char sector;   /* 分区起始扇区号(bit[5..0]),bit[7..6]为柱面号高2位 */
    unsigned char cyl;      /* 分区起始柱面号低8位 */
    unsigned char sys_ind;  /* 分区类型字节;0x80-MINIX */
    unsigned char end_head; /* 分区结束磁头号 */
    unsigned char end_sector; /* 分区扇区结束号(bit[5..0]),bit[7..0]结束柱面号高2位 */
    unsigned char end_cyl;    /* 结束柱面号低8位 */
    unsigned int start_sect;  /* 基于分区的起始扇区号,从0开始 */
    unsigned int nr_sects;    /* 分区占用扇区数 */
};

#endif
