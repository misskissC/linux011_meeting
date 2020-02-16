#ifndef _CTYPE_H
#define _CTYPE_H

/* 字符类型标识 */
#define _U  0x01    /* upper,标识大写字母['A','Z'] */
#define _L  0x02    /* lower,标识小写字母['a','z'] */
#define _D  0x04    /* digit,标识数字字符['0','9'] */
#define _C  0x08    /* cntrl,标识所定义的控制字符 */
#define _P  0x10    /* punct,标识标点字符 */
#define _S  0x20    /* white space (space/lf/tab),标识空白字符 */
#define _X  0x40    /* hex digit,标识16进制数字 */
#define _SP 0x80    /* hard space (0x20),标识空格字符 */

/* 声明ctype.c中定义的字符属性数组和字符变量 */
extern unsigned char _ctype[];
extern char _ctmp;

/* 判断字符类型的宏,宏值为1时表明c为宏名所标识类型 */
#define isalnum(c) ((_ctype+1)[c]&(_U|_L|_D))   /* c为小整型(字符也是整型) */
#define isalpha(c) ((_ctype+1)[c]&(_U|_L))       /* c为字符 */
#define iscntrl(c) ((_ctype+1)[c]&(_C))          /* c为控制字符 */
#define isdigit(c) ((_ctype+1)[c]&(_D))          /* c为数字 */
#define isgraph(c) ((_ctype+1)[c]&(_P|_U|_L|_D)) /* c为可显示图形字符 */
#define islower(c) ((_ctype+1)[c]&(_L))           /* c为小写字符 */
#define isprint(c) ((_ctype+1)[c]&(_P|_U|_L|_D|_SP)) /* c为可打印字符 */
#define ispunct(c) ((_ctype+1)[c]&(_P)) /* c为标点字符 */
#define isspace(c) ((_ctype+1)[c]&(_S)) /* c为空格 */
#define isupper(c) ((_ctype+1)[c]&(_U)) /* c为大写字符 */
#define isxdigit(c) ((_ctype+1)[c]&(_D|_X)) /* c为十六进制数 */

/* c为ASCII,将c转换为ASCII */
#define isascii(c) (((unsigned) c)<=0x7f)
#define toascii(c) (((unsigned) c)&0x7f)

/* 将c转换为小/大写字母,该宏在多线程中使用会引起对_ctmp的冲突访问 */
#define tolower(c) (_ctmp=c,isupper(_ctmp)?_ctmp-('A'-'a'):_ctmp)
#define toupper(c) (_ctmp=c,islower(_ctmp)?_ctmp-('a'-'A'):_ctmp)

#endif
