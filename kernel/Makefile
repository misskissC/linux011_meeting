#
# Makefile for the FREAX-kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# 注: make dep 命令将会移除手动指定的依赖并自动生成生成新的依赖。不要
# 手动放置依赖文件进入,除非该依赖文件比较特殊(如非.c文件)。

# 在制作映像文件过程中,kernel/Makefile由根目录Makefile(在本Makefile所在目录)执行make命令解析 #

# gar用于打包目标文件形成.a库文件,也能从库文件中抽取文件
# gas为GNU汇编器
# gld为GNU链接器
# gcc为GNU C编译器
# https://gcc.gnu.org/onlinedocs/
# https://gcc.gnu.org/onlinedocs/cpp/
AR  =gar
AS  =gas
LD  =gld
# 设置链接器链接参数
LDFLAGS =-s -x 
CC  =gcc
# 设置C编译器编译参数
CFLAGS  =-Wall -O -fstrength-reduce -fomit-frame-pointer -fcombine-regs \
    -finline-functions -mstring-insns -nostdinc -I../include
# C预处理器使用gcc -E选项对应的预处理器
CPP =gcc -E -nostdinc -I../include


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

# 手动指定目标文件集赋给OBJS变量
OBJS  = sched.o system_call.o traps.o asm.o fork.o \
    panic.o printk.o vsprintf.o sys.o exit.o \
    signal.o mktime.o

# kernel.o为本Makefile的顶层目标。当在本Makefile所在目录中执行
# make命令时,kernel.o将会作为make默认目标。
# 
# 目标kernel.o的先决依赖文件为OBJS变量中的目标文件集。当OBJS中
# 的目标文件有发生变化时,make将会(在命令行)运行该规则下将目标文
# 件链接成kerne.o目标文件的命令。
kernel.o: $(OBJS)
    $(LD) -r -o kernel.o $(OBJS)
    sync

# make clean 将删除由本Makefile生成的所有文件。
clean:
    rm -f core *.o *.a tmp_make keyboard.s
    for i in *.c;do rm -f `basename $$i .c`.s;done
    (cd chr_drv; make clean)
    (cd blk_drv; make clean)
    (cd math; make clean)

# make dep 将移除 ### Dependencies后续规则并将遍历本Makefile所在目录
# 下的源文件并确定其所依赖的头文件文件。原理是利用gcc预处理功能查看各
# C源文件中所包含的头文件,以将被包含头文件作为C源文件被编译的先决依赖文件。
dep:
    sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
    (for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
        $(CPP) -M $$i;done) >> tmp_make
    cp tmp_make Makefile
    (cd chr_drv; make dep)
    (cd blk_drv; make dep)

### Dependencies:
# 多目标规则 t1 t2 : prerequisites 相当于
#   t1 : prerequisites
#   t2 ：prerequisites
#
# 分别匹配前面第1条和第3条隐式规则,即由exit.c分别生成exit.s和exit.o。
exit.s exit.o : exit.c ../include/errno.h ../include/signal.h \
  ../include/sys/types.h ../include/sys/wait.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
  ../include/linux/kernel.h ../include/linux/tty.h ../include/termios.h \
  ../include/asm/segment.h 
# 分别匹配前面第1条和第3条隐式规则,即由fork.c分别生成fork.s和fork.o。
fork.s fork.o : fork.c ../include/errno.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h ../include/asm/system.h 
# 分别匹配前面第1条和第3条隐式规则,即由mktime.c分别生成mktime.s和mktime.o。
mktime.s mktime.o : mktime.c ../include/time.h 
# 分别匹配前面第1条和第3条隐式规则,即由panic.c分别生成panic.s和panic.o。
panic.s panic.o : panic.c ../include/linux/kernel.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h 
# 分别匹配前面第1条和第3条隐式规则,即由printk.c分别生成printk.s和printk.o。
printk.s printk.o : printk.c ../include/stdarg.h ../include/stddef.h \
  ../include/linux/kernel.h 
# 分别匹配前面第1条和第3条隐式规则,即由sched.c分别生成sched.s和sched.o。
sched.s sched.o : sched.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/linux/kernel.h ../include/linux/sys.h \
  ../include/linux/fdreg.h ../include/asm/system.h ../include/asm/io.h \
  ../include/asm/segment.h 
# 分别匹配前面第1条和第3条隐式规则,即由signal.c分别生成signal.s和signal.o。
signal.s signal.o : signal.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/linux/kernel.h ../include/asm/segment.h 
# 分别匹配前面第1条和第3条隐式规则,即由sys.c分别生成sys.s和sys.o。
sys.s sys.o : sys.c ../include/errno.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/tty.h \
  ../include/termios.h ../include/linux/kernel.h ../include/asm/segment.h \
  ../include/sys/times.h ../include/sys/utsname.h 
# 分别匹配前面第1条和第3条隐式规则,即由traps.c分别生成traps.s和traps.o。
traps.s traps.o : traps.c ../include/string.h ../include/linux/head.h \
  ../include/linux/sched.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/system.h ../include/asm/segment.h ../include/asm/io.h 
# 分别匹配前面第1条和第3条隐式规则,即由vsprintf.c分别生成vsprintf.s和vsprintf.o。
vsprintf.s vsprintf.o : vsprintf.c ../include/stdarg.h ../include/string.h 

# 这些匹配相应隐式规则而生成的目标文件将用于顶层目标kernel.o所在规则的先决依赖文件以生成kernel.o #

