#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

/* Amount of space required in an argument list for an arg of type TYPE.
   TYPE may alternatively be an expression whose type is used.  */

/* __va_rounded_size(TYPE),
 * 以int类型大小对齐计算TYPE所占字节数。
 * 
 * 因为栈以int类型对齐存储数据,所以需将各类型数据所占字节数以int对齐。*/
#define __va_rounded_size(TYPE)  \
  (((sizeof (TYPE) + sizeof (int) - 1) / sizeof (int)) * sizeof (int))

/* va_start, 获取参数LASTARG上一个参数的栈地址。
 * 
 * 函数实参在父函数中从右至左依次入栈。
 * 
 * __builtin_saveregs ()将存储在寄存器中的参数压入栈中,可参考gcc手册。*/
#ifndef __sparc__
#define va_start(AP, LASTARG)                       \
 (AP = ((char *) &(LASTARG) + __va_rounded_size (LASTARG)))
#else
#define va_start(AP, LASTARG)                       \
 (__builtin_saveregs (),                            \
  AP = ((char *) &(LASTARG) + __va_rounded_size (LASTARG)))
#endif

/* 声明gnulib库只能怪的va_end,
 * 若要使用所声明的va_end则应将宏va_end(AP)注释掉。*/
void va_end (va_list);  /* Defined in gnulib */
#define va_end(AP)

/* va_arg(AP, TYPE),
 * 获取上一个(在函数参数列表中靠右)类型为TYPE的参数值,同时AP后移指向上一个参数的栈地址。*/
#define va_arg(AP, TYPE)                            \
 (AP += __va_rounded_size (TYPE),                   \
  *((TYPE *) (AP - __va_rounded_size (TYPE))))

#endif /* _STDARG_H */
