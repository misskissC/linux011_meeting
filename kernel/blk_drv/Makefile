#
# Makefile for the FREAX-kernel block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# 注, make dep 会移除为源文件手动指定的依赖而自动为源文件生成新依赖。
# 执行该规则后,不要再手动放置C源文件作为依赖文件。

# 在制作映像文件过程中,kernel/blk_drv/Makefile由根目录Makefile(在本Makefile所在目录)执行make命令解析 #

# gar,gas,gld,gcc, gcc -E分别为
# GNU 库文件打包,汇编器,链接器,C编译器和C预处理器。
# https://gcc.gnu.org/onlinedocs/
# https://gcc.gnu.org/onlinedocs/cpp/
AR  =gar
AS  =gas
LD  =gld
LDFLAGS =-s -x
CC  =gcc
CFLAGS  =-Wall -O -fstrength-reduce -fomit-frame-pointer -fcombine-regs \
    -finline-functions -mstring-insns -nostdinc -I../../include
CPP =gcc -E -nostdinc -I../../include

# 隐式规则,用于匹配后续只含目标和先决依赖文件的规则 #
# 此处的自动变量$*和$<分别代表无后缀目标和第一个先决依赖文件 #
#
# 用于匹配目标为汇编源文件(.s),先决依赖文件为C源文件(.c)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用C编译器
# 及其编译选项将($<对应的)C源文件转换为($*.o指定的)汇编源文件。
.c.s:
    $(CC) $(CFLAGS) \
    -S -o $*.s $<
# 用于匹配目标为目标文件(.o),先决依赖文件为汇编源文件(.c)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用C编译器将
# ($<对应的)C源文件转换为($*.o指定的)目标文件。
.s.o:
    $(AS) -o $*.o $<
# 用于匹配目标为目标文件(.o),先决依赖文件为汇编源文件(.s)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用汇编器将
# ($<对应的)汇编源文件转换为($*.o指定的)目标文件。
.c.o:
    $(CC) $(CFLAGS) \
    -c -o $*.o $<
# 
# 为以上隐式规则举个简单例子。
# 当前目录有 Makefile main.c 文件
# 
# all: main.out
# 
# main.out : main.o
#     gcc main.o -o main.out
# 
# main.o : main.c
# 
# .c.o:
#     gcc -c -o $*.o $<
#
# 在当前目录执行 make 命令
# gcc -c -o main.o main.c
# gcc main.o -o main.out


# 将目标文件集赋给变量OBJS
OBJS  = ll_rw_blk.o floppy.o hd.o ramdisk.o

# blk_drv.a为本Makefile的顶层目标。当在本Makefile所在目录中执行
# make命令时,blk_drv.a将会作为make默认目标。
# 
# 目标blk_drv.a的先决依赖文件为OBJS变量中的目标文件集。当OBJS中
# 的目标文件有发生变化时,make将会(在命令行)运行该规则下将目标文
# 件打包成blk_drv.a库文件的命令。
blk_drv.a: $(OBJS)
    $(AR) rcs blk_drv.a $(OBJS)
    sync

# make clean 将删除由本Makefile生成的文件。
clean:
    rm -f core *.o *.a tmp_make
    for i in *.c;do rm -f `basename $$i .c`.s;done

# make dep 将移除 ### Dependencies后续规则并将遍历本Makefile所在目录
# 下的源文件并确定其所依赖的头文件文件。原理是利用gcc预处理功能查看各
# C源文件中所包含的头文件,以将被包含头文件作为C源文件被编译的先决依赖文件。
dep:
    sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
    (for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
        $(CPP) -M $$i;done) >> tmp_make
    cp tmp_make Makefile

### Dependencies:
# 多目标规则 t1 t2 : prerequisites 相当于
#   t1 : prerequisites
#   t2 ：prerequisites
#
#分别匹配前面第1条和第3条隐式规则,即由floppy.c分别生成floppy.s和floppy.o。
floppy.s floppy.o : floppy.c ../../include/linux/sched.h ../../include/linux/head.h \
  ../../include/linux/fs.h ../../include/sys/types.h ../../include/linux/mm.h \
  ../../include/signal.h ../../include/linux/kernel.h \
  ../../include/linux/fdreg.h ../../include/asm/system.h \
  ../../include/asm/io.h ../../include/asm/segment.h blk.h 
#分别匹配前面第1条和第3条隐式规则,即由hd.c分别生成hd.s和hd.o。
hd.s hd.o : hd.c ../../include/linux/config.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/linux/mm.h ../../include/signal.h \
  ../../include/linux/kernel.h ../../include/linux/hdreg.h \
  ../../include/asm/system.h ../../include/asm/io.h \
  ../../include/asm/segment.h blk.h 
#分别匹配前面第1条和第3条隐式规则,即由ll_rw_blk.c分别生成ll_rw_blk.s和ll_rw_blk.o。
ll_rw_blk.s ll_rw_blk.o : ll_rw_blk.c ../../include/errno.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/linux/mm.h ../../include/signal.h \
  ../../include/linux/kernel.h ../../include/asm/system.h blk.h 

# 这些匹配相应隐式规则而生成的目标文件目标文件将用于顶层目标blk_drv.a所在规则的先决依赖文件以生成blk_drv.a #
