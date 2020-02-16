#ifndef _CONST_H
#define _CONST_H

#define BUFFER_END 0x200000

/* struct m_inode.i_mode字段属性标识 */
#define I_TYPE          0170000 /* 类型许可码 */
#define I_DIRECTORY     0040000 /* 目录 */
#define I_REGULAR       0100000 /* 常规文件 */
#define I_BLOCK_SPECIAL 0060000 /* 块设备特殊文件 */
#define I_CHAR_SPECIAL  0020000 /* 字符设备特殊文件 */
#define I_NAMED_PIPE    0010000 /* 命名管道 */
/* execve()对应的内核函数有涉及以下两个概念 */
#define I_SET_UID_BIT   0004000 /* 执行时设置有效用户ID */
#define I_SET_GID_BIT   0002000 /* 执行时设置有效组ID */

#endif
