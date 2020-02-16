# 在制作映像文件过程中,fs/Makefile由根目录Makefile(在本Makefile所在目录)执行make命令解析 #

# gar,gas,gld,gcc, gcc -E分别为
# GNU 库文件打包,汇编器,链接器,C编译器和C预处理器。
# https://gcc.gnu.org/onlinedocs/
# https://gcc.gnu.org/onlinedocs/cpp/
AR  =gar
AS  =gas
CC  =gcc
LD  =gld
CFLAGS  =-Wall -O -fstrength-reduce -fcombine-regs -fomit-frame-pointer \
    -mstring-insns -nostdinc -I../include
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

# 将目标文件集赋予变量OBJS
OBJS=   open.o read_write.o inode.o file_table.o buffer.o super.o \
    block_dev.o char_dev.o file_dev.o stat.o exec.o pipe.o namei.o \
    bitmap.o fcntl.o ioctl.o truncate.o

# fs.o为本Makefile的顶层目标。当在本Makefile所在目录中执行
# make命令时,fs.o将会作为make默认目标。
# 
# 目标fs.o的先决依赖文件为OBJS变量中的目标文件(集)。当OBJS中
# 的目标文件有发生变化时,make将会(在命令行)运行该规则下将目标文
# 件链接成fs.o目标文件的命令。
fs.o: $(OBJS)
    $(LD) -r -o fs.o $(OBJS)

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
# 匹配前面第1条隐式规则,即由*.c生成*.o。
bitmap.o : bitmap.c ../include/string.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h 
block_dev.o : block_dev.c ../include/errno.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h ../include/asm/system.h 
buffer.o : buffer.c ../include/stdarg.h ../include/linux/config.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/sys/types.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/system.h ../include/asm/io.h 
char_dev.o : char_dev.c ../include/errno.h ../include/sys/types.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h ../include/asm/io.h 
exec.o : exec.c ../include/errno.h ../include/string.h \
  ../include/sys/stat.h ../include/sys/types.h ../include/a.out.h \
  ../include/linux/fs.h ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h 
fcntl.o : fcntl.c ../include/string.h ../include/errno.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/sys/types.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/segment.h ../include/fcntl.h \
  ../include/sys/stat.h 
file_dev.o : file_dev.c ../include/errno.h ../include/fcntl.h \
  ../include/sys/types.h ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/segment.h 
file_table.o : file_table.c ../include/linux/fs.h ../include/sys/types.h 
inode.o : inode.c ../include/string.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/system.h 
ioctl.o : ioctl.c ../include/string.h ../include/errno.h \
  ../include/sys/stat.h ../include/sys/types.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
  ../include/signal.h 
namei.o : namei.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/linux/kernel.h ../include/asm/segment.h \
  ../include/string.h ../include/fcntl.h ../include/errno.h \
  ../include/const.h ../include/sys/stat.h 
open.o : open.c ../include/string.h ../include/errno.h ../include/fcntl.h \
  ../include/sys/types.h ../include/utime.h ../include/sys/stat.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/tty.h \
  ../include/termios.h ../include/linux/kernel.h ../include/asm/segment.h 
pipe.o : pipe.c ../include/signal.h ../include/sys/types.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/linux/mm.h ../include/asm/segment.h 
read_write.o : read_write.c ../include/sys/stat.h ../include/sys/types.h \
  ../include/errno.h ../include/linux/kernel.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
  ../include/signal.h ../include/asm/segment.h 
stat.o : stat.c ../include/errno.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/linux/fs.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/segment.h 
super.o : super.c ../include/linux/config.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/system.h ../include/errno.h ../include/sys/stat.h 
truncate.o : truncate.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/sys/stat.h 
