/*
 * malloc.c --- a general purpose kernel memory allocator for Linux.
 * 
 * Written by Theodore Ts'o (tytso@mit.edu), 11/29/91
 *
 * This routine is written to be as fast as possible, so that it
 * can be called from the interrupt level.
 *
 * Limitations: maximum size of memory we can allocate using this routine
 *  is 4k, the size of a page in Linux.
 *
 * The general game plan is that each page (called a bucket) will only hold
 * objects of a given size.  When all of the object on a page are released,
 * the page can be returned to the general free pool.  When malloc() is
 * called, it looks for the smallest bucket size which will fulfill its
 * request, and allocate a piece of memory from that bucket pool.
 *
 * Each bucket has as its control block a bucket descriptor which keeps 
 * track of how many objects are in use on that page, and the free list
 * for that page.  Like the buckets themselves, bucket descriptors are
 * stored on pages requested from get_free_page().  However, unlike buckets,
 * pages devoted to bucket descriptor pages are never released back to the
 * system.  Fortunately, a system should probably only need 1 or 2 bucket
 * descriptor pages, since a page can hold 256 bucket descriptors (which
 * corresponds to 1 megabyte worth of bucket pages.)  If the kernel is using 
 * that much allocated memory, it's probably doing something wrong.  :-)
 *
 * Note: malloc() and free() both call get_free_page() and free_page()
 *  in sections of code where interrupts are turned off, to allow
 *  malloc() and free() to be safely called from an interrupt routine.
 *  (We will probably need this functionality when networking code,
 *  particularily things like NFS, is added to Linux.)  However, this
 *  presumes that get_free_page() and free_page() are interrupt-level
 *  safe, which they may not be once paging is added.  If this is the
 *  case, we will need to modify malloc() to keep a few unused pages
 *  "pre-allocated" so that it can safely draw upon those pages if
 *  it is called from an interrupt routine.
 *
 *  Another concern is that get_free_page() should not sleep; if it 
 *  does, the code is carefully ordered so as to avoid any race 
 *  conditions.  The catch is that if malloc() is called re-entrantly, 
 *  there is a chance that unecessary pages will be grabbed from the 
 *  system.  Except for the pages for the bucket descriptor page, the 
 *  extra pages will eventually get released back to the system, though,
 *  so it isn't all that bad.
 */
/*
 * malloc.c - Linux内核内存分配通用工具。
 * 由Theodore Ts'o于11/29/91编写。
 *
 * 本程序被尽可能地编写得能快速执行,这样就可以在中断层面使用本程序。
 *
 * 限制: 本程序可分配的最大内存空间为4Kb即Linux中一页内存大小。
 *
 * 在本程序中,将系统可用内存看作一个内存池,通过 get_free_page()从内
 * 存池中获取到的空闲内存页会被分隔成指定大小内存块-桶。即1内存页会
 * 被分割为特定数量的桶, 当内存页上的桶都空闲时则释放该内存页到系统
 * 内存池中。malloc()将会从桶目录数据结构体中寻找能满足所请求内存大
 * 小的最小桶并返回。
 *
 * 每个桶都有一个桶描述符与其对应,桶描述符同时将会记录内存页中桶的分
 * 配和释放情况。桶描述符也存储在由 get_free_page() 分配来的内存页中。
 * 与桶所在的内存页不同的是, 用作桶描述符的内存不会被释放。由于1页内
 * 存能够容纳256个桶描述符, 所以系统只会使用1到2个内存页用作桶描述符。
 * 但若在内核中过多使用malloc()来分配内存,有可能会出错。
 *
 * 注: 在 malloc() 和 free() 分别调用 get_free_page() 和 free_page() 时
 * 需禁止CPU对中断的响应,以使得 malloc() 和 free() 能在中断处理程序中被
 * 安全调用(在诸如网络编程中可能需要这个功能,尤其像NFS)。当没有加入分页
 * 机制时,get_free_page() 和 free_page() 似乎也是可在中断程序中被调用的。
 * 若加入了分页机制,则需修改 malloc() 以预分配几页内存以在中断程序中使用。
 *
 * 还有一点, 在调用 get_free_page()时不应进入睡眠,如果发生了睡眠,应仔细安
 * 排编码以防止竞争的发生。若 malloc() 能被重入就增加了能从系统获取内存页
 * 的情况。之前提到,除了桶描述符所占内存外,其余内存在用完之后都会被释放回
 * 系统,所以 malloc() 可重入特点也并非完全不可取。*/

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

/* struct bucket_desc,
 * 桶描述符结构体类型。*/
struct bucket_desc { /* 16 bytes */
    void                *page;  /* 桶描述符对应的内存页 */
    struct bucket_desc  *next;  /* 指向下一个桶描述符 */
    void                *freeptr;    /* 指向当前内存页空闲桶 */
    unsigned short      refcnt;      /* 计数内存页中被分配的桶数 */
    unsigned short      bucket_size; /* 本桶描述符对应桶大小 */
};

/* struct _bucket_dir,
 * 描述特定大小桶的结构体类型。*/
struct _bucket_dir { /* 8 bytes */
    int                 size;   /* 桶大小 */
    struct bucket_desc  *chain; /* 桶描述符(链表头)指针 */
};

/*
 * The following is the where we store a pointer to the first bucket
 * descriptor for a given size.  
 *
 * If it turns out that the Linux kernel allocates a lot of objects of a
 * specific size, then we may want to add that specific size to this list,
 * since that will allow the memory to be allocated more efficiently.
 * However, since an entire page must be dedicated to each specific size
 * on this list, some amount of temperance must be exercised here.
 *
 * Note that this list *must* be kept in order.
 */
/* 在以下结构体数组中设置指向特定大小桶的初始指针。
 * 
 * 如果程序发现某特定大小的桶将会被大量分配,本程序
 * 会将该大小桶的链表添加到以下数组中,在该数组中分
 * 配内存将会更加高效。然而,由于在链表中一页内存会
 * 被分割成指定大小的块-桶,所以需评估或调试下桶大小。
 *
 * 注,以下个元素 必须 以桶大小的升序排列,这样才能以
 * 保证调用 malloc() 申请内存时,以最小桶匹配申请。*/
struct _bucket_dir bucket_dir[] = {
    { 16,   (struct bucket_desc *) 0},
    { 32,   (struct bucket_desc *) 0},
    { 64,   (struct bucket_desc *) 0},
    { 128,  (struct bucket_desc *) 0},
    { 256,  (struct bucket_desc *) 0},
    { 512,  (struct bucket_desc *) 0},
    { 1024, (struct bucket_desc *) 0},
    { 2048, (struct bucket_desc *) 0},
    { 4096, (struct bucket_desc *) 0},
    { 0,    (struct bucket_desc *) 0}};   /* End of list marker */

/*
 * This contains a linked list of free bucket descriptor blocks
 */
/* 指向空闲桶描述符链表头部 */
struct bucket_desc *free_bucket_desc = (struct bucket_desc *) 0;

/*
 * This routine initializes a bucket description page.
 */
/* 该函数初始化一内存页用作桶描述符 */
/* init_bucket_desc,
 * 分配一页内存用作桶描述符链表。*/
static inline void init_bucket_desc()
{
    struct bucket_desc *bdesc, *first;
    int i;

    /* 分配一空闲内存页用作桶描述符链表 */
    first = bdesc = (struct bucket_desc *) get_free_page();
    if (!bdesc)
        panic("Out of memory in init_bucket_desc()");
    for (i = PAGE_SIZE/sizeof(struct bucket_desc); i > 1; i--) {
        bdesc->next = bdesc+1;
        bdesc++;
    }
    
    /*
     * This is done last, to avoid race conditions in case 
     * get_free_page() sleeps and this routine gets called again....
     */
    /* 该语句用于避免 get_free_page() 睡眠
     * 或本程序再次被调用时产生竞争条件。*/
    bdesc->next = free_bucket_desc;
    free_bucket_desc = first;
}

/* malloc,
 * 申请指定大小即len字节内存,通过该函
 * 数所申请到的内存大小将大于等于len。*/
void *malloc(unsigned int len)
{
    struct _bucket_dir  *bdir;
    struct bucket_desc  *bdesc;
    void    *retval;

    /*
    * First we search the bucket_dir to find the right bucket change
    * for this request.
    */
    /* 首先,从同目录 bucket_dir 数组中找到包含刚好能匹配len大小的桶的内存页 */
    for (bdir = bucket_dir; bdir->size; bdir++)
        if (bdir->size >= len)
            break;
    if (!bdir->size) {
        printk("malloc called with impossibly large argument (%d)\n",
            len);
        panic("malloc: bad arg");
    }
    
    /*
    * Now we search for a bucket descriptor which has free space
    */
    /* 在搜索到包含刚好能匹配len字节桶的内存页后,接下来在该内
     * 存页中搜索空闲桶(禁止CPU处理当前进程中断以避免竞争)。*/
    cli(); /* Avoid race conditions */
    for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) 
        if (bdesc->freeptr)
            break;
    /*
     * If we didn't find a bucket with free space, then we'll 
     * allocate a new one.
     */
    /* 若内存页中已无空闲桶,则新分配一页内存并分隔成指定大小的桶 */
    if (!bdesc) {
        char *cp;
        int  i;

        /* 若还未初始化桶描述符则初始化 */
        if (!free_bucket_desc)
            init_bucket_desc();

        /* 使用 free_bucket_desc 当前所指向空闲桶描述符管理内存页中的桶 */
        bdesc = free_bucket_desc;
        free_bucket_desc = bdesc->next;
        bdesc->refcnt = 0;
        bdesc->bucket_size = bdir->size;
        bdesc->page = bdesc->freeptr = (void *) cp = get_free_page();
        if (!cp)
            panic("Out of memory in kernel malloc()");
        
        /* Set up the chain of free objects */
        /* 将内存页分成指定大小的内存块(桶),
         * 每个桶的头部存储着下一个桶的地址。*/
        for (i=PAGE_SIZE/bdir->size; i > 1; i--) {
            *((char **) cp) = cp + bdir->size;
            cp += bdir->size;
        }
        /* 最后一个桶的头部置0即表示无下一个桶 */
        *((char **) cp) = 0;
        /* 将含指定大小桶的内存页附加到相应桶大小的桶目录元素中 */
        bdesc->next = bdir->chain; /* OK, link it in! */
        bdir->chain = bdesc;
    }
    /* retval值为当前空闲桶地址 */
    retval = (void *) bdesc->freeptr;
    /* 将桶描述符中空闲桶指针元素指向内存页中的下一个空闲桶 */
    bdesc->freeptr = *((void **) retval);
    bdesc->refcnt++;
    sti(); /* OK, we're safe again */
    return(retval);
}

/*
 * Here is the free routine.  If you know the size of the object that you
 * are freeing, then free_s() will use that information to speed up the
 * search for the bucket descriptor.
 * 
 * We will #define a macro so that "free(x)" is becomes "free_s(x, 0)"
 */
/* 以下是释放 malloc() 所申请内存的函数。
 * 若调用者知道所释放内存的大小, free_s() 将会根据该信息更快地搜索到该内存
 * 块(桶)的桶描述符。kernel.h 中定义了宏值为 free_s(x,0) 的宏 free(x)。当
 * size为0时,free_s将会依次遍历桶目录中各个桶大小的内存块,直到找到目标内存。*/

/* free_s,
 * 释放基址为obj的内存块。*/
void free_s(void *obj, int size)
{
    void *page;
    struct _bucket_dir *bdir;
    struct bucket_desc *bdesc, *prev;

    /* Calculate what page this object lives in */
    /* 首先计算obj内存块所属内存页 */
    page = (void *)  ((unsigned long) obj & 0xfffff000);

    /* Now search the buckets looking for that page */
    /* 然后在桶目录中搜索obj所属内存页 */
    for (bdir = bucket_dir; bdir->size; bdir++) {
        prev = 0;
        /* If size is zero then this conditional is always false */
        if (bdir->size < size)
            continue;
        /* 在桶大小刚好大于或等于size的内存页中寻找页机制为page的内存页 */
        for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) {
            if (bdesc->page == page) 
                goto found;
            prev = bdesc;
        }
    }
    panic("Bad address passed to kernel free_s()");
    
found:
    cli(); /* To avoid race conditions */
    /* 桶描述符桶空闲指针指向刚释放桶,释放桶头部指向原当前空闲桶,
     * 减少桶被分配计数,若内存页中无被分配桶则将该内存页释放。*/
    *((void **)obj) = bdesc->freeptr;
    bdesc->freeptr = obj;
    bdesc->refcnt--;
    if (bdesc->refcnt == 0) {
        /*
         * We need to make sure that prev is still accurate.  It
         * may not be, if someone rudely interrupted us....
         */
        /* 确保 prev 指向的下个桶描述符为刚释放桶的桶描述符 */
        if ((prev && (prev->next != bdesc)) ||
            (!prev && (bdir->chain != bdesc)))
                for (prev = bdir->chain; prev; prev = prev->next)
                    if (prev->next == bdesc)
                        break;
        /* 释放bdesc指向的桶分配数为0的内存页 */
        if (prev)
            prev->next = bdesc->next;
        else {
            if (bdir->chain != bdesc)
                panic("malloc bucket chains corrupted");
            bdir->chain = bdesc->next;
        }
        free_page((unsigned long) bdesc->page);

        /* 更新当前空闲桶描述符指针的值,让刚空闲下来
         * 的桶描述符指针充当空闲桶描述符表头元素。*/
        bdesc->next = free_bucket_desc;
        free_bucket_desc = bdesc;
    }
    sti();
    return;
}

/* 粗略理解内存页桶式分配过程。
 * 
 * 分配一页内存用作桶描述符(struct bucket_desc)
 * |-----------------|
 * | 0 | 1 | ... |255|
 * |-----------------|
 * bdesc
 *   ^
 *   |
 * free_bucket_desc
 * 
 * 设 malloc(2047) 被调用了3次,则 bucket_dir 数组情况大体如下
 * |=====|
 * | 16  |
 * |-----|  second page            first page
 * |  0  |  |---------|           |---------|
 * |=====|  |    |    |           |    |    |
 * |.....|  |---------|           |---------|
 * |=====|  m2<--|                m1<--|
 * |2048 |       |                     |
 * |-----|     |-------|           |-------|
 * |chain| --> | page  |           | page  |
 * |=====|     | next  | --------> | next  |--> NULL
 * |.....|     |freeptr|->m2+2048  |freeptr|--> NULL
 * |=====|     |.......|           |.......|
 * |0    |     bdesc[1]            bdesc[0]
 * |-----|
 * |0    |
 * |=====|
 * 
 * 分配两个桶描述符管理内存页中的桶之后
 * |---------------------|
 * | 0 | 1 | 2 | ... |255|
 * |---------------------|
 * bdesc    ^
 *          |
 *          |
 *   free_bucket_desc */



