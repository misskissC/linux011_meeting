#ifndef _STRING_H_
#define _STRING_H_

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

extern char * strerror(int errno);

/*
 * This string-include defines all string functions as inline
 * functions. Use gcc. It also assumes ds=es=data space, this should be
 * normal. Most of the string-functions are rather heavily hand-optimized,
 * see especially strtok,strstr,str[c]spn. They should work, but are not
 * very easy to understand. Everything is done entirely within the register
 * set, making the functions fast and clean. String instructions have been
 * used through-out, making for "slightly" unclear code :-)
 *
 * (C) 1991 Linus Torvalds
 *
 * 该头文件以内联函数的方式定义了所有的字符串操作函数,需用gcc编译。这些函数
 * 适用于ds和es都指向同一个数据段的场景。本文件在代码层面就优化了大多数字符
 * 串函数,见strtok,strstr,str[c]spn函数。这些经优化的函数的运行效率更高, 但
 * 他们的代码会更难被理解一些。在寄存器层面完成所有的功能使得他们的运行更高
 * 效,机器码也更简洁。不过,其中的一些字符串操作指令,可能会降低代码的可读性。
 */

/* strcpy,
 * 将src所指内存段中的字符串拷贝到dest所指内存段中,返回dest。*/
extern inline char * strcpy(char * dest,const char *src)
{
/* 内联汇编输入。
 * "S" (src), esi=src;
 * "D" (dest),edi=dest;
 * 
 * 内联汇编指令。
 * cld 1: lodsb stosb 
 * do {
 *     movb ds:esi, al // lodsb
 *     movb al, es:edi // stosb
 *     esi++;edi++     // cld
 * } while (al != 0);  // testb %%al, %%al jne 1b
 * 将ds:esi所指字符串逐字节拷贝到es:edi所指内存段中。*/
__asm__("cld\n"
    "1:\tlodsb\n\t"
    "stosb\n\t"
    "testb %%al,%%al\n\t"
    "jne 1b"
    ::"S" (src),"D" (dest):"si","di","ax");
return dest;
}

/* strncpy,
 * 最多(遇src内存段中的0则结束)将src所指内存段中前count字节拷贝到dest所指内存中。*/
extern inline char * strncpy(char * dest,const char *src,int count)
{
/* 内联汇编输入。
 * "S" (src), esi=src;
 * "D" (dest),edi=dest;
 * "c" (count),ecx=count。
 *
 * 内联汇编语句。
 * 1:decl %2 js 2f,ecx自减,若ecx小于0则向前跳转到标号2处;
 * 
 * cld 1: decl %2 lodsb stosb 
 * do {
 *     movb ds:esi, al // lodsb
 *     movb al, es:edi // stosb
 *     esi++;edi++     // cld
 * } while (al != 0);  // testb %%al, %%al jne 1b
 * 
 * rep stosb
 * x=ecx
 * while (x--) {
 *     movb al, es:edi
 *     edi++
 * }
 * 逐字节拷贝src所指内存中的字符串到dest所指内存段中,
 * 当count小于src所指字符串长度时,则拷贝src前count字
 * 符到dest所指内存中;当count大于src所指字符串长度时,
 * 则将src所指字符串拷贝到dest所指内存后,将dest剩余的
 * count - (字符串长度+1)的空间补0。*/
__asm__("cld\n"
    "1:\tdecl %2\n\t"
    "js 2f\n\t"
    "lodsb\n\t"
    "stosb\n\t"
    "testb %%al,%%al\n\t"
    "jne 1b\n\t"
    "rep\n\t"
    "stosb\n"
    "2:"
    ::"S" (src),"D" (dest),"c" (count):"si","di","ax","cx");
return dest;
}

/* strcat,
 * 将src所指字符串附加到dest所指字符串的末尾。*/
extern inline char * strcat(char * dest,const char * src)
{
/* 内联汇编输入。
 * "S" (src), esi=src;
 * "D" (dest),edi=dest;
 * "a" (0),   eax=0;
 * "c" (0xffffffff),ecx=0xffffffff。
 *
 * 内联汇编语句。
 * cld repne scasb
 * while (ecx-- != 0 && eflag.ZF == 0) {
 *     if (es:edi-- == al)
 *         eflag.ZF = 1
 *     ecx--
 *     edi++
 * }
 * 在es:edi所指内存段逐字节搜索直到搜索到al(0)。
 * 
 * decl %1,将edi自减以指向es:edi内存段中的0。
 *
 * cld 1: lodsb stosb
 * do {
 *     movb ds:esi, al // lodsb
 *     movb al, es:edi // stosb
 *     esi++;edi++     // cld
 * } while (al != 0);  // testb %%al, %%al jne 1b
 * 将src所指内存段中的字符串拷贝到es:edi所指内存段中。*/
__asm__("cld\n\t"
    "repne\n\t"
    "scasb\n\t"
    "decl %1\n"
    "1:\tlodsb\n\t"
    "stosb\n\t"
    "testb %%al,%%al\n\t"
    "jne 1b"
    ::"S" (src),"D" (dest),"a" (0),"c" (0xffffffff):"si","di","ax","cx");
return dest;
}

/* strncat,
 * 最多(遇到src所指内存中的0则结束)将src所指内存段的count字节附加到dest所指内存段中。*/
extern inline char * strncat(char * dest,const char * src,int count)
{
/* "S" (src),esi=src;
 * "D" (dest),edi=dest;
 * "a" (0),eax=0;
 * "c" (0xffffffff),ecx=0xffffffff;
 * "g" (count),若有空闲寄存器则用该寄存器存count值,否则直接使用内存变量count。
 *
 * 内联汇编语句。
 * cld repne scasb
 * while (ecx-- != 0 && eflag.ZF == 0) {
 *     if (es:edi-- == al)
 *         eflag.ZF = 1
 *     ecx--
 *     edi++
 * }
 * 在es:edi所指内存段逐字节搜索直到搜索到al(0)。
 * 
 * decl %1,将edi自减以指向es:edi内存段中的0。
 *
 * movl %4, %3, ecx=count。
 * decl %3 js 2f, if (--ecx < 0) goto 2,若ecx自减后为负数则向前跳转到标号2处;
 *
 * cld 1: decl %3 lodsb stosb 
 * do {
 *     movb ds:esi, al // lodsb
 *     movb al, es:edi // stosb
 *     esi++;edi++     // cld
 * } while (al != 0);  // testb %%al, %%al jne 1b
 * 
 * xorl %2,%2 stosb
 * 若count大于src所指字符串长度则将dest后续空闲内存补0。*/
__asm__("cld\n\t"
    "repne\n\t"
    "scasb\n\t"
    "decl %1\n\t"
    "movl %4,%3\n"
    "1:\tdecl %3\n\t"
    "js 2f\n\t"
    "lodsb\n\t"
    "stosb\n\t"
    "testb %%al,%%al\n\t"
    "jne 1b\n"
    "2:\txorl %2,%2\n\t"
    "stosb"
    ::"S" (src),"D" (dest),"a" (0),"c" (0xffffffff),"g" (count)
    :"si","di","ax","cx");
return dest;
}

/* 用文字描述难以与每个汇编语句对应,尝试用伪C语言注释一下后续函数 */

/* strcmp,
 * 比较ct和cs指向的字符串,若两个字符串相等返回0。*/
extern inline int strcmp(const char * cs,const char * ct)
{
register int __res __asm__("ax");

/* 内联汇编输入。
 * "D" (cs),edi=cs;
 * "S" (ct),esi=ct。
 *
 * 内联汇编输出。
 * __res = eax。*/
__asm__("cld\n"
    "1:\tlodsb\n\t"         /* movb ds:esi++, al */
    "scasb\n\t"             /* if (al != es:edi) */
    "jne 2f\n\t"            /*     goto 2        */
    "testb %%al,%%al\n\t"   /* if(al != 0)       */
    "jne 1b\n\t"            /*     goto 1        */
    "xorl %%eax,%%eax\n\t"  /* eax = 0           */
    "jmp 3f\n"              /* goto 3            */
    "2:\tmovl $1,%%eax\n\t" /* eax = 1           */
    "jl 3f\n\t"             /* if (*ct < *cs) goto 3 */
    "negl %%eax\n"          /* eax=-eax          */
    "3:"
    :"=a" (__res):"D" (cs),"S" (ct):"si","di");
return __res;
}

/* strncmp,
 * 比较cs和ct所指字符串的前count字节,相等则返回0。*/
extern inline int strncmp(const char * cs,const char * ct,int count)
{
register int __res __asm__("ax");

/* 内联汇编输入。
 * "D" (cs),edi=cs;
 * "S" (ct),esi=ct;
 * "c" (count),ecx=count。
 *
 * 内联汇编输出。
 * __res = eax。*/
__asm__("cld\n"
    "1:\tdecl %3\n\t"           /* if (--ecx < 0) */
    "js 2f\n\t"                 /*     goto 2     */
    "lodsb\n\t"                 /* movb ds:esi++, al */
    "scasb\n\t"                 /* if (al != es:edi) */
    "jne 3f\n\t"                /*     goto 3        */
    "testb %%al,%%al\n\t"       /* if (al != 0)  */
    "jne 1b\n"                  /*     goto 1    */
    "2:\txorl %%eax,%%eax\n\t"  /* eax = 0  */
    "jmp 4f\n"
    "3:\tmovl $1,%%eax\n\t"     /* eax = 1*/
    "jl 4f\n\t"                 /* if (ct[ecx] > cs[ecx]) goto 4 */
    "negl %%eax\n"              /* eax = -1 */
    "4:"
    :"=a" (__res):"D" (cs),"S" (ct),"c" (count):"si","di","cx");
return __res;
}

/* strchr,
 * 在s所指字符串串中查找字符c,若找到则返
 * 回字符c首次在s位置处的地址,否则返回0。*/
extern inline char * strchr(const char * s,char c)
{
register char * __res __asm__("ax");

/* 内联汇编输入。
 * esi = s; eax = c.
 *
 * 内联汇编输出。
 * __res = eax。*/
__asm__("cld\n\t"
    "movb %%al,%%ah\n"      /* ah = al = c       */
    "1:\tlodsb\n\t"         /* movb ds:esi++, al */
    "cmpb %%ah,%%al\n\t"    /* if (ah == al)     */
    "je 2f\n\t"             /*     goto 2        */
    "testb %%al,%%al\n\t"   /* if (al != 0)      */
    "jne 1b\n\t"            /*     goto 1        */
    "movl $1,%1\n"          /* esi = 1           */
    "2:\tmovl %1,%0\n\t"    /* eax=esi           */
    "decl %0"               /* eax--             */
    :"=a" (__res):"S" (s),"0" (c):"si");
return __res;
}

/* strrchr,
 * 在s所指字符串中查找字符c,返回c在s中最后出现
 * 位置的地址。若cs所指字符串不包含字符c则返回0。*/
extern inline char * strrchr(const char * s,char c)
{
register char * __res __asm__("dx");

/* 内联汇编输入。
 * "0" (0),edx=0;
 * "S" (s),esi=s;
 * "a" (c),eax=c。
 * 
 * 内联汇编输出。
 * __res = edx。*/
__asm__("cld\n\t"
    "movb %%al,%%ah\n"          /* ah = al = c       */
    "1:\tlodsb\n\t"             /* movb ds:esi++, al */
    "cmpb %%ah,%%al\n\t"        /* if (ah != al)     */
    "jne 2f\n\t"                /*     goto 2        */
    "movl %%esi,%0\n\t"         /* mov esi, edx      */
    "decl %0\n"                 /* edx--             */
    "2:\ttestb %%al,%%al\n\t"   /* if (al != 0)      */
    "jne 1b"                    /*     goto 1        */
    :"=d" (__res):"0" (0),"S" (s),"a" (c):"ax","si");
return __res;
}

/* strspn,
 * 从cs所指字符串首字符开始,查看该字符是否存在于ct中。
 * 该函数返回连续存在于ct中字符的个数。*/
extern inline int strspn(const char * cs, const char * ct)
{
register char * __res __asm__("si");

/* 内联汇编输入。
 * "a" (0),eax=0;
 * "c" (0xffffffff),ecx=0xffffffff;
 * "0" (cs),esi=cs;
 * "g" (ct),若还有空闲寄存器则该寄存器=ct,否则引用内存变量ct本身。
 * 
 * 内联汇编输出。
 * "=S" (__res),__res=esi。*/
__asm__("cld\n\t"
    "movl %4,%%edi\n\t"     /* edi = ct */
    "repne\n\t"             /* while (ecx--) */
    "scasb\n\t"             /*     if (es:edi++ == al) break */
    "notl %%ecx\n\t"        /* ecx ~= ecx   */
    "decl %%ecx\n\t"        /* ecx--        */
    "movl %%ecx,%%edx\n"    /* edx = ecx 即ct字符串长度 */
    "1:\tlodsb\n\t"         /* movb ds:esi++, al */
    "testb %%al,%%al\n\t"   /* if (al == 0)      */
    "je 2f\n\t"             /*     goto 2        */
    "movl %4,%%edi\n\t"     /* edi = ct          */
    "movl %%edx,%%ecx\n\t"  /* ecx = edx         */
    "repne\n\t"             /* while (ecx--)     */
    "scasb\n\t"             /*     if (es:edi++ == al) break */
    "je 1b\n"               /* if (es:(edi-1) == al) goto 1  */
    "2:\tdecl %0"           /* esi--            */
    :"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
    :"ax","cx","dx","di");
return __res-cs;
}

/* strcspn,
 * 从cs所指字符串首字符开始,查看该字符是否存在于ct中。
 * 该函数返回连续不存在于ct中字符的个数。*/
extern inline int strcspn(const char * cs, const char * ct)
{
register char * __res __asm__("si");

/* 内联汇编输入。
 * "a" (0),eax=0;
 * "c" (0xffffffff),ecx=0xffffffff;
 * "0" (cs),esi=cs;
 * "g" (ct),若还有空闲寄存器则该寄存器=ct,否则引用内存变量ct本身。
 *
 * 内联汇编输出。
 * "=S" (__res),__res=esi。*/
__asm__("cld\n\t"
    "movl %4,%%edi\n\t"     /* edi = ct           */
    "repne\n\t"             /* while (ecx--)      */
    "scasb\n\t"             /*     if (es:edi++ == al) break; */
    "notl %%ecx\n\t"        /* ecx ~= ecx         */
    "decl %%ecx\n\t"        /* ecx--              */
    "movl %%ecx,%%edx\n"    /* edx = ecx 即ct长度 */
    "1:\tlodsb\n\t"         /* al = ds:esi++      */
    "testb %%al,%%al\n\t"   /* if (al == 0)       */
    "je 2f\n\t"             /*     goto 2         */
    "movl %4,%%edi\n\t"     /* edi = ct           */
    "movl %%edx,%%ecx\n\t"  /* ecx = edx          */
    "repne\n\t"             /* while (ecx--)      */
    "scasb\n\t"             /*     if (es:edi++ == al) break; */
    "jne 1b\n"              /* if (es:(edi-1) != al) goto 1   */
    "2:\tdecl %0"           /* esi--              */
    :"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
    :"ax","cx","dx","di");
return __res-cs;
}

/* strpbrk,
 * 从cs所指字符串首字符开始,查看该字符是否属于ct所指字符中任一字符。
 * 该函数返回cs串中首次出现ct串中任一字符时该字符的地址,若ct串中没
 * 一个字符属于cs串则返回空。*/
extern inline char * strpbrk(const char * cs,const char * ct)
{
register char * __res __asm__("si");

/* 内联汇编输入。
 * "a" (0),eax=0;
 * "c" (0xffffffff),ecx=0xffffffff;
 * "0" (cs),esi=cs;
 * "g" (ct),若还有空闲寄存器则该寄存器=ct,否则引用内存变量ct本身。
 *
 * 内联汇编输出。
 * "=S" (__res),__res=esi。*/
__asm__("cld\n\t"
    "movl %4,%%edi\n\t"     /* edi = ct */
    "repne\n\t"             /* while (ecx--) */
    "scasb\n\t"             /*    if (es:edi++ == al) break; */
    "notl %%ecx\n\t"        /* ecx ~= ecx        */
    "decl %%ecx\n\t"        /* ecx--             */
    "movl %%ecx,%%edx\n"    /* edx = ecx即ct长度 */
    "1:\tlodsb\n\t"         /* al=ds:esi++       */
    "testb %%al,%%al\n\t"   /* if (al == 0)      */
    "je 2f\n\t"             /*     goto 2        */
    "movl %4,%%edi\n\t"     /* edi = ct          */
    "movl %%edx,%%ecx\n\t"  /* ecx = edx         */
    "repne\n\t"             /* while (ecx--)     */
    "scasb\n\t"             /*    if (es:edi++ != al) */
    "jne 1b\n\t"            /*        goto 1          */
    "decl %0\n\t"           /* esi--                  */
    "jmp 3f\n"              /* goto 3                 */
    "2:\txorl %0,%0\n"      /* esi = 0                */
    "3:"
    :"=S" (__res):"a" (0),"c" (0xffffffff),"0" (cs),"g" (ct)
    :"ax","cx","dx","di");
return __res;
}

/* strstr,
 * 在cs所指字符串中搜索ct所指字符串,若搜索到
 * 则返回ct在cs中首次出现的地址,否则返回NULL。*/
extern inline char * strstr(const char * cs,const char * ct)
{
register char * __res __asm__("ax");

/* 内联汇编输入。
 * "0" (0),eax=0;
 * "c" (0xffffffff),ecx=0xffffffff;
 * "S" (cs),esi=cs;
 * "g" (ct),若还有空闲寄存器则该寄存器=ct,否则引用内存变量ct本身。
 *
 * 内联汇编输出。
 * "=a" (__res),__res=eax。*/
__asm__("cld\n\t" \
    "movl %4,%%edi\n\t"     /* edi = ct */
    "repne\n\t"             /* while (ecx--)                 */
    "scasb\n\t"             /*     if (es:edi++ == al) break */
    "notl %%ecx\n\t"        /* ecx ~= ecx                    */
    "decl %%ecx\n\t"        /* ecx-- NOTE! This also sets Z if searchstring='' */
    "movl %%ecx,%%edx\n"    /* edx = ecx (length of ct)      */
    "1:\tmovl %4,%%edi\n\t" /* edi = ct                      */
    "movl %%esi,%%eax\n\t"  /* eax = esi */
    "movl %%edx,%%ecx\n\t"  /* ecx = edx */
    "repe\n\t"              /* while (ecx--) */
    "cmpsb\n\t"             /*     if (ds:esi++ != es:edi++) break;   */
    "je 2f\n\t"             /* if (ecx==0) goto 2                     */
                            /* also works for empty string, see above */
    "xchgl %%eax,%%esi\n\t" /* tmp=eax; eax=esi; esi=tmp */
    "incl %%esi\n\t"        /* esi++  */
    "cmpb $0,-1(%%eax)\n\t" /* if (ds:eax-1 != 0) */
    "jne 1b\n\t"            /*     goto 1         */
    "xorl %%eax,%%eax\n\t"  /* eax = 0 (not found ct in cs) */
    "2:"
    :"=a" (__res):"0" (0),"c" (0xffffffff),"S" (cs),"g" (ct)
    :"cx","dx","di","si");
return __res;
}

/* strlen,
 * 计算s所指字符串长度并返回。*/
extern inline int strlen(const char * s)
{
register int __res __asm__("cx");
/* 内联汇编输入。
 * "D" (s),edi=s;
 * "a" (0),eax=0;
 * "0" (0xffffffff),ecx=0xffffffff。
 * 
 * 内联汇编输出。
 * "=c" (__res),__res=ecx。*/
__asm__("cld\n\t"
    "repne\n\t"     /* while (ecx--) */
    "scasb\n\t"     /*     if (es:edi++ == al) break;*/
    "notl %0\n\t"   /* ecx ~= ecx */
    "decl %0"       /* ecx-- */
    :"=c" (__res):"D" (s),"a" (0),"0" (0xffffffff):"di");
return __res;
}

extern char * ___strtok;

/* strtok,
 * 以ct所指字符串中的字符为分隔符分隔s所指字符串。
 * 在s所指字符串中查找到ct包含的分隔字符时,strtok将会把
 * s中该分隔字符替换为0,在后续调用strtok时需将s参数置空。
 * 每次调用成功该函数将返回s串中分隔符之后的字符串。*/
extern inline char * strtok(char * s,const char * ct)
{
register char * __res __asm__("si");
/* 内联汇编输入。
 * "0" (___strtok),ebx=___strtok;
 * "1" (s),esi=s;
 * "g" (ct),若还有空闲寄存器则该空闲寄存器=ct,否则引用内存变量ct。
 *
 * 内联汇编输出。
 * "=b" (__res),__res=ebx;
 * "=S" (___strtok),___strtok=esi。*/
__asm__("testl %1,%1\n\t"   /* if (esi != 0) */
    "jne 1f\n\t"            /*    goto 1     */
    "testl %0,%0\n\t"       /* if (ebx == 0) */
    "je 8f\n\t"             /*    goto 8     */
    "movl %0,%1\n"          /* esi = ebx     */
    "1:\txorl %0,%0\n\t"    /* ebx = 0       */
    "movl $-1,%%ecx\n\t"    /* ecx = -1(0xffffffff)*/
    "xorl %%eax,%%eax\n\t"  /* eax = 0       */
    "cld\n\t"
    "movl %4,%%edi\n\t"     /* edi = ct      */
    "repne\n\t"             /* while (ecx--) */
    "scasb\n\t"             /*     if (es:edi++ == al) break */
    "notl %%ecx\n\t"        /* ecx ~= ecx   */
    "decl %%ecx\n\t"        /* ecx--        */
    "je 7f\n\t"             /* empty delimeter-string */
    "movl %%ecx,%%edx\n"    /* edx = ecx */
    "2:\tlodsb\n\t"         /* al = ds:esi++ */
    "testb %%al,%%al\n\t"   /* if (al == 0)  */
    "je 7f\n\t"             /*    goto 7     */
    "movl %4,%%edi\n\t"     /* edi = ct      */
    "movl %%edx,%%ecx\n\t"  /* ecx = edx     */
    "repne\n\t"             /* while(ecx --) */
    "scasb\n\t"             /*    if (es:edi++ == al) break */
    "je 2b\n\t"             /* if (es:edi-1 == al) goto 2   */
    "decl %1\n\t"           /* esi-- */
    "cmpb $0,(%1)\n\t"      /* if (0 == esi) */
    "je 7f\n\t"             /*     goto 7    */
    "movl %1,%0\n"          /* esi = ebx     */
    "3:\tlodsb\n\t"         /* al = ds:esi++ */
    "testb %%al,%%al\n\t"   /* if (al == 0)  */
    "je 5f\n\t"             /*    goto 5     */
    "movl %4,%%edi\n\t"     /* edi = ct      */
    "movl %%edx,%%ecx\n\t"  /* ecx = edx     */
    "repne\n\t"             /* while (ecx--) */
    "scasb\n\t"             /*     if (es:edi++ == al) break */
    "jne 3b\n\t"            /* if (es:edi-1 != al) goto 3    */
    "decl %1\n\t"           /* esi-- */
    "cmpb $0,(%1)\n\t"      /* if (0 == esi) */
    "je 5f\n\t"             /*     goto 5    */
    "movb $0,(%1)\n\t"      /* esi = 0       */
    "incl %1\n\t"           /* esi++         */
    "jmp 6f\n"              
    "5:\txorl %1,%1\n"      /* esi = 0       */
    "6:\tcmpb $0,(%0)\n\t"  /* if (ebx != 0) */
    "jne 7f\n\t"            /*     goto 7    */
    "xorl %0,%0\n"          /* ebx = 0       */
    "7:\ttestl %0,%0\n\t"   /* if (ebx != 0) */
    "jne 8f\n\t"            /*    goto 8     */
    "movl %0,%1\n"          /* esi = ebx     */
    "8:"
    :"=b" (__res),"=S" (___strtok)
    :"0" (___strtok),"1" (s),"g" (ct)
    :"ax","cx","dx","di");
return __res;
}

/* memcpy,
 * 将src所指内存段前n字节拷贝到dest所指内存段中。*/
extern inline void * memcpy(void * dest,const void * src, int n)
{
/* 内联汇编输入。
 * "c" (n),ecx=n;
 * "S" (src),esi=src;
 * "D" (dest),edi=dest。*/
__asm__("cld\n\t"
    "rep\n\t"   /* while (ecx--) */
    "movsb"     /*     es:edi++ = ds:esi++ */
    ::"c" (n),"S" (src),"D" (dest)
    :"cx","si","di");
return dest;
}

/* memmove,
 * 若dest小于src时,将src前n字节拷贝到dest所指内存段中;
 * 否则从src+n-1开始"逆向"n字节到dest+n-1内存段中。*/
extern inline void * memmove(void * dest,const void * src, int n)
{
/* 若地址dest小于地址src */
if (dest<src)
/* 内联汇编输入。
 * "c" (n),ecx=n;
 * "S" (src),esi=src;
 * "D" (dest),edi=dest。*/
__asm__("cld\n\t"
    "rep\n\t"   /* while (ecx--) */
    "movsb"     /*     es:edi++ = ds:esi++ */
    ::"c" (n),"S" (src),"D" (dest)
    :"cx","si","di");
else
/* 内联汇编输入。
 * "c" (n),ecx=n;
 * "S" (src+n-1),esi=src+n-1;
 * "D" (dest+n-1),edi=dest+n-1。*/
__asm__("std\n\t"
    "rep\n\t"   /* while (ecx--) */
    "movsb"     /*     es:edi--=ds:esi-- */
    ::"c" (n),"S" (src+n-1),"D" (dest+n-1)
    :"cx","si","di");
return dest;
}

/* memcmp,
 * 比较cs和ct所指内存段中的count字节内容。*/
extern inline int memcmp(const void * cs,const void * ct,int count)
{
register int __res __asm__("ax");

/* 内联汇编输入。
 * "0" (0),eax=0;
 * "D" (cs),edi=cs;
 * "S" (ct),esi=ct;
 * "c" (count),ecx=count。
 *
 * 内联汇编输出。
 * "=a" (__res),__res=eax。*/
__asm__("cld\n\t"
    "repe\n\t"              /* while (ecx--) */
    "cmpsb\n\t"             /*    if (ds:esi++ != es:edi++) break */
    "je 1f\n\t"             /* if (ecx == ) goto 1 */
    "movl $1,%%eax\n\t"     /* eax = 1             */
    "jl 1f\n\t"             /* if(ct[ecx+1] < cs[ecx+1]) goto 1 */
    "negl %%eax\n"          /* if(ct[ecx+1] > cs[ecx+1]) eax=-eax */
    "1:"
    :"=a" (__res):"0" (0),"D" (cs),"S" (ct),"c" (count)
    :"si","di","cx");
return __res;
}

/* memchr,
 * 在cs所指内存count字节中查找c,若找到则返回c在内存中的地址,否则返回0。*/
extern inline void * memchr(const void * cs,char c,int count)
{
register void * __res __asm__("di");
if (!count)
    return NULL;

/* 内联汇编输入。
 * "a" (c),eax=c;
 * "D" (cs),edi=cs;
 * "c" (count),ecx=count。
 * 
 * 内联汇编输出。
 * "=D" (__res),__res=edi。*/
__asm__("cld\n\t"
    "repne\n\t"     /* while (ecx--) */
    "scasb\n\t"     /*     if (es:edi++ == al) break */
    "je 1f\n\t"     /* if (es:edi-1 == al) goto 1    */
    "movl $1,%0\n"  /* edi = 1 */
    "1:\tdecl %0"   /* edi -- */
    :"=D" (__res):"a" (c),"D" (cs),"c" (count)
    :"cx");
return __res;
}

/* memset,
 * 从s所指内存开始填充count字节c。*/
extern inline void * memset(void * s,char c,int count)
{
/* 内联汇编输入。
 * "a" (c),eax=c;
 * "D" (s),edi=s;
 * "c" (count),ecx=count。*/
__asm__("cld\n\t"
    "rep\n\t"   /* while (ecx--) */
    "stosb"     /*     movb al, es:edi */
    ::"a" (c),"D" (s),"c" (count)
    :"cx","di");
return s;
}

#endif
