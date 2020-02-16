#
# Makefile for some libs needed in the kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# 注, make dep 会移除为源文件手动指定的依赖而自动为源文件生成新依赖。
# 执行该规则后,不要再手动放置C源文件作为依赖文件。

# 在制作映像文件过程中,lib/Makefile由根目录Makefile(在本Makefile所在目录)执行make命令解析 #

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
    -finline-functions -mstring-insns -nostdinc -I../include
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


# 将目标文件集赋给OBJS变量
OBJS  = ctype.o _exit.o open.o close.o errno.o write.o dup.o setsid.o \
    execve.o wait.o string.o malloc.o

# lib.a为本Makefile的顶层目标。当在本Makefile所在目录中执行
# make命令时,lib.a将会作为make默认目标。
# 
# 目标lib.a的先决依赖文件为OBJS变量中的目标文件(集)。当OBJS中
# 的目标文件有发生变化时,make将会(在命令行)运行该规则下将目标文
# 件打包成lib.a库文件的命令。
lib.a: $(OBJS)
    $(AR) rcs lib.a $(OBJS)
    sync

# make clean 将清除由本Makefile生成的文件
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
#分别匹配前面第1条和第3条隐式规则,即由*.c分别生成*.s和*.o。
_exit.s _exit.o : _exit.c ../include/unistd.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/sys/times.h ../include/sys/utsname.h \
  ../include/utime.h 
close.s close.o : close.c ../include/unistd.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/sys/times.h ../include/sys/utsname.h \
  ../include/utime.h 
ctype.s ctype.o : ctype.c ../include/ctype.h 
dup.s dup.o : dup.c ../include/unistd.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/sys/times.h ../include/sys/utsname.h \
  ../include/utime.h 
errno.s errno.o : errno.c 
execve.s execve.o : execve.c ../include/unistd.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/sys/times.h ../include/sys/utsname.h \
  ../include/utime.h 
malloc.s malloc.o : malloc.c ../include/linux/kernel.h ../include/linux/mm.h \
  ../include/asm/system.h 
open.s open.o : open.c ../include/unistd.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/sys/times.h ../include/sys/utsname.h \
  ../include/utime.h ../include/stdarg.h 
setsid.s setsid.o : setsid.c ../include/unistd.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/sys/times.h ../include/sys/utsname.h \
  ../include/utime.h 
string.s string.o : string.c ../include/string.h 
wait.s wait.o : wait.c ../include/unistd.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/sys/times.h ../include/sys/utsname.h \
  ../include/utime.h ../include/sys/wait.h 
write.s write.o : write.c ../include/unistd.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/sys/times.h ../include/sys/utsname.h \
  ../include/utime.h 
