#ifndef _HEAD_H
#define _HEAD_H

/* 定义C结构体类型desc_table用于描述IDT和GDT。*/
typedef struct desc_struct {
	unsigned long a,b;
} desc_table[256];

/* 以 unsigned long数组类型 声明在head.s中定义的页表目录_pg_dir,
 * 以 desc_table类型 声明在head.s中定义的IDT(_idt)和GDT(_gdt)。*/
extern unsigned long pg_dir[1024];
extern desc_table idt,gdt;

/* GDT前4项描述符的索引。*/
#define GDT_NUL 0
#define GDT_CODE 1
#define GDT_DATA 2
#define GDT_TMP 3

/* LDT(暂未涉及)前3项描述符的索引。*/
#define LDT_NUL 0
#define LDT_CODE 1
#define LDT_DATA 2

/* 2019.05.24 */
#endif
