/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */
/* 执行时加载功能于01.12.91开始开发,执行时加载的需求比较强烈,它也应该容易被实现。*/
/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */
/* 基于共享内存页机制,执行时加载比较简单。页共享于02.12.91开始开发。
 *
 * 在老版本内核中运行30个 /bin/sh 时可占用约6Mb内存,目前看来运行正常。
 *
 * 此次还修正了些 invalidate() 的不足之处。*/
 
#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

volatile void do_exit(long code);

/* 提示无可用的内核内存, 并调用do_exit(SIGSEGV),
 * 暂先不转去阅读do_exit的源码吧。*/
static inline volatile void oom(void)
{
    printk("out of memory\n\r");
    do_exit(SIGSEGV);
}

/* 输入: "a" (0)- eax=0。
 * 
 * 将页目录首地址(0)重新加载给cr3,
 * 以刷新页机制相关数据结构缓冲区(快表)中的数据。*/
#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
/* 若要修改以下宏常量, 则需在head.s中对页相关程序进行相应的修改。*/
/* LOW_MEM - 实模式内存大小。
 * PAGING_MEMORY - linux 0.11将会管理扩展内存的最大值。
 * PAGING_PAGES - PAGING_MEMORY内存的页数。
 * MAP_NR(addr) - 计算addr在扩展内存中的页偏移。
 * USED - 内存页引用计数。*/
#define LOW_MEM 0x100000
#define PAGING_MEMORY (15*1024*1024)
#define PAGING_PAGES (PAGING_MEMORY>>12)
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12)
#define USED 100

#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

/* 用于记录操作系统所使用内存的总大小 */
static long HIGH_MEMORY = 0;

/* 将首地址为from的内存页内容拷贝到首地址为to的内存页中。
 * "S" (from), ESI = from;
 * "D" (to), EDI = to;
 * "c" (1024), ecx = 1024。
 * cld; rep; movsl 相当于
 * while (ecx--)
 *     movsl ds:esi, es:edi
 *     esi += 4
 *     edi += 4
 * 即完成从from内存页复制1024 * 4字节内容到to内存页中。*/
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

/* mem_map数组以页为单位与操作系统所使用的扩展内存的映射关系如下。
 * mem_map element        memory space
 *  mem_map[0]        [0x100000, 0x100fff]
 *  mem_map[1]        [0x101000, 0x101fff]
 *      ...                   ...        
 *  mem_map[3838]     [0xffe000, 0xffefff]
 *  mem_map[3839]     [0xfff000, 0xffffff]
 * 
 * mem_map[i] = count 标识
 * 内存段[0x100000 + i << 12, 0x100000 + i << 12 + 0xfff]
 * 的引用计数为count, i = [0..PAGING_PAGES - 1].
 * 下标i为mem_map所映射内存页在扩展内存中的页偏移。*/
static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
/* [3] get_free_page,
 * 从mem_map数组末尾开始往前遍历,返回首个
 * 引用计数为0的mem_map元素所对应物理内存
 * 页的首地址(见mem_map处注释),并在该元素
 * mem_map中增加该内存页的引用计数;若无空
 * 闲内存页则返回0。*/
unsigned long get_free_page(void)
{
/* 为啥不是eax ? */
register unsigned long __res asm("ax");

/* 按照此次阅读linux 0.11的路线,
 * 此处应该是第一次遇到C程序中的GCC内联汇编。
 * 
 * 符合linux内联汇编语法的书本可参考
 *《Professional Assembly language》Richard Blum, P401_411.
 *
 * 内联汇编的输入部分。
 * "0" (0), 将0赋值给第一个约束("=a")中的寄存器即eax=0;
 * "i" (LOW_MEM), 立即数LOW_MEM;
 * "c" (PAGING_PAGES), 将PAGING_PAGES赋值给ecx寄存器;
 * "D" (mem_map+PAGING_PAGES-1), 将mem_map+PAGING_PAGES-1
 * 即mem_map末尾元素的地址赋值给IDI寄存器。
 * 
 * 内联汇编指令。
 * std; repne; scasb 当ecx不为0且标志寄存器ZF=0时,
 * 比较al(0)和es:edi指向内存中的值(mem_map[i]), 
 * 若相等则置ZF=1, edi减1, ecx减1。
 * 
 * 用C程序表达以上几条汇编指令的含义为
 * while (ecx-- && !eflag.zf)
 *     if (al == es:edi--)
 *         zf = 1
 * 若在mem_map中没有找到值为0的元素,
 * 则向前跳转1标号处结束内联汇编的执行,
 * 随后将eax的值赋给__res并返回。
 * 
 * 若在mem_map中找到值为0的元素,
 * 则将1赋给相应的mem_map元素以标识对应内存页的引用计数为1
 * (edi需要加1, 是因为在std; repne; scasb指令中找到引用计数为0的mem_map元素后,
 * edi继续被减了1),
 * 然后通过下标值(扩展内存中的页偏移)ecx计算出mem_map[ecx]对应内存页的首地址
 * 即ecx << 12 + %2(LOW_MEM)。
 *
 * 在获得内存页首地址后, 给所获得的内存页清0。
 * ecx = 1024; edx = 内存页首地址; edi = 内存页首地址 + 4092
 * std; rep; stosl 相当于
 * while (ecx--)
 *     movl eax, es:edi
 *     edi -= 4
 *
 * 在内存页清0后, 将内存页首地址edi赋给eax寄存器,
 * 内联汇编最后输出给__res变量, __res作为函数最终返回值返回。*/
__asm__("std ; repne ; scasb\n\t"
    "jne 1f\n\t"
    "movb $1,1(%%edi)\n\t"
    "sall $12,%%ecx\n\t"
    "addl %2,%%ecx\n\t"
    "movl %%ecx,%%edx\n\t"
    "movl $1024,%%ecx\n\t"
    "leal 4092(%%edx),%%edi\n\t"
    "rep ; stosl\n\t"
    "movl %%edx,%%eax\n"
    "1:"
    :"=a" (__res)
    :"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
    "D" (mem_map+PAGING_PAGES-1)
    :"di","cx","dx");
return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
/* 释放物理内存首地址为addr的内存页, 该函数由free_page_tables调用。*/
/* [4] free_page,
 * 释放首地址为addr的内存页。
 * "释放"的实际含义是减少该内存页的引用计数,
 * 当该内存页的引用计数为0时表该内存页为空闲状态。*/
void free_page(unsigned long addr)
{
    if (addr < LOW_MEM) return;
    if (addr >= HIGH_MEMORY)
        panic("trying to free nonexistent page");
    /* 计算内存地址addr在扩展内存中的页偏移 */
    addr -= LOW_MEM;
    addr >>= 12;

    /* 减少内存页的引用计数 */
    if (mem_map[addr]--) return;
    mem_map[addr]=0;
    panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
/* [5] free_page_tables,
 * 从from逻辑内存页所在页表开始(form为该页表第一个页表项所映射的内存地址),
 * 连续释放(size/4Mb)个页表及页表所映射物理内存, 并在页目录中清理相应的页表信息。*/
int free_page_tables(unsigned long from,unsigned long size)
{
    unsigned long *pg_table;
    unsigned long * dir, nr;

    /* 检查from是否为页表所映射内存的入口地址 */
    if (from & 0x3fffff)
        panic("free_page_tables called with wrong alignment");
    if (!from)
        panic("Trying to free up swapper memory space");

    /* 将size的单位换算为4Mb;+0x3fffff能让size不足4Mb时补齐4Mb */
    size = (size + 0x3fffff) >> 22;

    /* 对于32位内存地址,
     * 内存地址最高10位为其页表信息在页目录中的偏移,
     * 由于页目录的内存地址为0, 所以该偏移就是其页表信息的内存地址。*/
    dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */

    /* 从内存地址from对应的页表开始,连续释放size个页表及其
     * 所映射物理内存页,并在页目录中清理相应的页表信息。*/
    for ( ; size-->0 ; dir++) {
        if (!(1 & *dir)) /* 页表信息最低位P=1表示其描述的页表存在 */
            continue;
        /* 从页表信息中获取页表首地址 */
        pg_table = (unsigned long *) (0xfffff000 & *dir);
        /* 一个页表中有1Kb页表项,每个页表项最低位P=1表示该页表项映射了
         * 物理内存页,若页表项最低位P=1则释放其所映射的物理内存页。*/
        for (nr=0 ; nr<1024 ; nr++) {
            if (1 & *pg_table)
                free_page(0xfffff000 & *pg_table);
            *pg_table = 0; /* 清理页表项 */
            pg_table++;
        }
        /* 释放页表所占内存 */
        free_page(0xfffff000 & *dir);
        *dir = 0; /* 清理页目录中的页表信息 */
    }

    /* 刷新页机制的缓冲区 */
    invalidate();
    return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
/* [6] copy_page_tables,
 * 从地址from对应的页表开始, 连续复制size个页表到目的页表中。
 * 
 * 目的页表将从主存中申请分配,
 * 分配页表的页表信息将从 地址to在页目录中的偏移处开始连续设置。
 * 
 * 复制成功后, 增加源页表所映射内存页的引用计数,
 * 并更改源页表项和目标页表项的属性为只读。*/
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
    unsigned long * from_page_table;
    unsigned long * to_page_table;
    unsigned long this_page;
    unsigned long * from_dir, * to_dir;
    unsigned long nr;

    /* 检查源/目的内存地址是否为一个页表所映射内存的入口。*/
    if ((from&0x3fffff) || (to&0x3fffff))
        panic("copy_page_tables called with wrong alignment");
    
    /* 对于32位内存地址, 高10位为其页表信息在页目录中的偏移,
     * 由于页目录起始地址为0, 所以该偏移就是其页表信息的内存地址。*/
    from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
    to_dir = (unsigned long *) ((to>>20) & 0xffc);
    size = ((unsigned) (size+0x3fffff)) >> 22; /* 将size的单位换算为4Mb */

    /* 申请分配size个页表并从地址to在页目录偏移处开始连续设置他们,
     * 并将from对应的页表内容拷贝到以上页表中。*/
    for( ; size-->0 ; from_dir++,to_dir++) {
        if (1 & *to_dir) /* 目的页表一定要是空闲的 */
            panic("copy_page_tables: already exist");
        if (!(1 & *from_dir)) /* 源页表不存在则不用复制 */
            continue;
        
        /* 从页目录中的源页表信息中获取源页表地址,
         * 页目录中的页表信息高20位为页表地址(低12位默认为0)。*/
        from_page_table = (unsigned long *) (0xfffff000 & *from_dir);

        /* 申请空闲内存页用作目的页表 */
        if (!(to_page_table = (unsigned long *) get_free_page()))
            return -1;	/* Out of memory, see freeing */

        /* 将目的页表信息设置到页目录中,
         * 并将该页表信息的属性设置为可读可写且存在。*/
        *to_dir = ((unsigned long) to_page_table) | 7;

        /* 如果起始地址from为0, 则只复制from所在页表的
         * 前160个页表项到目的页表中, 否则复制整张页表。*/
        nr = (from==0)?0xA0:1024;
        for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
             /* 获取当前页表项内容, 页表项高20位为内存页的首地址 */
            this_page = *from_page_table;
            if (!(1 & this_page)) /* 跳过没有映射内存页的页表项 */
                continue;
            
            /* 将源页表中的页表项复制到目的页表中,
             * 并将页表项的属性更改为只读。经此复制后,
             * 目的页表项和源页表项保存了同一内存页的首地址等信息,
             * 所以后续会增加该内存页的引用计数。*/
            this_page &= ~2;
            *to_page_table = this_page;

            /* 若当前页表项所映射的内存页为mem_map所维护的扩展内存,
             * 则增加该内存页的引用计数。*/
            if (this_page > LOW_MEM) {
                *from_page_table = this_page; /* 将源内存页也设置为只读 */
                this_page -= LOW_MEM;
                this_page >>= 12;
                mem_map[this_page]++;
            }
        }
    }

    /* 刷新页机制相关数据结构体的缓冲区 */
    invalidate();
    return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
/* [7] put_page,
 * 该函数将内存页page映射给一个32位的内存地址address。
 * 完成映射后, address通过页变换后会访问到物理内存页page。*/
unsigned long put_page(unsigned long page,unsigned long address)
{
    unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

    if (page < LOW_MEM || page >= HIGH_MEMORY)
        printk("Trying to put page %p at %p\n",page,address);
    if (mem_map[(page-LOW_MEM)>>12] != 1)
        printk("mem_map disagrees with %p at %p\n",page,address);

    /* 32位内存地址address高10位为其页表信息在页目录中的偏移,
     * 由于页目录起始地址为0, 此偏移即为页表信息的内存地址。*/
    page_table = (unsigned long *) ((address>>20) & 0xffc);

    /* 根据页表信息判断其所描述的页表是否存在,
     * 若address原本已分配页表, 则在该页表中映射内存页page。
     *
     * 若address还未有页表, 则在内核内存中为其分配一页内存作为其页表,
     * 并在该页表中映射内存页page。*/
    if ((*page_table)&1)
        /* 从页目录中的页表信息中获取页表地址 */
        page_table = (unsigned long *) (0xfffff000 & *page_table);
    else {
        if (!(tmp=get_free_page()))
            return 0;
        *page_table = tmp|7; /* 在页目录中设置页表信息, 可读可写且存在 */
        page_table = (unsigned long *) tmp; /* 页表地址 */
    }
    /* 根据32位内存地址address的中间10位计算出其页表项在页表中的偏移,
     * 并在该偏移处设置页表项以存储
     * 内存页page的首地址及其可读可写等属性信息。*/
    page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */
    return page;
}

/* [8]up_wp_page,
 * 实现页表项table_entry所映射内存页的写时拷贝。
 * 
 * 当table_entry所映射内存页的引用计数为1时(单进程使用该内存页),
 * 则在table_entry中直接添加写属性即可;
 * 当table_entry所映射内存页的引用计数超过1时,
 * 则为table_entry映射一页可读可写的空闲内存页,
 * 并将原来所映射内存页的内容拷贝到其新映射的内存页中以供写操作。*/
void un_wp_page(unsigned long * table_entry)
{
    unsigned long old_page,new_page;

    /* table_entry一内存页的页表项地址, 获取其所描述的内存页地址 */
    old_page = 0xfffff000 & *table_entry;

    /* 若该内存页的引用计数为1, 则通过其页表项为其增添可写的属性,
     * 然后刷新页相关数据结构体的快表缓冲区, 并返回。*/
    if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
        *table_entry |= 2;
        invalidate();
        return;
    }
    
    /* 若页表项table_entry所映射内存页的引用计数大于1,
     * 则为页表项table_entry新映射一页内存,
     * 并将其原来所映射内存页中的内容拷贝到新的内存页中,
     * 同时减少原内存页的引用计数。*/
    if (!(new_page=get_free_page()))
        oom();
    if (old_page >= LOW_MEM)
        mem_map[MAP_NR(old_page)]--;
    *table_entry = new_page | 7;
    invalidate();

    /* 将原内存页的内容拷贝到新内存页中 */
    copy_page(old_page,new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
/* [9] do_wp_page,
 * 写时拷贝address所映射的内存页。
 * (error_code为页写保护异常入口程序传入的参数,
 * 这跟进程和中断/异常机制有些关联, 可暂不在此时细读此函数)。*/
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
    if (CODE_SPACE(address))
        do_exit(SIGSEGV);
#endif
    /* (address>>20) &0xffc), 内存地址address的页表信息的地址;
     * *((unsigned long *) ((address>>20) &0xffc)), address的页表信息内容;
     * (0xfffff000 & *((unsigned long *) ((address>>20) &0xffc))), 得到
     * address页表信息中的页表地址。
     * (address>>10) & 0xffc), 由内存地址address中间10位获得其在页表中的偏移;
     * 
     * (unsigned long *)
     * (((address>>10) & 0xffc) + (0xfffff000 &
     * *((unsigned long *) ((address>>20) &0xffc)))), 即获得address页表项地址。*/
    un_wp_page((unsigned long *)
        (((address>>10) & 0xffc) + (0xfffff000 &
        *((unsigned long *) ((address>>20) &0xffc)))));

}

/* [10] write_verify,
 * 为内存地址address所(映射的)内存页增添可写属性。*/
void write_verify(unsigned long address)
{
    unsigned long page;

    /* ((address>>20) & 0xffc), address的页表项地址,
     * page为address的页表项内容, page & 1即判断页表项最低位是否为1,
     * 若为0则表示页表项所描述的页表不存在, 所以返回。*/
    if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
        return;
    /* 获取address页表项所描述页表的内存地址 */
    page &= 0xfffff000;
    /* 获取address的页表项 */
    page += ((address>>10) & 0xffc);
    /* 若address对应页表项所描述的内存页属性为不可读,
     * 则调用写时拷贝函数实现address映射内存页的可写属性。*/
    if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
        un_wp_page((unsigned long *) page);
    return;
}

/* [11] get_empty_page,
 * 为内存地址address映射一物理内存页。*/
void get_empty_page(unsigned long address)
{
    unsigned long tmp;

    /* 获取一空内存页tmp, 并将该内存页tmp映射到address地址上,
     * 即在address所对应的页表项中映射tem内存页。*/
    if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
        free_page(tmp);		/* 0 is ok - ignored */
        oom();
    }
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */
/* [12] try_to_share,
 * 检查任务(进程)p内存地址address处是否映射了物理页内存,
 * 若该页内存存在且干净(没有被改写过),
 * 则将其分享给当前进程(内存地址address处)。
 * p进程和当前进程所执行同一个可执行程序文件, 这是该函数被调用的前提环境。
 * (该函数跟进程相关, 可了解进程后回读)。*/
static int try_to_share(unsigned long address, struct task_struct * p)
{
    unsigned long from;
    unsigned long to;
    unsigned long from_page;
    unsigned long to_page;
    unsigned long phys_addr;

    /* 各个进程都有一段逻辑内存地址空间,
     * 如64Mb, address为[0, 64Mb)中的一个值。
     * 之所以将进程的内存地址空间称作逻辑内存地址空间,
     * 是因为他们需要通过页变换才能能映射到实际可用的内存地址。
     * (在do_no_page中, 传给share_page的参数将是将进程代码起始逻辑地址减掉了的)。
     * 
     * 在内存中运行时, 各进程逻辑地址空间的起始地址不同,
     * 所以此处需加上各进程逻辑内存空间基址来获取address
     * 在各进程中所对应的页表信息地址。*/
     
    /* 计算address在进程p和当前进程中的
     * 页表信息在页目录中的内存地址(页目录地址为0)。*/
    from_page = to_page = ((address>>20) & 0xffc);
    from_page += ((p->start_code>>20) & 0xffc);
    to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
    /* 从页表地址中取出页表信息, 若没设置页表信息则返回 */
    from = *(unsigned long *) from_page;
    if (!(from & 1))
        return 0;
    from &= 0xfffff000; /* 页表内存地址 */
    from_page = from + ((address>>10) & 0xffc); /* 页表项地址 */
    phys_addr = *(unsigned long *) from_page; /* 页表项内容 */
/* is the page clean and present? */
    if ((phys_addr & 0x41) != 0x01) /* bit[6] D=0表示内存页干净 */
        return 0;
    phys_addr &= 0xfffff000; /* 从页表项中获取内存页地址 */
    if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
        return 0;

    /* 下面开始获取当前进程页相关信息 */
    to = *(unsigned long *) to_page; /* 页表信息内容 */
    if (!(to & 1)) /* 若还没有设置页表则新设置 */
        if (to = get_free_page())
            *(unsigned long *) to_page = to | 7;
        else
            oom();
    to &= 0xfffff000; /* 页表地址 */
    to_page = to + ((address>>10) & 0xffc); /* 页表项地址 */
    if (1 & *(unsigned long *) to_page)
        panic("try_to_share: to_page already exists");
/* share them: write-protect */
    /* 更改源页表项属性使其所映射的内存页仅可读,
     * 并将其分享给当前进程。 */
    *(unsigned long *) from_page &= ~2;
    *(unsigned long *) to_page = *(unsigned long *) from_page;
    invalidate(); /* 刷新页相关数据结构缓冲区 */
    phys_addr -= LOW_MEM;
    phys_addr >>= 12;
    mem_map[phys_addr]++; /* 增加被分享内存页的引用计数 */
    return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
/* [13] share_page,
 * 查找与当前进程拥有相同可执行程序文件的进程,
 * 并共享其内存空间地址为address处的物理内存页。
 * (该函数跟进程、文件相关, 可了解进程和文件后回读)。*/
static int share_page(unsigned long address)
{
    struct task_struct ** p;

    /* current指向当前正在运行的进程,
     * 其中的executable字段指向当前进程的可执行程序文件
     * 在内存中的i节点(包含可执行程序文件信息)。
     *
     * 若当前进程已设置其可执行程序文件且可执行程序文件的引用计数大于1,
     * 则说明由该可执行程序文件运行了多个进程。*/
    if (!current->executable)
        return 0;
    if (current->executable->i_count < 2)
        return 0;
    /* 寻找到与当前进程的可执行程序文件相同的进程,
     * 将其内存空间中地址为address处所映射的内存页与当前进程共享。*/
    for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
        if (!*p)
            continue;
        if (current == *p)
            continue;
        if ((*p)->executable != current->executable)
            continue;
        if (try_to_share(address,*p))
            return 1;
    }
    return 0;
}

/* [14] do_no_page,
 * 缺页异常处理C函数。*/
void do_no_page(unsigned long error_code,unsigned long address)
{
    int nr[4];
    unsigned long tmp;
    unsigned long page;
    int block,i;

    address &= 0xfffff000;
    tmp = address - current->start_code;
    /* 如果进程刚被创建还未设置可执行文件的i节点,
     * 或在申请新的物理内存页, 则为内存地址address映射一页物理内存。*/
    if (!current->executable || tmp >= current->end_data) {
        get_empty_page(address);
        return;
    }
    /* 找与当前进程共用可执行程序文件的进程
     * 共享tmp处映射的物理内存页。*/
    if (share_page(tmp))
        return;

    /* 若没有与当前进程共用可执行程序文件的进程则申请一页内存 */
    if (!(page = get_free_page()))
        oom();
/* remember that 1 block is used for header */
    /* 在没有进程与当前进程共用可执行程序文件的情况下,
     * 需要将该页在可执行程序文件中对应的内容读到内存页中来。
     * 从可执行程序文件中读取一页数据涉及文件系统和缓冲区buffer管理
     * 内容, 可暂时略过两个函数的细节内容。待对他们有所了解后回读。*/
    block = 1 + tmp/BLOCK_SIZE;
    for (i=0 ; i<4 ; block++,i++)
        nr[i] = bmap(current->executable,block);
    bread_page(page,current->executable->i_dev,nr);

    /* 超过进程end_data部分的内容为bss段, 将bss清0。*/
    i = tmp + 4096 - current->end_data;
    tmp = page + 4096;
    while (i-- > 0) {
        tmp--;
        *(char *)tmp = 0;
    }

    /* 将物理内存页映射给地址address */
    if (put_page(page,address))
        return;
    free_page(page);
    oom();
}

/* [1] mem_init,
 * 用全局变量HIGH_MEMORY保存操作系统所管理内存的总大小,
 * 以页为单位初始化内存段[start_mem, end_mem)的引用计数。*/
void mem_init(long start_mem, long end_mem)
{
    int i;

    HIGH_MEMORY = end_mem;

    /* 使用USED初始化mem_map数组,
     * mem_map数组以页为单位记录内存段[0x100000, 0xffffff]的引用计数,
     * 即初始化内存段[0x100000, 0xffffff]的引用计数为USED。*/
    for (i=0 ; i<PAGING_PAGES ; i++)
        mem_map[i] = USED;

    /* 计算start_mem在扩展内存中的页偏移。
     * 计算内存段[start_mem, end_mem)的总页数。*/
    i = MAP_NR(start_mem);
    end_mem -= start_mem;
    end_mem >>= 12;

    /* 将内存段[start_mem, end_mem)(主存)的引用计数初始化为0。*/
    while (end_mem-->0)
        mem_map[i++]=0;
}
/* 在了解linux 0.11 对内存的分配及各段内存的用途之后,
 * 继mem_init之后, 再继续了解下本文件中的内存相关函数吧。
 *
 * 虽然linux 0.11最多只管理16Mb物理内存, 但通过页机制,
 * 可以将任意一个32位地址和主存中的一页物理内存形成映射关系,
 * 从而访问到实际的物理内存页。
 *
 * 在阅读后续函数时, 可在head.s中查看页目录, 页目录中的页表信息,
 * 页表, 页表项等数据结构及32位内存地址的页变换过程。*/

/* [2] calc_mem, 计算主存的使用情况。*/
void calc_mem(void)
{
    int i,j,k,free=0;
    long * pg_tbl;

    /* 根据mem_map统计空闲的内存页数 */
    for(i=0 ; i<PAGING_PAGES ; i++)
        if (!mem_map[i]) free++;
    printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);

    /* 从页表目录第3项开始计算已处于使用的页表,
     * 已使用页表中已被使用的页表项即已使用的内存页数。*/
    for(i=2 ; i<1024 ; i++) {
        if (1&pg_dir[i]) {
            pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
            for(j=k=0 ; j<1024 ; j++)
            if (pg_tbl[j]&1)
                k++;
            printk("Pg-dir[%d] uses %d pages\n",i,k);
        }
    }
}
