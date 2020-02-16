#
# if you want the ram-disk device, define this to be the
# size in blocks.
#
# 若欲使用虚拟硬盘,则用RAMDISK定义虚拟硬盘大小(单位Kb,见main)。
RAMDISK = #-DRAMDISK=512

# 8086指令 汇编编译器和链接器
# 没有度娘到as86官网,参考网上归结其编译选项如下
# as86
# -0 16bit指令
# -3 32bit指令
# -a 产生与GNU编译链接器兼容的机器码
# -b 产生二进制文件,其后可跟文件名
# -g 在目标文件中保存全局符号
# -j 所有跳转语句均为长跳转
# -m 在符号列表中扩展宏定义
# -o 产生目标文件,其后跟目标文件名
# -s 产生符号文件,其后跟符号文件名
# -u 产生未定义符号表
# -w 不显示编译警告信息
#
# ld86
# -0 产生16bit格式的头部信息
# -3 产生32bit格式的头部信息
# -M 在标准输出设备上显示已链接的符号
# -lx 将库/local/lib/subdir/libx.a加入链接表中
# -m 在标准输出设备上显示已链接的模块
# -o 指定输出文件名,其后跟输出文件名
# -r 产生适合进一步重定位的输出
# -s 删除目标文件中的符号
AS86    =as86 -0 -a
LD86    =ld86 -0

# GNU汇编编译器和链接器
AS  =gas
LD  =gld

# GNU链接器选项...
LDFLAGS =-s -x -M

# gcc及其编译选项,
# 可以在其官网上通过Manual页面查看(第3章)
# https://gcc.gnu.org/onlinedocs/
CC  =gcc $(RAMDISK)
CFLAGS  =-Wall -O -fstrength-reduce -fomit-frame-pointer \
-fcombine-regs -mstring-insns
# GNU CPP 预处理器
# https://gcc.gnu.org/onlinedocs/cpp/
# 要在官网文档找以下这两个特定选项也是比较难一点。
CPP =cpp -nostdinc -Iinclude

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of /dev/hd6 is used by 'build'.
#
# ROOT_DEV用于指定 在用build工具制作磁盘映像时的 默认根文件设备。
ROOT_DEV=/dev/hd6

# 定义变量,作为后续规则的先决依赖条件或目标输出文件。
ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
MATH    =kernel/math/math.a
LIBS    =lib/lib.a

# 隐式规则,用于匹配后续只含目标和先决依赖文件的规则 #
# 此处的自动变量$*和$<分别代表无后缀目标和第一个先决依赖文件 #
#
# 用于匹配目标为汇编源文件(.s),先决依赖文件为C源文件(.c)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用C编译器
# 及其编译选项将($<对应的)C源文件转换为($*.o指定的)汇编源文件。
.c.s:
    $(CC) $(CFLAGS) \
    -nostdinc -Iinclude -S -o $*.s $<
# 用于匹配目标为目标文件(.o),先决依赖文件为汇编源文件(.s)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用汇编器将
# ($<对应的)汇编源文件转换为($*.o指定的)目标文件。
.s.o:
    $(AS) -c -o $*.o $<
# 用于匹配目标为目标文件(.o),先决依赖文件为汇编源文件(.c)的规则。
# 若某显式规则在被解析时与此规则匹配,该规则下的命令使用C编译器将
# ($<对应的)C源文件转换为($*.o指定的)目标文件。
.c.o:
    $(CC) $(CFLAGS) \
    -nostdinc -Iinclude -c -o $*.o $<
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

# 最顶层目标all,其先决依赖文件为Image。
# 当执行"make all"时会先检查Image是否发生改变。
all:    Image

# 目标Image,其先决依赖文件为boot/bootset... tools/build等文件,
# 当先决依赖文件有改变时则会执行该规则下的命令以重新生成Image。
# 
# 该规则可由"make Image" "make disk"间接触发或由"make Image"直接触发。
Image: boot/bootsect boot/setup tools/system tools/build
    tools/build boot/bootsect boot/setup tools/system $(ROOT_DEV) > Image
    sync

# 目标disk的先决依赖文件也为Image, 当Image发生变化时该规则下的命令会执行。
# 该命令将Image以8192字节为单位写入/dev/PSO即第一个软驱设备上。将Image写
# 入到软驱设备软盘上后若BIOS将该软盘设置为启动设备,重启计算机就可以让计算
# 机启动并运行Linux0.11了。
#
# 由 make disk 触发该规则。
disk: Image
    dd bs=8192 if=Image of=/dev/PS0

# 目标tools/build的先决依赖文件为tools/build.c。
# 当依赖文件tools/build.c发生改变则执行规则中的命令及重新生成build。
#
# make Image; make Image; make disk都可触发该规则。
tools/build: tools/build.c
    $(CC) $(CFLAGS) \
    -o tools/build tools/build.c

# 目标boot/head.o的依赖文件文件boot/head.s
# 该规则与前文第2个隐式规则匹配,即若该规则
# 被触发则将boot/head.s转换为boot/head.o。
# 
# 该规则由下一条即tools/system目标所在规则触发。
boot/head.o: boot/head.s

# 目标tools/system的先决依赖文件为boot/head.o init/main.o... $(LIBS)等
# 当先决依赖文件有发生改变时,规则中的命令将被执行,即会生成system到tools
# 目录下。
#
# 该规则由make Image触发,凡能触发Image目标所在规则的规则都可触本规则。
tools/system:   boot/head.o init/main.o \
    $(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
    $(LD) $(LDFLAGS) boot/head.o init/main.o \
    $(ARCHIVES) \
    $(DRIVERS) \
    $(MATH) \
    $(LIBS) \
    -o tools/system > System.map

# 目标kernel/math/math.a由命令
# (cd kernel/math; make)生成即进入kernel/math目录后执行make生成。
#
# 该规则直接由 目标tools/system所在规则直接触发。
kernel/math/math.a:
    (cd kernel/math; make)

# 目标kernel/blk_drv/blk_drv.a由命令
# (cd kernel/blk_drv; make)生成即进入kernel/blk_drv目录后执行make生成。
#
# 该规则直接由 目标tools/system所在规则直接触发。
kernel/blk_drv/blk_drv.a:
    (cd kernel/blk_drv; make)

# 目标kernel/chr_drv/chr_drv.a由命令
# (cd kernel/chr_drv; make)生成即进入kernel/chr_drv目录后执行make生成。
#
# 该规则直接由 目标tools/system所在规则直接触发。
kernel/chr_drv/chr_drv.a:
    (cd kernel/chr_drv; make)

# 目标kernel/kernel.o由命令
# (cd kernel; make)生成即进入kernel目录后执行make生成。
#
# 该规则直接由 目标tools/system所在规则直接触发。
kernel/kernel.o:
    (cd kernel; make)

# 目标mm/mm.o由命令
# (cd mm; make)生成即进入mm目录后执行make生成。
#
# 该规则直接由 目标tools/system所在规则直接触发。
mm/mm.o:
    (cd mm; make)

# 目标fs/fs.o由命令
# (cd fs; make)生成即进入fs目录后执行make生成。
#
# 该规则直接由 目标tools/system所在规则直接触发。
fs/fs.o:
    (cd fs; make)

# 目标lib/lib.a由命令
# (cd lib; make)生成即进入lib目录后执行make生成
# 
# 该规则直接由 目标tools/system所在规则直接触发。
lib/lib.a:
    (cd lib; make)

# 目标boot/setup的先决依赖文件为boot/setup.s。
# 当boot/setup.s发生改变时规则中命令将被执行
# 即生成setup可执行文件到boot目录下。
#
# 该规则直接由 目标Image所在规则直接触发。
boot/setup: boot/setup.s
    $(AS86) -o boot/setup.o boot/setup.s
    $(LD86) -s -o boot/setup boot/setup.o

# 目标boot/bootsect先决依赖文件为boot/bootsect.s
# 当依赖文件boot/bootsect.s发生改变时规则中命令将
# 会被执行即生成bootsect到boot目录下。
#
# 该规则直接由 目标Image所在规则直接触发。
boot/bootsect: boot/bootsect.s
    $(AS86) -o boot/bootsect.o boot/bootsect.s
    $(LD86) -s -o boot/bootsect boot/bootsect.o

# 忽略吧
tmp.s:  boot/bootsect.s tools/system
    (echo -n "SYSSIZE = (";ls -l tools/system | grep system \
        | cut -c25-31 | tr '\012' ' '; echo "+ 15 ) / 16") > tmp.s
    cat boot/bootsect.s >> tmp.s

# clean对应的命令将清除所有的结果文件
clean:
    rm -f Image System.map tmp_make core boot/bootsect boot/setup
    rm -f init/*.o tools/system tools/build boot/*.o
    (cd mm;make clean)
    (cd fs;make clean)
    (cd kernel;make clean)
    (cd lib;make clean)

backup: clean
    (cd .. ; tar cf - linux | compress - > backup.Z)
    sync

# init/main.o目标所在规则 所依赖头文件是通过手动指定的。
#
# make dep 将移除 ### Dependencies后续规则并将遍历本Makefile所在目录
# 下的源文件并确定其所依赖的头文件文件。原理是利用gcc预处理功能查看各
# C源文件中所包含的头文件,以将被包含头文件作为C源文件被编译的先决依赖文件。
dep:
    sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
    (for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
    cp tmp_make Makefile
    (cd fs; make dep)
    (cd kernel; make dep)
    (cd mm; make dep)

### Dependencies:
# 目标init/main.o所依赖的文件如下。
# 该规则由tools/system目标所在规则直接触发。
# 
# 该规则被触发后与本文件前面第3条隐式规则匹配,
# 即.c文件将被转换为init/main.o文件。
init/main.o : init/main.c include/unistd.h include/sys/stat.h \
  include/sys/types.h include/sys/times.h include/sys/utsname.h \
  include/utime.h include/time.h include/linux/tty.h include/termios.h \
  include/linux/sched.h include/linux/head.h include/linux/fs.h \
  include/linux/mm.h include/signal.h include/asm/system.h include/asm/io.h \
  include/stddef.h include/stdarg.h include/fcntl.h 


# 此文阅读Makefile主要是为了解下由工程文件生成映像文件的过程,所以只会重点关注与这个过程相关的规则。
# 欲了解映像文件生成,只阅读本Makefile应该就足够了,但也可以根据本Makefile提供的线索继续到各子目录看
# 各Makefile。
#
# kernel/Makefile
# kerne/math/Makefile
# kernel/blk_drv/Makefile
# kernel/chr_drv/Makefile
#
# mm/Makefile
# fs/Makefile
# lib/Makefile