### linux011_meeting
---
`linux011_meeting` just only means linux0.11 learning, reading and annotating.

I had tried my best to read and annotate linux0.11 during 2019.05 ~ 2019.09, the fragments following extracted from boot/bootsect.s and kernel/sched.c respectively.
```asm
! 完成引导程序的拷贝后, 跳转到新内存段中执行后续程序。
! jmpi offset, seg 实现段间跳转即使得 cs = seg, ip = offset。
! seg为段基址, offset为基于seg段的偏移。
!
! jmpi go, INITSEG 跳转执行0x9000:go处指令。
! 此处, go在0x7c00段和0x9000段中的偏移相同(所以才能正确跳转哦)。
    jmpi    go,INITSEG
```

```C
/* wake_up,
 * 唤醒由参数*p指向结构体所管理进程,即
 * 为该进程设置为已就绪状态。由wake_up
 * 唤醒的进程为最后调用sleep_on(p)的进
 * 程,该进程将会在sleep_on()函数中唤醒
 * 在他之前调用sleep_on的进程,依次类推
 * 直到唤醒所有调用过sleep_on(p)的进程。*/
void wake_up(struct task_struct **p)
{
    if (p && *p) {
        (**p).state=0;
        /* 复位实参在本进程中所指向的上一个进入睡眠的进程 */
        *p=NULL;
    }
}
```
Goddess hopes more knowledgeable guys just like you can  ontinue to improve linux011_meeting together.
