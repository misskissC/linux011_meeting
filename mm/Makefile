# 在制作映像文件过程中,mm/Makefile由根目录Makefile(在本Makefile所在目录)执行make命令解析 #

# gar,gas,gld,gcc, gcc -E分别为
# GNU 库文件打包,汇编器,链接器,C编译器和C预处理器。
# https://gcc.gnu.org/onlinedocs/
# https://gcc.gnu.org/onlinedocs/cpp/
CC  =gcc
CFLAGS  =-O -Wall -fstrength-reduce -fcombine-regs -fomit-frame-pointer \
    -finline-functions -nostdinc -I../include
AS  =gas
AR  =gar
LD  =gld
CPP =gcc -E -nostdinc -I../include

# 隐式规则,用于匹配后续只含目标和先决依赖文件的规则 #
# 此处的自动变量$*和$<分别代表无后缀目标和第一个先决依赖文件 #
#
# 用于匹配目标为目标文件(.o),先决依赖文件为汇编源文件(.s)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用汇编器将
# ($<对应的)汇编源文件转换为($*.o指定的)目标文件。
.c.o:
    $(CC) $(CFLAGS) \
    -c -o $*.o $<
# 用于匹配目标为目标文件(.o),先决依赖文件为汇编源文件(.c)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用C编译器将
# ($<对应的)C源文件转换为($*.o指定的)目标文件。
.s.o:
    $(AS) -o $*.o $<
# 用于匹配目标为汇编源文件(.s),先决依赖文件为C源文件(.c)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用C编译器
# 及其编译选项将($<对应的)C源文件转换为($*.o指定的)汇编源文件。
.c.s:
    $(CC) $(CFLAGS) \
    -S -o $*.s $<
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


# 将目标文件集赋给OBJS变量
OBJS    = memory.o page.o

# all为本Makefile的顶层目标。当在本Makefile所在目录中执行
# make命令时,all将会作为make默认目标。该规则将会触发mm.o目
# 标所在规则被make解析。
all: mm.o

# 该规则由make all触发被make解析。
# 当先决依赖文件OBJS有改变时,该规则下的命令将会被make
# (丢到命令行)执行,从而将OBJS中的目标文件链接为mm.o目标文件。
mm.o: $(OBJS)
    $(LD) -r -o mm.o $(OBJS)

# make clean 将清除由本Makefile生成的文件
clean:
    rm -f core *.o *.a tmp_make
    for i in *.c;do rm -f `basename $$i .c`.s;done

# make dep 将移除 ### Dependencies后续规则并将遍历本Makefile所在目录
# 下的源文件并确定其所依赖的头文件文件。原理是利用gcc预处理功能查看各
# C源文件中所包含的头文件,以将被包含头文件作为C源文件被编译的先决依赖文件。
dep:
    sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
    (for i in *.c;do $(CPP) -M $$i;done) >> tmp_make
    cp tmp_make Makefile

### Dependencies:
# 该规则将匹配本Makefile第2条隐式规则,即由memory.c生成memory.o供mm.o目标所在规则使用。
memory.o : memory.c ../include/signal.h ../include/sys/types.h \
  ../include/asm/system.h ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/linux/mm.h ../include/linux/kernel.h 

# 没有看到生成page.o的规则呢 #
