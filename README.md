### linux011_meeting
---
`linux011_meeting` just only means linux0.11 learning, reading and annotating.

I had tried my best to read and annotate linux0.11 during 2019.05 ~ 2019.09, the small fragment following extracted from boot/bootsect.s.
```x86asm
! 完成引导程序的拷贝后, 跳转到新内存段中执行后续程序。
! jmpi offset, seg 实现段间跳转即使得 cs = seg, ip = offset。
! seg为段基址, offset为基于seg段的偏移。
!
! jmpi go, INITSEG 跳转执行0x9000:go处指令。
! 此处, go在0x7c00段和0x9000段中的偏移相同(所以才能正确跳转哦)。
    jmpi    go,INITSEG
```
Goddess hopes more knowledgeable guys just like you can  ontinue to improve linux011_meeting together.
