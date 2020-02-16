#ifndef _CONFIG_H
#define _CONFIG_H

/*
 * The root-device is no longer hard-coded. You can change the default
 * root-device by changing the line ROOT_DEV = XXX in boot/bootsect.s
 */
/* 根文件系统设备没有再写死在代码中。可以通过boot/bootsect.s中的ROOT_DEV设
 * 定根文件系统设备。*/

/*
 * define your keyboard here -
 * KBD_FINNISH for Finnish keyboards
 * KBD_US for US-type
 * KBD_GR for German keyboards
 * KBD_FR for Frech keyboard
 */
/* 该宏将会表征你使用什么类型的键盘
 * KBD_FINNISH - 芬兰键盘
 * KBD_US - 美式键盘
 * KBD_GR - 德式键盘
 * KBD_FR - 法式键盘。*/
/*#define KBD_US */
/*#define KBD_GR */
/*#define KBD_FR */
#define KBD_FINNISH

/*
 * Normally, Linux can get the drive parameters from the BIOS at
 * startup, but if this for some unfathomable reason fails, you'd
 * be left stranded. For this case, you can define HD_TYPE, which
 * contains all necessary info on your harddisk.
 *
 * 正常情况下,Linux在启动阶段(见setup.s)会通过BIOS获取硬盘参数,但
 * 若获取失败的话可在此处自定义满足你本地硬盘的参数。
 * 
 * The HD_TYPE macro should look like this:
 * 定义HD_TYPE宏的格式为(见hd.c),
 *
 * #define HD_TYPE { head, sect, cyl, wpcom, lzone, ctl}
 *
 * In case of two harddisks, the info should be sepatated by
 * commas:
 * 类似的,定义包含两个硬盘参数的宏为,
 * 
 * #define HD_TYPE { h,s,c,wpcom,lz,ctl },{ h,s,c,wpcom,lz,ctl }
 */
/*
 This is an example, two drives, first is type 2, second is type 3:

#define HD_TYPE { 4,17,615,300,615,8 }, { 6,17,615,300,615,0 }

 NOTE: ctl is 0 for all drives with heads<=8, and ctl=8 for drives
 with more than 8 heads.

 If you want the BIOS to tell what kind of drive you have, just
 leave HD_TYPE undefined. This is the normal thing to do.

 举一个定义两个硬盘参数的例子,第2个硬盘的类型为2,第2个为3

 #define HD_TYPE { 4,17,615,300,615,8 }, { 6,17,615,300,615,0 }

 注,磁头数小于8时ctl字段为0,磁头数大于8时ctl字段为8。

 若你想通过BIOS自动检测当前计算机所携带的硬盘,可以将这部分代码注释掉,
 用BIOS自动检测硬盘才是更合理的方法,这只是个备用方法。
*/

#endif
