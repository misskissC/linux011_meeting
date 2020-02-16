/*
 *  linux/fs/file_table.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/fs.h>

/* 打开文件结构体全局数组,
 * 用于记录系统打开的所有文件。*/
struct file file_table[NR_FILE];
