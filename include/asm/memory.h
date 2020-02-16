/*
 *  NOTE!!! memcpy(dest,src,n) assumes ds=es=normal data segment. This
 *  goes for all kernel functions (ds=es=kernel space, fs=local data,
 *  gs=null), as well as for all well-behaving user programs (ds=es=
 *  user data space). This is NOT a bug, as any user program that changes
 *  es deserves to die if it isn't careful.
 */
/* 注,memcpy(dest,src,n)假设 ds和es指向同一个数据段。
 * 对于所有的内核函数,他们在调用该函数时,ds=es=内核数据段,fs=用户进程数据段,
 * gsp=null;在用户进程调用该函数时,ds=es=用户进程数据段。如果在用户程序中不
 * 小心改变了es寄存器的值,调用memcpy就会出错的哦。*/

/* 内联汇编的输入。
 * "D" ((long)(_res)), EDI = _res = dest;
 * "S" ((long)(src)),  ESI = src;
 * c" ((long) (n)), ecx = n。
 *
 * cld; rep; movsb 相当于
 * while(ecx--)
 *     movb ds:esi: es:edi
 *     esi += 1
 *     edi += 1
 * 即将ds:esi指向的n字节内容拷贝到es:edi指向的内存中,
 * memcpy需要保证ds=es, 即需二者指向同一个内存段。
 *
 * 最后返回目的内存空间首地址。
 *
 * GNU C支持在括号()中包含复合语句的语法。
 * 复合语句为包含在大括号{}中语句或表达式的集合。
 * ({...})会被当作一个表达式,
 * 所以({...})中的最后一个表达式为表达式的最终值,
 * 此处相当于"返回值"。
 * 2019.06.02 */
#define memcpy(dest,src,n) ({ \
void * _res = dest; \
__asm__ ("cld;rep;movsb" \
	::"D" ((long)(_res)),"S" ((long)(src)),"c" ((long) (n)) \
	:"di","si","cx"); \
_res; \
})
