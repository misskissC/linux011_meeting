/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
/* fork.c 包含了 fork 系统调用的内核代码(见 system_call.s)和一些其他函数
 * (如 verify_area)。一旦掌握 fork 的关键点, fork还是挺简单的,但是内存管
 * 理程序就有些难以被理解,见 mm/mm.c中的copy_page_tables.*/
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

/* 用于保存新建进程id号 */
long last_pid=0;

/* verify_area,
 * 基于当前进程数据段基址dbase,以4Kb将内存段
 * [dbase + addr, dbase + addr + size)对齐。
 *
 * 然后写时拷贝所对齐内存段: 当前进程被创建时
 * 以页机制的方式共享父进程物理内存页。在当前
 * 进程写与父进程共享的内存页时, 则将该共享内
 * 存页内容拷贝到一块空闲未用的内存页中, 并将
 * 被拷贝内容的内存页映射到当前进程页表中。如
 * 果当前进程数据内存段引用计数为0,则表明该进
 * 程还未创建任何子进程,可写原内存段。*/
void verify_area(void * addr,int size)
{
    unsigned long start;

    /* 使得(addr + size)以4Kb对齐 */
    start = (unsigned long) addr;
    size += start & 0xfff;
    start &= 0xfffff000;
    
    /* 让addr的段基址为当前进程数据段基址 */
    start += get_base(current->ldt[2]);

    /* 以4Kb为单位写时拷贝内存段
     * [dbase + addr, dbase + addr + size) */
    while (size>0) {
        size -= 4096;
        write_verify(start);
        start += 4096;
    }
}

/* copy_mem,
 * 为(任务号为nr的)进程分配逻辑地址空间并设置在
 * 其LDT中,通过页机制共享父进程代码和数据内存段。*/
int copy_mem(int nr,struct task_struct * p)
{
    unsigned long old_data_base,new_data_base,data_limit;
    unsigned long old_code_base,new_code_base,code_limit;

    /* 根据选择符bit[2]=1时选择LDT段描述符, 0x0f和0x17分别为
     * LDT段描述符GDT[LDTR][1]和GDT[LDTR][2]的选择符即分别选
     * 中父进程LDT[1](代码段描述符)和LDT[2](数据段描述符)。
     *
     * 获取父进程代码段和数据段的基址。
     *
     * 父进程代码段和数据段的基址和限长见init_task赋值处, 初始进程
     * 代码段基址=数据段基址=0x0;代码段限长=数据段限长=0x9ffff。第
     * 2个用户进程的父进程为初始进程,所有用户进程LDT表内容将相同。*/
    code_limit=get_limit(0x0f);
    data_limit=get_limit(0x17);
    old_code_base = get_base(current->ldt[1]);
    old_data_base = get_base(current->ldt[2]);
    if (old_data_base != old_code_base)
        panic("We don't support separate I&D");
    if (data_limit < code_limit)
        panic("Bad data_limit");

    /* 进程逻辑地址空间为[nr * 0x4000000, (nr+1) * 0x4000000 - 1]每个
     * 进程的逻辑地址空间为64Mb,64进程逻辑空间共64 * 0x4000000 = 4Gb。
     * 将进程代码段和数据段逻辑基址设置到其LDT中。*/
    new_data_base = new_code_base = nr * 0x4000000;
    p->start_code = new_code_base;
    set_base(p->ldt[1],new_code_base);
    set_base(p->ldt[2],new_data_base);

    /* 将内存地址空间[old_data_base, old_data_base + data_limit]所对应页表
     * 拷贝到内存地址空间[new_data_base, new_data_base + data_limit]对应页
     * 表中。如此, 当访问[new_data_base, new_data_base + data_limit]中内存
     * 地址时,(根据页机制,见head.s)将访问到父进程代码和数据所在内存段。*/
    if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
        free_page_tables(new_data_base,data_limit);
        return -ENOMEM;
    }
    return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
/* ss
 * esp
 * flag
 * cs
 * eip
 * ds
 * es
 * fs
 * edx
 * ecx
 * ebx
 * call时入栈eip
 * gs
 * esi
 * edi
 * ebp
 * eax */
/* copy_process,
 * 创建子进程。
 *
 * nr  - 管理即将创建子进程结构体的下标;
 * ebp,...,gs为_sys_fork为其传递参数,这些寄存器值为父进程调用fork时的值;
 * ebx,..ds为_system_call所传递实参, 这些寄存器值为父进程调用fork时的值;
 * eip,...,ss为CPU执行父进程fork("int 80h指令")时往栈中写入的。*/
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
    long ebx,long ecx,long edx,
    long fs,long es,long ds,
    long eip,long cs,long eflags,long esp,long ss)
{
    struct task_struct *p;
    int i;
    struct file *f;

    /* 为管理进程结构体分配内存 */
    p = (struct task_struct *) get_free_page();
    if (!p)
        return -EAGAIN;
    task[nr] = p;

    /* 将管理父进程(当前进程)的结构体复制到管理子进程
     * 的结构体中, 并更改子进程不继承父进程结构体成员。*/
    *p = *current; /* NOTE! this doesn't copy the supervisor stack */
    p->state = TASK_UNINTERRUPTIBLE;
    p->pid = last_pid; /* 子进程id(见find_empty_process) */
    p->father = current->pid; /* 父进程id */
    p->counter = p->priority; /* 进程初始时间片为其优先级时间片 */
    p->signal = 0; /* 无处理信号 */
    p->alarm = 0;  /* 无报警超时 */
    p->leader = 0; /* process leadership doesn't inherit */
    p->utime = p->stime = 0;   /* 进程运行时间 */
    p->cutime = p->cstime = 0; /* 其子进程运行时间 */
    p->start_time = jiffies;   /* 进程诞生时间(片) */
    p->tss.back_link = 0;      /* 上一个进程TSS选择符 */
    p->tss.esp0 = PAGE_SIZE + (long) p; /* 子进程内核态下的栈顶, 见struct union task */
    p->tss.ss0 = 0x10; /* 子进程内核态栈段 */
    /* 回看栈中内容吧: eip=fork函数中"if (__res >= 0)"处偏移地址;
     * fork函数中有提到, 父进程通过"int 80h"指令完成系统调用后将
     * 执行"if (__res >= 0)"处及后续语句;此处亦将"if (__res >= 0)"
     * 语句处偏移地址赋给子进程TSS.eip,当子进程被调度运行时也会执
     * "if (__res >= 0)"处及后续语句,所以fork函数会在父子进程中各
     * 返回1次。
     *
     * 另外,为了不让RET语句破坏子进程正确的栈内容,fork函数需为内联
     * 函数(内联函数指令被直接嵌在被调用处,无CALL-RET对栈暗中操作)。*/
    p->tss.eip = eip;
    /* 使用栈中备份的标志寄存器,真实标志寄存器TF&&IF位已置0 */
    p->tss.eflags = eflags;
    /* 当调度子进程运行时, eax=TSS.eax,
     * eax将作为"fork返回值":__res=eax,
     * 这也是fork在子进程中返回0的原因。*/
    p->tss.eax = 0;
    /* 子进程各寄存器继承父进程调fork之前各寄存器值 */
    p->tss.ecx = ecx;
    p->tss.edx = edx;
    p->tss.ebx = ebx;
    p->tss.esp = esp;
    p->tss.ebp = ebp;
    p->tss.esi = esi;
    p->tss.edi = edi;
    p->tss.es = es & 0xffff;
    p->tss.cs = cs & 0xffff;
    p->tss.ss = ss & 0xffff;
    p->tss.ds = ds & 0xffff;
    p->tss.fs = fs & 0xffff;
    p->tss.gs = gs & 0xffff;
    p->tss.ldt = _LDT(nr); /* 将本进程LDT选择符赋予TSS.ldt字段 */
    p->tss.trace_bitmap = 0x80000000; /* I/O许可位图偏移 */
    /* 若父进程使用了协处理器,则清CR0TS标志并将协处理器转态备份 */
    if (last_task_used_math == current)
        __asm__("clts ; fnsave %0"::"m" (p->tss.i387));

    /* 通过页机制将[nr * 0x4000000, (nr + 1) * 0x4000000)内存地址空间映
     * 射父进程数据和代码内存段,并将基址nr*0x4000000设置在子进程的LDT中。*/
    if (copy_mem(nr,p)) {
        task[nr] = NULL;
        free_page((long) p);
        return -EAGAIN;
    }
    
    /* 因*p = *current,子进程已继承父进程所打开文
     * 件和各目录i节点,此处增加共享对象的引用计数。*/
    for (i=0; i<NR_OPEN;i++)
        if (f=p->filp[i])
            f->f_count++;
    if (current->pwd)
        current->pwd->i_count++;
    if (current->root)
        current->root->i_count++;
    if (current->executable)
        current->executable->i_count++;
    
    /* 在GDT中为新建进程设置TSS和LDT,可以再结合schedule
     * 函数粗略理解进程切换过程: _TSS(nr)将得到task[nr]
     * 进程TSS的选择符, 使用ljmp指令跳转GDT[_TSS(nr)]时
     * CPU会将_TSS(nr)加载到TR并由此访问到进程TSS即获取
     * 到进程运行上下文;随后将进程LDT选择符GDT[TSS.ldt]
     * 加载到LDTR并访问进程LDT,即由此访问到由LDT所描述代
     * 码和数据内存段,由此运行task[nr]所管理的进程。*/
    set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
    set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
    p->state = TASK_RUNNING; /* do this last, just in case */
    
    /* 再继续跟踪下fork函数的返回流程吧,直到main调用fork处。
     * --> _sys_fork --> _system_call --> fork
     * last_pid将被返回到父进程中,fork在子进程中的返回值为0。
     *
     * 似乎反复跟踪fork执行过程才是理解fork函数的真实渠道哦。*/
    return last_pid;
}

/* find_empty_process,
 * 为新进程编译唯一进程id和空闲未用进程结构体。
 * 该函数由_sys_fork调用。*/
int find_empty_process(void)
{
    int i;

    /* 全局变量last_pid用于记录新进程id */
    repeat:
        if ((++last_pid)<0) last_pid=1;
        for(i=0 ; i<NR_TASKS ; i++)
            if (task[i] && task[i]->pid == last_pid) goto repeat;
    /* 在进程管理结构体中遍历空闲元素并返回空闲元素下标 */
    for(i=1 ; i<NR_TASKS ; i++)
        if (!task[i])
            return i;
    return -EAGAIN;
}
