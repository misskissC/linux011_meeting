#ifndef _STDDEF_H
#define _STDDEF_H

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef long ptrdiff_t;
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif

#undef NULL /* 取消在此之前的宏定义NULL */
#define NULL ((void *)0)

/* offsetof(TYPE, MEMBER),取MEMBER成员在TYPE类型中的偏移量。
 * 即取成员MEMBER的地址,因基址从0开始,所以此处所获取到的MEMBER
 * 地址即为其在TYPE类型中的偏移。*/
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#endif
