#ifndef _TIME_H
#define _TIME_H

#ifndef _TIME_T
#define _TIME_T
typedef long time_t;
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

/* 100个时间片为1s */
#define CLOCKS_PER_SEC 100

typedef long clock_t;

/* struct tm,
 * 描述事件的结构体类型。*/
struct tm {
    int tm_sec;  /* 秒 */
    int tm_min;  /* 分 */
    int tm_hour; /* 时 */
    int tm_mday; /* 天 */
    int tm_mon;  /* 月 */
    int tm_year; /* 年 */
    int tm_wday; /* [0,6]-[周天,周六] */
    int tm_yday; /* 每年1月1日起的天数 */
    int tm_isdst; /* 大于0时表夏令时 */
};

clock_t clock(void);
time_t time(time_t * tp);
double difftime(time_t time2, time_t time1);
time_t mktime(struct tm * tp);

char * asctime(const struct tm * tp);
char * ctime(const time_t * tp);
struct tm * gmtime(const time_t *tp);
struct tm *localtime(const time_t * tp);
size_t strftime(char * s, size_t smax, const char * fmt, const struct tm * tp);
void tzset(void);

#endif
