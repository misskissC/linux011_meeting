#ifndef _A_OUT_H
#define _A_OUT_H

/* a.out(Assembley out)目标(可执行文件)的格式,跟ELF是同层级的概念 */

/* [1] GNU GCC 目标(可执行)文件格式,即linux0.11 OS system部分的格式 */

#define __GNU_EXEC_MACROS__

/* 粗略理解gnu gcc目标(可执行)文件格式
 * |------------------------------------------------------------------------------------------|
 * |头部区域|代码区域|数据区域|代码重定位信息区域|数据重定位信息区域|符号表区域|字符串常量区域|
 * |------------------------------------------------------------------------------------------| */

/* struct exec,
 * 描述(gcc构建的)可执行文件头部信息的结构体类型。*/
struct exec {
    unsigned long a_magic;  /* Use macros N_MAGIC, etc for access */
    unsigned a_text;        /* length of text, in bytes */
    unsigned a_data;        /* length of data, in bytes */
    unsigned a_bss;         /* length of uninitialized data area for file, in bytes */
    unsigned a_syms;        /* length of symbol table data in file, in bytes */
    unsigned a_entry;       /* start address */
    unsigned a_trsize;      /* length of relocation info for text, in bytes */
    unsigned a_drsize;      /* length of relocation info for data, in bytes */
};

#ifndef N_MAGIC
#define N_MAGIC(exec) ((exec).a_magic)
#endif

/* 标识目标文件魔数;这3种目标文件格式稍有差异,
 * 见后续计算目标文件中代码数据等区域的宏。*/
#ifndef OMAGIC
/* Code indicating object file or impure executable.  */
#define OMAGIC 0407
/* Code indicating pure executable.  */
#define NMAGIC 0410
/* Code indicating demand-paged executable.  */
#define ZMAGIC 0413
#endif /* not OMAGIC */

/* N_BADMAG,_N_BADMAG,
 * 不能能识别的(目标)文件格式 */
#ifndef N_BADMAG
#define N_BADMAG(x) \
 (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC  \
  && N_MAGIC(x) != ZMAGIC)
#endif

#define _N_BADMAG(x)    \
 (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC  \
  && N_MAGIC(x) != ZMAGIC)


/* 计算目标文件一个段中除去描述头部信息结构体后的长度 */
#define _N_HDROFF(x) (SEGMENT_SIZE - sizeof (struct exec))

/* N_TXTOFF,
 * ZMAGIC魔数所标识的目标文件头部信息占SEGMENT_SIZE
 * 字节,非ZMAGIC标识目标文件不以SEGMENT_SIZE字节对齐。
 * 
 * 由于目标文件代码区域在头部信息之后,所以该宏返回值
 * 为代码区域在目标文件中的偏移地址。*/
#ifndef N_TXTOFF
#define N_TXTOFF(x) \
 (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct exec) : sizeof (struct exec))
#endif

/* N_DATOFF,
 * 数据区域在目标文件中的偏移地址(数据区域在代码区域之后)。*/
#ifndef N_DATOFF
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#endif

/* N_TRELOFF,
 * 目标文件代码重定位信息偏移地址(代码重定位信息在数据区域之后)。*/
#ifndef N_TRELOFF
#define N_TRELOFF(x) (N_DATOFF(x) + (x).a_data)
#endif

/* N_DRELOFF,
 * 目标文件数据重定位信息偏移地址(数据重定位信息在代码重定位信息之后)。*/
#ifndef N_DRELOFF
#define N_DRELOFF(x) (N_TRELOFF(x) + (x).a_trsize)
#endif

/* N_SYMOFF,
 * 目标文件符号表偏移地址(符号表在数据重定位信息知之后)。*/
#ifndef N_SYMOFF
#define N_SYMOFF(x) (N_DRELOFF(x) + (x).a_drsize)
#endif

/* N_STROFF,
 * 目标文件字符串常量偏移地址(字符串常量在符号表之后)。*/
#ifndef N_STROFF
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)
#endif

/* [2] 可执行文件加载(内存地址)相关宏。
 * 在可执行文件被加载到内存中后,其中诸如符号表,可重定位等区域将被移除。*/

/* Address of text segment in memory after it is loaded.  */
/* linux0.11 代码区域被加载的内存基址 */
#ifndef N_TXTADDR
#define N_TXTADDR(x) 0
#endif

/* Address of data segment in memory after it is loaded.
   Note that it is up to you to define SEGMENT_SIZE
   on machines not listed here.  */
/* linux0.11 数据区域被加载的内存基址;
 * 可以在此处定义其他CPU所支持的SEGMEN_SIZE和PAGE_SIZE */
#if defined(vax) || defined(hp300) || defined(pyr)
#define SEGMENT_SIZE PAGE_SIZE
#endif
#ifdef  hp300
#define PAGE_SIZE   4096
#endif
#ifdef  sony
#define SEGMENT_SIZE    0x2000
#endif  /* Sony.  */
#ifdef is68k
#define SEGMENT_SIZE 0x20000
#endif
#if defined(m68k) && defined(PORTAR)
#define PAGE_SIZE 0x400
#define SEGMENT_SIZE PAGE_SIZE
#endif

#define PAGE_SIZE 4096
#define SEGMENT_SIZE 1024

/* _N_SEGMENT_ROUND(x),
 * 将x以SEGMENT_SIZE大小对齐。*/
#define _N_SEGMENT_ROUND(x) (((x) + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1))

/* _N_TXTENDADDR,
 * linux0.11 代码区域末端即数据区域起始基址。*/
#define _N_TXTENDADDR(x) (N_TXTADDR(x)+(x).a_text)

/* N_DATADDR,计算linux0.11 数据区被加载基址,非OMAGIC所标识
 * 目标文件数据区域被加载内存基址以SEGMENT_SIZE大小对齐。*/
#ifndef N_DATADDR
#define N_DATADDR(x) \
    (N_MAGIC(x)==OMAGIC? (_N_TXTENDADDR(x)) \
     : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))
#endif

/* Address of bss segment in memory after it is loaded.  */
/* linux0.11 bss区域被加载基址 */
#ifndef N_BSSADDR
#define N_BSSADDR(x) (N_DATADDR(x) + (x).a_data)
#endif

/* [3] 供编译链接器使用的相关宏 */

#ifndef N_NLIST_DECLARED
/* struct nlist,符号表表项
 * 用于描述目标文件中一个符号的结构体类型。*/
struct nlist {
  union {
    char *n_name;
    struct nlist *n_next;
    long n_strx;
  } n_un;
  unsigned char n_type; /* 符号类型,见后续N_*系列宏常量 */
  char n_other;
  short n_desc; /* 对符号的描述 */
  unsigned long n_value; /* 符号的值 */
};
#endif

/* 粗略领略标识符号特性/类型的宏,即
 * 以下N_*系列宏常量用于标识一个符号的类型,
 * 是目标文件符号表中各表项n_type字段的值。*/

#ifndef N_UNDF
#define N_UNDF 0    /* 标识符号未定义 */
#endif
#ifndef N_ABS
#define N_ABS 2     /* ... */
#endif
#ifndef N_TEXT
#define N_TEXT 4    /* 代码符号(代码地址) */
#endif
#ifndef N_DATA
#define N_DATA 6    /* 数据符号(数据地址) */
#endif
#ifndef N_BSS
#define N_BSS 8     /* 位于bss区域数据的地址 */
#endif
#ifndef N_COMM
#define N_COMM 18   /* ... */
#endif
#ifndef N_FN
#define N_FN 15     /* 链接器链接的文件名 */
#endif

#ifndef N_EXT
#define N_EXT 1     /* 标识是否为外部符号 */
#endif
#ifndef N_TYPE
#define N_TYPE 036  /* 符号类型 */
#endif
#ifndef N_STAB
#define N_STAB 0340 /* 符号表类型 */
#endif

/* The following type indicates the definition of a symbol as being
   an indirect reference to another symbol.  The other symbol
   appears as an undefined reference, immediately following this symbol.

   Indirection is asymmetrical.  The other symbol's value will be used
   to satisfy requests for the indirect symbol, but not vice versa.
   If the other symbol does not have a definition, libraries will
   be searched to find a definition.  */
/* 以下定义的常量0xa用于标识一个间接引用另一个符号值的符号。该符号之后
 * 的符号(被引用符号)为未定义引用状态。
 *
 * 间接引用不是对称的,间接引用符号可以引用另一个符号的值,反过来则不行。
 * 若没有在当前文件中找到间接引用符号所引用符号的定义,则会尝试继续在库
 * 中查找。*/
#define N_INDR 0xa

/* The following symbols refer to set elements.
   All the N_SET[ATDB] symbols with the same name form one set.
   Space is allocated for the set in the text section, and each set
   element's value is stored into one word of the space.
   The first word of the space is the length of the set (number of elements).

   The address of the set is made into an N_SETV symbol
   whose name is the same as the name of the set.
   This symbol acts like a N_DATA global symbol
   in that it can satisfy undefined external references.  */
/* 以下宏常量用于标识集合,值相同的符号构成一个集合。目标文件代码段为集合分配了
 * 空间,每个集合元素的值存储在1个字大小内存空间中。第1个字中存储了集合的长度。
 *
 * 集合基址会被一个被标识为N_SETV的跟集合同名的符号记录。该N_SETV符号就相当于
 * 一个N_DATA全局符号,可以匹配一个外部全局引用。*/
 
/* These appear as input to LD, in a .o file.  */
/* 这些宏值由编译器在编译阶段标识在目标文件的符号表中,以进一步输入给链接器处理 */
#define N_SETA  0x14    /* Absolute set element symbol */
#define N_SETT  0x16    /* Text set element symbol */
#define N_SETD  0x18    /* Data set element symbol */
#define N_SETB  0x1A    /* Bss set element symbol */

/* This is output from LD.  */
/* 该宏常量由链接器标识在(数据)符号表中 */
#define N_SETV  0x1C    /* Pointer to set vector in data area.  */

#ifndef N_RELOCATION_INFO_DECLARED

/* This structure describes a single relocation to be performed.
   The text-relocation section of the file is a vector of these structures,
   all of which apply to the text section.
   Likewise, the data-relocation section applies to the data section.  */
/* struct relocation_info结构体描述如何重定向一个符号。
 * 目标文件中的代码重定向区域就是由多个此结构体组成的(结构体数组);
 * 同理,目标文件中的数据重定向区域也是由struct relocation_info数组构成的。*/
struct relocation_info
{
  /* Address (within segment) to be relocated.  */
  /* (段内)重定位地址 */
  int r_address;
  /* The meaning of r_symbolnum depends on r_extern.  */
  /* r_symbolnum 的含义取决于r_extern */
  unsigned int r_symbolnum:24;
  /* Nonzero means value is a pc-relative offset
     and it should be relocated for changes in its own address
     as well as for changes in the symbol or section specified.  */
  /* 该位非0表示重定位类型为指令指针相对偏移,在符号或段(section)
   * 地址改变时需重定位。*/
  unsigned int r_pcrel:1;
  /* Length (as exponent of 2) of the field to be relocated.
     Thus, a value of 2 indicates 1<<2 bytes.  */
  /* 2^r_length用以指定重定位长度 */
  unsigned int r_length:2;
  /* 1 => relocate with value of symbol.
          r_symbolnum is the index of the symbol
      in file's the symbol table.
     0 => relocate with the address of a segment.
          r_symbolnum is N_TEXT, N_DATA, N_BSS or N_ABS
      (the N_EXT bit may be set also, but signifies nothing).  */
  /* 1 => 重定位 r_symbolnum 在符号表中所索引符号的值;
   * 0 => 重定位段的地址,此时r_symbolnum为N_TEXT或N_DATA或N_BSS或N_ABS
   * (N_EXT位也可能被设置,但在这里不会解析该位) */
  unsigned int r_extern:1;
  /* Four bits that aren't used, but when writing an object file
     it is desirable to clear them.  */
  /* 以下4比特位没有使用,但最好将其清0 */
  unsigned int r_pad:4;
};
#endif /* no N_RELOCATION_INFO_DECLARED.  */


#endif /* __A_OUT_GNU_H__ */
