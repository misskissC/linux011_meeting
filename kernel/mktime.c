/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 *
 * 本文件中的代码不会充当库程序,仅会在内核中使用。实际上,本文件不会处理
 * 1970年以前的时间,不妨假设之前的这些时间一切正常吧。类似的,时间区域TZ
 * 等为题也被本文件嗨皮地忽略掉了,本文件打算尽可能简单的处理时间相关问题。
 * 可以使用公开的时间库程序(尽管我认为minix时间函数也属于公开的,你懂的)。
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 *
 * 另外,1970年这个起始点时间设置得让我有些讨厌——就不能用一个闰年时间代替吗?
 * 我还讨厌Gregorius,pope...这件事情激发了讨厌链,我现在甚至可以一口气做20及
 * 以上的引体向上。
 */
/* 1分钟/小时/天/年对应的秒数 */
#define MINUTE 60
#define HOUR (60*MINUTE)
#define DAY (24*HOUR)
#define YEAR (365*DAY)

/* month[m]表示(m+1)月前所有月份对应的秒数,2月按照29年计算(闰年)。*/
/* interestingly, we assume leap-years */
static int month[12] = {
    0,
    DAY*(31),
    DAY*(31+29),
    DAY*(31+29+31),
    DAY*(31+29+31+30),
    DAY*(31+29+31+30+31),
    DAY*(31+29+31+30+31+30),
    DAY*(31+29+31+30+31+30+31),
    DAY*(31+29+31+30+31+30+31+31),
    DAY*(31+29+31+30+31+30+31+31+30),
    DAY*(31+29+31+30+31+30+31+31+30+31),
    DAY*(31+29+31+30+31+30+31+31+30+31+30)
};

/* kernel_mktime,
 * 计算并返回从1970.01.01 0:0:0开始到现在(2000年前有效)所经历的秒数并返回。*/
long kernel_mktime(struct tm * tm)
{
    long res;
    int year;

    year = tm->tm_year - 70; /* year=0, 1, 2, ... */
/* magic offsets (y+1) needed to get leapyears right.*/
    /* 计算[1970, 1970 + year)之间的秒数, 1970 + year年前几个月的秒数。
     * ((year + 1) / 4)用于计算[1970, 1970 + year)之间的闰年数。*/
    res = YEAR*year + DAY*((year+1)/4);
    res += month[tm->tm_mon];
/* and (y+2) here. If it wasn't a leap-year, we have to adjust */
    /* (year+2)%4)为0时表明1970 + year为闰年 */
    if (tm->tm_mon>1 && ((year+2)%4))
        res -= DAY;
    res += DAY*(tm->tm_mday-1); /* 不算今天一整天对应的秒数 */
    res += HOUR*tm->tm_hour;    /* 今天已经历小时数对应的秒数 */
    res += MINUTE*tm->tm_min;
    res += tm->tm_sec;
    return res;
}
