/*
 * This file has definitions for some important file table
 * structures etc.
 */

#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 * 
 * 以下是主设备号和各类型设备的对应关系。
 * 
 * 0 - unused (nodev) 保留未用
 * 1 - /dev/mem       内存
 * 2 - /dev/fd        软盘
 * 3 - /dev/hd        硬盘
 * 4 - /dev/ttyx      串口
 * 5 - /dev/tty       终端
 * 6 - /dev/lp        打印
 * 7 - unnamed pipes 未命名管道
 */

/* 判断主设备号x对应设备是否可定位寻找(lseek) */
#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3)

/* 读;写;预读;预写标志 */
#define READ 0
#define WRITE 1
#define READA 2  /* read-ahead - don't pause */
#define WRITEA 3 /* "write-ahead" - silly, but somewhat useful */

void buffer_init(long buffer_end);

/* MAJOR(a),获取设备分区号中的主设备号;
 * MINOR(a),获取设备分区号中的次设备号。*/
#define MAJOR(a) (((unsigned)(a))>>8)
#define MINOR(a) ((a)&0xff)

/* 命令最大长度;根i节点号 */
#define NAME_LEN 14
#define ROOT_INO 1

/* i节点位图块指针数组大小;
 * 逻辑块位图块指针数组大小;
 * MINIX1.0文件系统类型魔数。*/
#define I_MAP_SLOTS 8
#define Z_MAP_SLOTS 8
#define SUPER_MAGIC 0x137F

/* 单进程可打开文件最大数;
 * i节点在内存中缓存最大个数;
 * 所有进程同时可打开文件最大个数;
 * */
#define NR_OPEN 20  /* 单进程可打开文件最大数 */
#define NR_INODE 32 /* i节点在内存中同时能缓存的最大个数 */
#define NR_FILE 64  /* 系统可同时打开文件的最大个数 */
#define NR_SUPER 8  /* 超级块在内存中同时能缓存的最大个数 */
#define NR_HASH 307 /* 缓冲区块全局hash数组元素个数 */
#define NR_BUFFERS nr_buffers /* 缓冲区块buffer数 */
#define BLOCK_SIZE 1024       /* 缓冲区块大小, 1024字节即1Kb */
#define BLOCK_SIZE_BITS 10    /* 缓冲区块大小对应的bit位数 */
#ifndef NULL                  /* 若没有定义NULL,则定义NULL为无类型的地址0 */
#define NULL ((void *) 0)
#endif

/* 每个逻辑块中包含的i节点数;每个逻辑块中包含的目录项数 */
#define INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct d_inode)))
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct dir_entry)))

/* 管道i节点管理的是一个缓冲区内存块,
 * i节点的i_zone[0]指向管道数据尾,
 * i节点的i_zone[1]指向管道数据头;
 * PIPE_SIZE计算管道数据长度;
 * PIPE_EMPTY判断管道数据为空;
 * PIPE_FULL判断管道数据已经占满了整个缓冲区块;
 * INC_PIPE以循环队列方式使用管道缓冲区块,增加管道头指针。*/
#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode)-PIPE_TAIL(inode))&(PAGE_SIZE-1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode)==PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode)==(PAGE_SIZE-1))
#define INC_PIPE(head) \
__asm__("incl %0\n\tandl $4095,%0"::"m" (head))

/* 缓冲区块? 似乎没有在程序中使用 */
typedef char buffer_block[BLOCK_SIZE];

/* struct buffer_head,
 * 用于管理缓冲区块的节点的结构体类型。*/
struct buffer_head {
    char * b_data; /* pointer to data block (1024 bytes) */
    unsigned long b_blocknr;  /* block number */
    unsigned short b_dev;     /* device (0 = free) */
    unsigned char b_uptodate; /* b_data所指缓冲区块中的数据读有效 */
    unsigned char b_dirt;     /* 0-clean,1-dirty */
    unsigned char b_count;    /* users using this block */
    unsigned char b_lock;     /* 0 - ok, 1 -locked */
    struct task_struct * b_wait; /* 用于各进程互斥访问缓冲区块 */
    struct buffer_head * b_prev; /* 指向与当前节点具相同hash值的上一节点 */
    struct buffer_head * b_next; /* 指向与当前节点具相同hash值的下一节点 */
    struct buffer_head * b_prev_free; /* 指向链表中上一节点 */
    struct buffer_head * b_next_free; /* 指向链表中下一节点 */
};

/* struct d_inode,
 * 磁盘中i节点结构体类型。
 * 其中的数据成员含义同
 * struct m_inode类型中磁盘部分数据成员的含义。*/
struct d_inode {
    unsigned short i_mode;
    unsigned short i_uid;
    unsigned long i_size;
    unsigned long i_time;
    unsigned char i_gid;
    unsigned char i_nlinks;
    unsigned short i_zone[9];
};

/* struct m_inode,
 * i节点结构体类型。
 * 
 * 从'名词概念'角度看,i节点结构体类型可描述3类东西。
 * [1] 文件(如包含'hello world'程序的hw.c);
 * [2] 目录(如包含hw.c文件的目录/home/);
 * [3] 管道(用于进程通信的缓冲区块)。
 *
 * 统称[1-3]为'文件',在涉及具体类型时再分文件或目录或管道。*/
struct m_inode {
/* 前面一部分数据成员是磁盘i节点结构体在内存中的缓存,
 * 当此部分在内存中被修改后, 将会被同步到磁盘中。*/
 
    /* 记录i节点对应文件的属性,各位含义如下。
     * bit[0..8], 文件的访问权限,
     * bit[0..2], 其他用户对此文件的读,写,执行(rwx)权限;
     * bit[3..5], 组员rwx权限;
     * bit[6..8], 宿主rwx权限。
     * 
     * bit[9..11],
     * (001)b-执行该文件时将进程有效用户id设置为其用户宿主id,
     * (010)b-执行该文件时将进程有效组id设置为其组id,
     * (100)b-对于目录,删除受限。
     * 
     * bit[12..15], 文件类型,
     * 001-FIFO文件;002-字符设备文件;
     * 004-目录;006-块设备文件; 010-常规文件。见const.h */
    unsigned short i_mode;

    /* 文件用户id */
    unsigned short i_uid;

    /* 文件大小; 本i节点为管道节点时,
     * 该数据成员保存管道缓冲区块的内存页首地址。*/
    unsigned long i_size;

    /* 文件最后被修改时间,
     * 为自1970.1.1 0:0:0到文件被修改时的秒数。*/
    unsigned long i_mtime;

    /* 文件组id */
    unsigned char i_gid;

    /* 文件链接数,由目录项指向本i节点的数量 */
    unsigned char i_nlinks;

    /* 对于管道。
     * 管道i节点只用到了i_zone[0..1],
     * i_zone[0]值为数据尾在管道缓冲区块中的偏移,
     * i_zone[1]值为数据头在管道缓冲区块中的偏移,
     * i_size保存了管道缓冲区块的首地址。
     *
     * 对于其余文件。
     * i_zone[0..6]分别存储了7个数据逻辑块的块号,
     * (对于块设备文件,z_none[0]存了设备号);
     * i_zone[7]用于存储一级间接逻辑块号——逻辑块
     * 号i_zone[7]对应的逻辑块中存储了512个逻辑块号。
     * 这512个逻辑块号对应的逻辑块用于存储文件数据。
     * i_zone[8]用于存储二级间接逻辑块号——逻辑块号
     * i_zone[8]对应逻辑块存储了512个逻辑块号,这512
     * 个逻辑块号对应逻辑块又分别存储了512个逻辑块
     * 号,这512*512个逻辑块号对应的逻辑块用于文件存储数据。
     * 
     * 即一个文件i节点最大可以拥有7 + 512 + 512 * 512个逻辑块,
     * linux0.11一个逻辑块为1Kb,即linux0.11一个文件的最大尺寸为
     * 519Kb + 256Mb。*/
    unsigned short i_zone[9];

/* these are in memory also */
/* 以下数据成员只存在于内存中, 他们不会被同步到磁盘中。
 * 这些数据成员用于磁盘上的i节点是否已被修改,实现多进程的互斥访问文件等。*/

    /* 用于多进程间对本i节点或对应文件的互斥访问,
     * <sleep_on + wake_up>。*/
    struct task_struct * i_wait;

    /* i节点最后被访问时间 */
    unsigned long i_atime;

    /* i节点最后被修改时间 */
    unsigned long i_ctime;

    /* i节点所在设备分区号 */
    unsigned short i_dev;

    /* i节点号, 与i节点位图中bit位号对应 */
    unsigned short i_num;

    /* i节点引用计数 */
    unsigned short i_count;

    /* i节点锁定标志, 0-未上锁, 1-已上锁 */
    unsigned char i_lock;

    /* i节点被修改标志, 0-未被修改, 1-已被修改 */
    unsigned char i_dirt;

    /* i节点管道标志,若该数据成为不为0则表示该i节点为管道i节点 */
    unsigned char i_pipe;

    /* i节点已挂载文件系统的标志,
     * 0-未挂载, 1-已挂载。*/
    unsigned char i_mount;

    /* 搜索标志, lseek操作时置位 */
    unsigned char i_seek;

     /* i节点已更新标志,该值不为0时表需将i节点内容更新到磁盘中 */
    unsigned char i_update;
};

/* struct file,
 * 描述文件的结构体类型。*/
struct file {
    unsigned short f_mode;    /* 文件属性,对应i节点i_mode成员 */
    unsigned short f_flags;   /* 文件访问和控制标志 */
    unsigned short f_count;   /* 文件引用计数 */
    struct m_inode * f_inode; /* 指向文件的i节点 */
    off_t f_pos; /* 访问文件的当前位置 */
};


/* struct super_block,
 * 超级块结构体类型。
 * 
 * 超级块用于描述文件系统各部分
 * 在磁盘上的大体信息,如大小,起始位置等。*/
struct super_block {
/* 此部分成员在内存和磁盘上都存在,
 * 这些数据成员在内存中被修改后, 会被同步到磁盘中。*/

    /* i节点数 */
    unsigned short s_ninodes;

    /* 数据逻辑块块数 */
    unsigned short s_nzones;

    /* i节点位图所占块数,
     * i节点位图用于记录i节点的使用状态,
     * 位为1/0表明对应i节点已/未被使用。*/
    unsigned short s_imap_blocks;

    /* 逻辑块位图所占块数,
     * 逻辑块位图用于记录数据逻辑块的使用状态,
     * 位为1/0表明对应逻辑数据块已/未被使用。*/
    unsigned short s_zmap_blocks;

    /* 数据逻辑块区域中第一块数据逻辑块的块号 */
    unsigned short s_firstdatazone;

    /* 以2为底数, 以"逻辑块大小/扇区大小"为真数来表明逻辑块大小,
     * 如逻辑块为1Kb时, s_log_zone_size = log2(2) = 1。*/
    unsigned short s_log_zone_size;

    /* 文件最大长度和文件系统类型 */
    unsigned long s_max_size;
    unsigned short s_magic;
/* These are only in memory */
/* 以下成员仅在内存中,
 * 这些数据成员用于记录以上数据成员是否被修改,实现多进程的互斥访问等。*/

    /* (struct buffer_head *)类型的指针数组,
     * 指针数组指向的内存用作i节点位图缓冲区。
     *
     * 一个缓冲区块大小为1Kb<见fs/buffer.c>,
     * s_imap[8]一共可以标识8 * 1024 * 8 = 65536个i节点。*/
    struct buffer_head * s_imap[8];

    /* 同s_imap, s_zmap[8]指针数组指向内存用作逻辑块位图缓冲区。
     * 一个文件系统最多有65536个逻辑块即64Mb大小。
     * i节点描述的文件尺寸最大可达256Mb之多,文件大小会受此字段限制。*/
    struct buffer_head * s_zmap[8];

    /* 超级块所在设备的逻辑设备分区号 */
    unsigned short s_dev;

    /* 指向文件系统根i节点 */
    struct m_inode * s_isup;

    /* 指向文件系统所挂载处的i节点 */
    struct m_inode * s_imount;

    /* 超级块可位于磁盘部分数据成员的最后被修改时间 */
    unsigned long s_time;

    /* 用于进程对超级块的互斥访问,
     * <sleep_on + wake_up>。*/
    struct task_struct * s_wait;

    /* 超级块的上锁标志, 0-未上锁, 1-已上锁 */
    unsigned char s_lock;

    /* 超级块只读标志 */
    unsigned char s_rd_only;

    /* 超级块修改标志, 0-未修改, 1-已修改 */
    unsigned char s_dirt;
};

/* struct d_super_block,
 * 磁盘中超级块结构体类型。
 * 其中的数据成员含义同
 * struct msuper_block类型中磁盘部分数据成员的含义。*/
struct d_super_block {
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned long s_max_size;
    unsigned short s_magic;
};

/* struct dir_entry,
 * 目录项,即目录数据逻辑块中的内容。*/
struct dir_entry {
    /* 目录或文件的i节点号;
     * 目录或文件名。*/
    unsigned short inode;
    char name[NAME_LEN];
};

/* 声明定义在各源文件中的全局变量和全局函数 */
extern struct m_inode inode_table[NR_INODE];
extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern struct buffer_head * start_buffer;
extern int nr_buffers;

extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void truncate(struct m_inode * inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode * inode);
extern int bmap(struct m_inode * inode,int block);
extern int create_block(struct m_inode * inode,int block);
extern struct m_inode * namei(const char * pathname);
extern int open_namei(const char * pathname, int flag, int mode,
    struct m_inode ** res_inode);
extern void iput(struct m_inode * inode);
extern struct m_inode * iget(int dev,int nr);
extern struct m_inode * get_empty_inode(void);
extern struct m_inode * get_pipe_inode(void);
extern struct buffer_head * get_hash_table(int dev, int block);
extern struct buffer_head * getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern void bread_page(unsigned long addr,int dev,int b[4]);
extern struct buffer_head * breada(int dev,int block,...);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern struct m_inode * new_inode(int dev);
extern void free_inode(struct m_inode * inode);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);

#endif
