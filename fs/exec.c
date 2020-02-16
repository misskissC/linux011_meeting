/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 */
/* 根据需求加载可执行文件到内存即首次加载可执行文件时除可执行文件头部外可暂
 * 不读取其他内容到内存中,该功能实现于01.12.91。可执行文件i节点由进程管理结
 * 构体的executable成员指向,可执行文件内容由缺页异常加载。
 *
 * 另外,此文在此吹个有底气的牛:实现按需(基于页异常)加载可执行文件到内存执行
 * 的功能,我只花了不到2小时时间。*/

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <a.out.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/segment.h>

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
#define MAX_ARG_PAGES 32

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
/* create_tables,
 * 在进程内存段末端组织进程环境(变量)参数和命令行参数信息;
 * 该函数返回参数信息首地址。*/
static unsigned long * create_tables(char * p,int argc,int envc)
{
    unsigned long *argv,*envp;
    unsigned long * sp;

    /* 环境参数末端地址以4字节对齐 */
    sp = (unsigned long *) (0xfffffffc & (unsigned long) p);
    /* 空出用来存各环境参数和命令行参数的地址的空间 */
    sp -= envc+1;
    envp = sp;
    sp -= argc+1;
    argv = sp;
    /* 将存各环境参数的地址的内存段首地址,
     * 存各命令行参数的地址的内存段首地址,
     * 命令行参数个数依次写入内存中。*/
    put_fs_long((unsigned long)envp,--sp);
    put_fs_long((unsigned long)argv,--sp);
    put_fs_long((unsigned long)argc,--sp);
    
    /* 将各命令行参数的地址依次写入argv内存段 */
    while (argc-->0) {
        put_fs_long((unsigned long) p,argv++);
        /* 跳过命令行参数字符串串 */
        while (get_fs_byte(p++)) /* nothing */ ;
    }
    /* 存储命令行参数的地址的内存段结束标志位0 */
    put_fs_long(0,argv);

    /* 将各环境参数的地址依次写入envp内存段 */
    while (envc-->0) {
        put_fs_long((unsigned long) p,envp++);
        /* 跳过环境参数本身 */
        while (get_fs_byte(p++)) /* nothing */ ;
    }
    /* 存储环境参数的地址的内存段结束标志位0 */
    put_fs_long(0,envp);
    return sp;

}
/* 经create_tables后,
 * 进程跟参数相关的逻辑地址空间跟分布大体如下。
 *64Mb|      | 
 *    |======| 环
 *    |      | 境
 *    |      | 参
 *    |      | 数
 *    |      | 段
 *    |======|   
 *    |      | 命
 *    |      | 令
 *    |      | 行
 *    |      | 参
 *    |      | 数
 *    |      | 段
 *    |======|   
 *    |      | 各
 *    |      | 环
 *    |      | 境
 *    |      | 参
 *    |      | 数
 *    |      | 的
 *    |      | 地
 *    |      | 址
 *    |      | 的
 *    |      | 段
 *    |======|   
 *    |      | 各
 *    |      | 命
 *    |      | 令
 *    |      | 行
 *    |      | 参
 *    |      | 数
 *    |      | 的
 *    |      | 地
 *    |      | 址
 *    |      | 的
 *    |======| 段
 *    | envp |   
 *    |======|   
 *    | argv |   
 *    |======|   
 *    | argc |   
 *    |======| sp
 *    |  .   |   
 *    |  .   |   
 *   0|  .   | */

/*
 * count() counts the number of arguments/envelopes
 */
/* count,
 * 计算argv所指内存段中所包含的
 * (char *)类型指针元素, 指针元
 * 素以NULL结尾。*/
static int count(char ** argv)
{
    int i=0;
    char ** tmp;

    /* count函数应用场景(32位地址线)。
     * 
     * 指针数组存储字符串常量地址,以NULL结尾。
     * char *arg[] = {"1","2","3",NULL};
     *
     * 内存中的字符串常量。
     * -------------
     * |"1"|"2"|"3"|
     * -------------
     * x   y   z
     * 
     * 设arg所在内存首地址为w
     * ---------------------------
     * |x     |y     |z     |NULL|
     * ---------------------------
     * w      w+4    w+8    w+12
     * arg[0] arg[1] arg[2] arg[3]
     * arg+0  arg+1  arg+2  arg+3 
     *
     * -----
     * |w  |
     * -----
     * tmp=argv
     * 
     * tmp作为右值时为其内存中内容即w;
     * *((unsigned long *)tmp)即从内存地址w处读取sizeof(long)=4字节内容即x。
     * 
     * tmp++,作为指向(char *)类型的指针,tmp++对应w+4;
     * *( (unsigned long *)tmp)即从内存地址(w+4)中读取sizeof(long)=4字节内容即y。
     * ...
     * 
     * 待*( (unsigned long *)tmp)为NULL时则退出循环,
     * 便统计了指针数组arg中指针元素个数从而统计了指针数组所指字符串个数。*/

    if (tmp = argv)
        while (get_fs_long((unsigned long *) (tmp++)))
            i++;

    return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */
/* copy_strings,
 * 将argv中的argc个指针元素所指数据拷贝到内核中,拷贝后的目的内存由page中相应元素指向。
 * from_kmem用于标识 *argv 和 **argv 是内核还是用户空间地址。该函数返回参数内存块首地址。*/
static unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
    unsigned long p, int from_kmem)
{
    char *tmp, *pag;
    int len, offset = 0;
    unsigned long old_fs, new_fs;

    if (!p)
        return 0;   /* bullet-proofing */
    new_fs = get_ds();
    old_fs = get_fs();
    /* 若参数在内核数据段则置fs加载内核数据段描述符 */
    if (from_kmem==2)
        set_fs(new_fs);

    /* 将argv中的argc个元素所指向字符串拷贝考内核内存中 */
    while (argc-- > 0) {
        /* 若 *argv 为内核空间地址则让fs加载内核数据段描述符 */
        if (from_kmem == 1)
            set_fs(new_fs);
        /* 将(argv+argc)地址中的地址取出 */
        if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc)))
            panic("argc is wrong");
        /* 若**argv为用户空间地址则恢复fs加载用户数据段描述符 */
        if (from_kmem == 1)
            set_fs(old_fs);
        
        /* 求取argv[argc]所指字符串长度 */
        len=0; /* remember zero-padding */
        do {
            len++;
        } while (get_fs_byte(tmp++));
        /* 判断字符串长度是否超过预留内存长度*/
        if (p-len < 0) { /* this shouldn't happen - 128kB */
            set_fs(old_fs);
            return 0;
        }
        
        /* 将argv[argc]即tmp所指字符串拷贝到空闲内核内存中 */
        while (len) {
            --p; --tmp; --len;
            if (--offset < 0) {
                /* offset=(PAGE_SIZE*32-5)%PAGE_SIZE=PAGE_SIZE-5 */
                offset = p % PAGE_SIZE;
                /* 若曾在本函数中间加载内核数据段描述
                 * 符于fs则先恢复以让内存分配函数使用。*/
                if (from_kmem==2)
                    set_fs(old_fs);
                /* 分配一页内存由page相应元素和pag指向 */
                if (!(pag = (char *) page[p/PAGE_SIZE]) &&
                    !(pag = (char *) page[p/PAGE_SIZE] =
                    (unsigned long *) get_free_page())) 
                    return 0;
                /* 恢复fs加载内核数据段描述符 */
                if (from_kmem==2)
                    set_fs(new_fs);
            }
            /* 将tmp所指字符串拷贝到内存页中(逆向拷贝) */
            *(pag + offset) = get_fs_byte(tmp);
        }
    }
    
    /* 恢复fs加载用户数据段描述符 */
    if (from_kmem==2)
        set_fs(old_fs);
    return p;
}

/* change_ldt,
 * 更改当前进程的LDT,使其代码段限长为text_size,数据段限长为64Mb;
 * 将进程数据段末端与page中保存环境变量和命令行等参数的内存页映射。*/
static unsigned long change_ldt(unsigned long text_size,unsigned long * page)
{
    unsigned long code_limit,data_limit,code_base,data_base;
    int i;

    /* 代码段以页对齐;数据段大小为64Mb */
    code_limit = text_size+PAGE_SIZE -1;
    code_limit &= 0xFFFFF000;
    data_limit = 0x4000000;

    /* 基于当前进程代码段和数据段基址和所计算的限长,设置新的LDT表 */
    code_base = get_base(current->ldt[1]);
    data_base = code_base;
    set_base(current->ldt[1],code_base);
    set_limit(current->ldt[1],code_limit);
    set_base(current->ldt[2],data_base);
    set_limit(current->ldt[2],data_limit);
    
/* make sure fs points to the NEW data segment */
    /* 确保fs寄存器加载用户数据段描述符 */
    __asm__("pushl $0x17\n\tpop %%fs"::);
    /* 将用户程序数据段末端与保存参数(环境、命令行等)的内存页映射 */
    data_base += data_limit;
    for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {
        data_base -= PAGE_SIZE;
        if (page[i])
            put_page(page[i],data_base);
    }
    return data_limit;
}

/*
 * 'do_execve()' executes a new program.
 */
/* do_execve,
 *
 * 
 * eip,tmp在_sys_execve中被入栈,tmp为_sys_execve call do_execve
 * 时入栈的eip寄存器;
 * filename, argv, envp分别和_system_call中入栈的edx, ecx, ebx对应。*/
int do_execve(unsigned long * eip,long tmp,char * filename,
    char ** argv, char ** envp)
{
    struct m_inode * inode;
    struct buffer_head * bh;
    struct exec ex;
    unsigned long page[MAX_ARG_PAGES];
    int i,argc,envc;
    int e_uid, e_gid;
    int retval;
    int sh_bang = 0;
    unsigned long p=PAGE_SIZE*MAX_ARG_PAGES-4;

    /* eip[1]即*(eip + 1)即系统调用execve时cs寄存器的值,
     * 若cs段寄存器值不为0x0f则表明当时进程为内核程序。*/
    if ((0xffff & eip[1]) != 0x000f)
        panic("execve called from supervisor mode");
    
    for (i=0 ; i<MAX_ARG_PAGES ; i++) /* clear page-table */
        page[i]=0;
    /* 获取可执行文件filename的i节点 */
    if (!(inode=namei(filename))) /* get executables inode */
        return -ENOENT;
    /* 分别统计传递给execve的命令行参数和环境变量个数 */
    argc = count(argv);
    envc = count(envp);

restart_interp:
    /* 可执行文件需为常规普通文件 */
    if (!S_ISREG(inode->i_mode)) { /* must be regular file */
        retval = -EACCES;
        goto exec_error2;
    }
    i = inode->i_mode;
    /* 根据可执行文件属性标志,设置其用户id和组id,
     * 当前进程为即将执行可执行程序的父进程。*/
    e_uid = (i & S_ISUID) ? inode->i_uid : current->euid;
    e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;

    /* 由于是有当前进程执行可执行文件,
     * 所以看看当前进程对于可执行文件
     * 的操作权限, 当当前进程是可执行
     * 文件组员时获取权限, 当当前进程
     * 是可执行文件宿主时获取其宿主权限。*/
    if (current->euid == inode->i_uid)
        i >>= 6;
    else if (current->egid == inode->i_gid)
        i >>= 3;
    /* 当当前进程作为组员或宿主身份对可执行文件没有执行权限且
     * 不满足可执行文件对所有进程都开放执行权限且当前进程为超
     * 级进程(初始进程的euid为0)的条件 时则出错返回。*/
    if (!(i & 1) &&
        !((inode->i_mode & 0111) && suser())) {
        retval = -ENOEXEC;
        goto exec_error2;
    }
    /* 读可执行程序文件前1Kb内容进入内存 */
    if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {
        retval = -EACCES;
        goto exec_error2;
    }
    /* 用作可执行文件头部的解析 */
    ex = *((struct exec *) bh->b_data); /* read exec-header */
    
    /* 若可执行文件为(shell)脚本文件 */
    if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
        /*
         * This section does the #! interpretation.
         * Sorta complicated, but hopefully it will work.  -TYT
         */

        char buf[1023], *cp, *interp, *i_name, *i_arg;
        unsigned long old_fs;

        /* 除去开头的#!字符,将脚本前1Kb内容拷贝到buf中并释放缓冲区块和i节点 */
        strncpy(buf, bh->b_data+2, 1022);
        brelse(bh);
        iput(inode);
        buf[1022] = '\0';
        
        /* 处理第1行内容(如#!/bash/sh),跳过#!后所有空格和制表符 */
        if (cp = strchr(buf, '\n')) {
            *cp = '\0';
            for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
        }
        /* 若没有回车符或已到行尾则表明脚本无内容需处理 */
        if (!cp || *cp == '\0') {
            retval = -ENOEXEC; /* No interpreter name found */
            goto exec_error1;
        }
        /* 如脚本首行内容为#!/bash/sh,i_name将等于"sh",interp="/bash/sh" */
        interp = i_name = cp;
        i_arg = 0;
        for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
            if (*cp == '/')
            i_name = cp+1;
        }
        if (*cp) {
            *cp++ = '\0';
            i_arg = cp;
        }
/* 到此处,就解析出脚本首行中(#!/bash/sh)的不带参数的解释器(/bash/sh)了 */
        /*
         * OK, we've parsed out the interpreter name and
         * (optional) argument.
         */
         /* 将用户空间的环境变量参数和命令行参数拷贝到可执行程序用
          * 于存储参数的内存中, 保存环境变量参数和命令行参数内存的
          * 地址由page末尾元素指向, 即环境变量和命令行参数此处预分
          * 配128Kb内存末端。*/
        if (sh_bang++ == 0) {
            p = copy_strings(envc, envp, page, p, 0);
            p = copy_strings(--argc, argv+1, page, p, 0);
        }

        /*
         * Splice in (1) the interpreter's name for argv[0]
         *           (2) (optional) argument to interpreter
         *           (3) filename of shell script
         *
         * This is done in reverse order, because of how the
         * user environment and arguments are stored.
         */
        /* 将filename拷贝到可执行文件用于存储参数的内存段中&filename
         * 是内核内存空间地址,filename是用户内存空间地址。*/
        p = copy_strings(1, &filename, page, p, 1);
        argc++;
        /* 将脚本首行中的参数拷贝到可执行文件用于存储参数的内存段中 */
        if (i_arg) {
            p = copy_strings(1, &i_arg, page, p, 2);
            argc++;
        }
        /* 将解释器名拷贝到可执行文件用于存储参数的内存段中 */
        p = copy_strings(1, &i_name, page, p, 2);
        argc++;
        if (!p) {
            retval = -ENOMEM;
            goto exec_error1;
        }
/* -----------------------------------------------------------------------------------------------------------------
 * ... interpreter_name interpreter_arg|filename|argv[1] argv[2] ... argv[argc-1]|envp[0] envp[1] ... envp[envc-1] |
 * -----------------------------------------------------------------------------------------------------------------
 * 0                                                                                                              0x1ffff
 *
 * 为进程命令行参数和环境变量预留的128Kb内存与page[32]对应。*/

        /*
         * OK, now restart the process with the interpreter's inode.
         */
        /* 让fs加载内核数据段描述符 */
        old_fs = get_fs();
        set_fs(get_ds());
        /* 获取interp所指向脚本可执行程序名(如/bash/sh)的i节点 */
        if (!(inode=namei(interp))) { /* get executables inode */
            set_fs(old_fs);
            retval = -ENOENT;
            goto exec_error1;
        }
        /* 恢复fs指向用户空间数据段描述符 */
        set_fs(old_fs);
        /* 跳转restart_interp处以启动执行脚本可执行程序
         * (如/bash/sh),现在inode指向脚本程序(/bash/sh)。*/
        goto restart_interp;
    }
    
    /* 释放可执行程序前1Kb缓冲区块 */
    brelse(bh);
/* 解析可执行文件头部 */
    if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
        ex.a_text+ex.a_data+ex.a_bss>0x3000000 ||
        inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
        retval = -ENOEXEC;
        goto exec_error2;
    }
    if (N_TXTOFF(ex) != BLOCK_SIZE) {
        printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
        retval = -ENOEXEC;
        goto exec_error2;
    }
    /* 若当前可执行程序文件不为脚本可执行文件则将环境变量
     * 和命令行参数拷贝到可执行文件用于存储参数内存段末端。*/
    if (!sh_bang) {
        p = copy_strings(envc,envp,page,p,0);
        p = copy_strings(argc,argv,page,p,0);
        if (!p) {
            retval = -ENOMEM;
            goto exec_error2;
        }
    }
/* 略看filename各参数的存储,假设参数未超过一页内存。
 * |<----------------------128Kb-------------------->|
 * ---------------------------------------------------
 * ...|........命令行参数+环境变量参数(+脚本程序参数)|
 * ---------------------------------------------------
 * 0           p                                     0x1ffff
 * 用于保存各参数内存页的物理地址存在page[31]中。*/

/* OK, This is the point of no return */
/* 使用当前进程管理结构体current管理execve所加载可执行文件的运行 */
    /* 覆盖可执行文件i节点 */
    if (current->executable)
        iput(current->executable);
    current->executable = inode;
    /* 复位信号处理函数指针 */
    for (i=0 ; i<32 ; i++)
        current->sigaction[i].sa_handler = NULL;
    /* 关闭所打开文件 */
    for (i=0 ; i<NR_OPEN ; i++)
        if ((current->close_on_exec>>i)&1)
            sys_close(i);
    current->close_on_exec = 0;
    /* 释放当前进程代码段和数据段所占内存段 */
    free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
    free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
    /* 使用协处理标志复位 */
    if (last_task_used_math == current)
        last_task_used_math = NULL;
    current->used_math = 0;
    /* 将当前进程LDT更改以描述可执行程序filename代码段和数据段,
     * 并将可执行程序filename数据段末尾段内存空间地址与保存各参
     * 数的page内存页相映射。*/
    p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE;
/* change_ldt执行完毕后,
 * 略看filename进程跟环境变量等参数相关内存地址空间。
 * |<---------------------64MB---------------->|
 * ---------------------------------------------
 * .............................|arguments.....|
 * ---------------------------------------------
 * 0                            p=64Mb-128Kb+p 0x3ffffff
 * 即将进程数据段末端映射到保存各参数的内存页。*/
    /* 在filename进程内存段末端组织环境变量和命令行参数 */
    p = (unsigned long) create_tables((char *)p,argc,envc);

    current->brk = ex.a_bss +
        (current->end_data = ex.a_data +
        (current->end_code = ex.a_text));
    current->start_stack = p & 0xfffff000; /* 栈顶地址 */
    /* 可执行文件属性不同时有效id不同(宿主id或继承当前进程id ) */
    current->euid = e_uid;
    current->egid = e_gid;
    i = ex.a_text+ex.a_data;

    /* 若进程末端内存地址不以4Kb对齐则往该内存地址对应内存页写0 */
    while (i&0xfff)
        put_fs_byte(0,(char *) (i++));
    /* 更改发生系统调用execve()时CPU往栈中备份的eip和esp寄存器的值,
     * eip=filename可执行文件指令入口地址处,
     * esp=p即进程命令行参数信息前4字节为进程栈顶。
     * 
     * 若对寄存器部分不太熟悉,则回system_call.s中看看栈中寄存器的备份布局吧。*/
    eip[0] = ex.a_entry; /* eip, magic happens :-) */
    eip[3] = p; /* stack pointer */
/* 由于用filename可执行文件代码入口地址修改了发生
 * 系统调用时CPU往栈中备份的eip寄存器,所以CPU会转
 * 而执行filename可执行文件(加上对诸如current->xx
 * 信息的修改,可执行文件的执行环境也是正确的啦)。
 *
 * 从调用系统调用execve开始到此处,就是execve加载
 * 可执行文件覆盖原进程执行的全部秘密啦。
 *
 * 能阅读明白的钥匙似乎是内存页机制,进程管理等相关知识和反复阅读。*/
    return 0;
exec_error2:
    iput(inode);
exec_error1:
    for (i=0 ; i<MAX_ARG_PAGES ; i++)
        free_page(page[i]);
    return(retval);
}
