/*
 *  linux/kernel/vsprintf.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* vsprintf.c -- Lars Wirzenius & Linus Torvalds. */
/*
 * Wirzenius wrote this portably, Torvalds fucked it up :-)
 */

#include <stdarg.h>
#include <string.h>

/* we use this so that we can do without the ctype library */
/* 咱自己定义个宏来判断字符c是否为数字字符,这样就可以避免使用ctype库了 */
#define is_digit(c) ((c) >= '0' && (c) <= '9')

/* skip_atoi,
 * 将(*s)所指内存中的数字字符转换为对应的数字并
 * 返回,同时修改(*s)跳过数字字符而指向后续字符。*/
static int skip_atoi(const char **s)
{
    int i=0;

    while (is_digit(**s))
        /* 取当前字节内容并将(*s)往后移1字节 */
        i = i*10 + *((*s)++) - '0';
    return i;
}

/* 补0标识;显示符号标识;显示正号标识;
 * 用空格代替正号标识;左对齐标识;16进
 * 制数标识;16进制小写模式标识。*/
#define ZEROPAD 1   /* pad with zero */
#define SIGN    2   /* unsigned/signed long */
#define PLUS    4   /* show plus */
#define SPACE   8   /* space if plus */
#define LEFT    16  /* left justified */
#define SPECIAL 32  /* 0x */
#define SMALL   64  /* use 'abcdef' instead of 'ABCDEF' */

/* to_div(n, baes),
 * 对十进制数n进行一次base进制数的转换,
 * 并返回十进制数n所对应base进制数低位,
 * 同时n被改变,其值为(n / base)的商。
 * 
 * 内联汇编输入。
 * "0" (n),eax=n;
 * "1" (0),edx=0;
 * "r" (base),任何通用空闲寄存器=base。
 *
 * divl %4, divl base 即(edx << 32) + eax / base --> eax=商,edx=余数。
 * 
 * 内联汇编输出。
 * "=a" (n),n=eax;
 * "=d" (__res),__res=edx。*/
#define do_div(n,base) ({ \
int __res; \
__asm__("divl %4":"=a" (n),"=d" (__res):"0" (n),"1" (0),"r" (base)); \
__res; })

/* number,
 * 将十进制数num根据转换精度precision,转换标志type 转换为base进制数存于str所指内存中。*/
static char * number(char * str, int num, int base, int size, int precision
    ,int type)
{
    char c,sign,tmp[36];
    const char *digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i;

    /* 小写模式; 左对齐模式判断 */
    if (type&SMALL) digits="0123456789abcdefghijklmnopqrstuvwxyz";
    if (type&LEFT) type &= ~ZEROPAD;

    /* 可转换为2进制-35进制数 */
    if (base<2 || base>36)
        return 0;
    /* 判断采用0还是空格补齐 */
    c = (type & ZEROPAD) ? '0' : ' ' ;

    /* 判断是否显示正负数符号 */
    if (type&SIGN && num<0) {
        sign='-';
        num = -num;
    } else
        sign=(type&PLUS) ? '+' : ((type&SPACE) ? ' ' : 0);
    if (sign) size--; /* 为符号腾空间 */

    /* 为16进制或8进制标识0x和o腾空间 */
    if (type&SPECIAL)
        if (base==16) size -= 2;
        else if (base==8) size--;

    /* 将数字num转换为base进制数(字符)保存在tmp中 */
    i=0;
    if (num==0)
        tmp[i++]='0';
    else while (num!=0)
        tmp[i++]=digits[do_div(num,base)];

    /* 计算还剩余的显示空间/宽度 */
    if (i>precision) precision=i;
    size -= precision;

    /* 无左对齐和0填充时用空格补齐剩余显示空间 */
    if (!(type&(ZEROPAD+LEFT)))
        while(size-->0)
            *str++ = ' ';
    /* 符号 */
    if (sign)
        *str++ = sign;
    /* 数制标识 */
    if (type&SPECIAL)
        if (base==8)
            *str++ = '0';
        else if (base==16) {
            *str++ = '0';
            *str++ = digits[33];
        }
    /* 数制标识与数值之间有填充的情况则用指定字符填充(0或空格) */
    if (!(type&LEFT))
        while(size-->0)
            *str++ = c;
    /* 数值长度没有精度值大时用0填充 */
    while(i<precision--)
        *str++ = '0';
    /* 写入所转换的数值 */
    while(i-->0)
        *str++ = tmp[i];
    /* 左对齐的情况,数字右边用空格补齐空闲显示位 */
    while(size-->0)
        *str++ = ' ';
    return str;
}

/* vsprintf,
 * 将fmt中类型转换符(如%c %d等)对应的参数拷贝/转换到buf所指内存中。*/
int vsprintf(char *buf, const char *fmt, va_list args)
{
    int len;
    int i;
    char * str;
    char *s;
    int *ip;

    int flags;          /* flags to number() */

    int field_width;    /* width of output field */
    int precision;      /* min. # of digits for integers; max
                           number of chars for from string */
    int qualifier;      /* 'h', 'l', or 'L' for integer fields */

    for (str=buf ; *fmt ; ++fmt) {
        /* 将非类型转换符依次拷贝到buf所指内存中 */
        if (*fmt != '%') {
            *str++ = *fmt;
            continue;
        }

        /* process flags */
        flags = 0;
        repeat:
            ++fmt;  /* this also skips first '%',跳过字符'%' */
            switch (*fmt) {
                /* 置类型转换符对应参数的显示模式标志 */
                case '-': flags |= LEFT; goto repeat;
                case '+': flags |= PLUS; goto repeat;
                case ' ': flags |= SPACE; goto repeat;
                case '#': flags |= SPECIAL; goto repeat;
                case '0': flags |= ZEROPAD; goto repeat;
            }

        /* get field width */
        /* 获取显示参数的宽度,宽度可以直接写在类型转换符之前(如%3d);也
         * 可以用'*'表明用参数值来指定宽度,如vsprintf(buf, "%*d", 3,4),
         * 3将作为%d匹配到的整型数字4的显示宽度。*/
        field_width = -1;
        if (is_digit(*fmt))
            field_width = skip_atoi(&fmt);
        else if (*fmt == '*') {
            /* 应添加fmt++;语句跳过'*',否则进入switch中后会进入default */
            /* it's the next argument */
            field_width = va_arg(args, int);
            if (field_width < 0) { /* 若为负数则标识左对齐 */
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        /* get the precision */
        /* 显示精度精度/位数可直接包含在类型转换
         * 符前,也有可能由占位符'*'匹配参数指定。*/
        precision = -1;
        if (*fmt == '.') {
            ++fmt;
            if (is_digit(*fmt))
                precision = skip_atoi(&fmt);
        else if (*fmt == '*') {
            /* it's the next argument */
            precision = va_arg(args, int);
        }
            if (precision < 0)
                precision = 0;
        }

        /* get the conversion qualifier */
        /* 获取类型转换限定符 */
        qualifier = -1;
        if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
            qualifier = *fmt;
            ++fmt;
        }

        /* 获取类型转换符 */
        switch (*fmt) {
        /* 以字符类型解析参数 */
        case 'c':
            /* 左对齐时在字符左边补相应数目空格 */
            if (!(flags & LEFT))
                while (--field_width > 0)
                    *str++ = ' ';
            /* 获取参数值并取低字节值即字符的值 */
            *str++ = (unsigned char) va_arg(args, int);
            /* 右对齐时在字符右边补相应数目空格 */
            while (--field_width > 0)
                *str++ = ' ';
            break;

        /* 以字符串解析参数 */
        case 's':
            /* 取字符串首地址和长度 */
            s = va_arg(args, char *);
            len = strlen(s);
        
            /* 决定字符串显示宽度 */
            if (precision < 0)
                precision = len;
            else if (len > precision)
                len = precision;

            /* 根据对齐标志决定是否要在字符串左边补齐相应数目空格 */
            if (!(flags & LEFT))
                while (len < field_width--)
                    *str++ = ' ';
            /* 从字符串所在内存拷贝字符到buf相应位置上 */
            for (i = 0; i < len; ++i)
                *str++ = *s++;
            /* 根据对齐标志决定是否要在字符串左边补齐相应数目空格 */
            while (len < field_width--)
                *str++ = ' ';
            break;

        /* 以无符号八进进制解析参数 */
        case 'o':
            str = number(str, va_arg(args, unsigned long), 8,
                field_width, precision, flags);
            break;

        /* 以16进制数解析参数,该参数占8个宽度,若参数不足8位则在左边补0 */
        case 'p':
            if (field_width == -1) {
                field_width = 8;
                flags |= ZEROPAD;
            }
            str = number(str,
                (unsigned long) va_arg(args, void *), 16,
                field_width, precision, flags);
            break;

        /* 以16进制解析参数,x和X分别标识16进制字母的小写和大写格式 */
        case 'x':
            flags |= SMALL;
        case 'X':
            str = number(str, va_arg(args, unsigned long), 16,
                field_width, precision, flags);
            break;

        /* 以有符号10进制解析参数,u表示以无符号10进制数解析参数 */
        case 'd':
        case 'i':
            flags |= SIGN;
        case 'u':
            str = number(str, va_arg(args, unsigned long), 10,
                field_width, precision, flags);
            break;

        /* 将当前解析的参数长度存在参数所指向的地址中 */
        case 'n':
            ip = va_arg(args, int *);
            *ip = (str - buf);
            break;

        /* 处理特殊情况或错误情况 */
        default:
            /* 在匹配到'%'后还能运行到这里,说明'%'之后紧跟了'%',
             * 若没有紧跟'%'则补写入'%'以方便错误提示。*/
            if (*fmt != '%')
                *str++ = '%';
            if (*fmt)
                *str++ = *fmt;
            else /* 若为NULL则回退一字节重新指向NULL以让for循环顺利退出 */
                --fmt;
            break;
        }
    }
    *str = '\0';
    
    /* 返回所解析参数的长度 */
    return str-buf;
}
