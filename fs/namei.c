/*
 *  linux/fs/namei.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <const.h>
#include <sys/stat.h>

#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])

/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed.
 */
/* #define NO_TRUNCATE */

/* 对应permission函数的mask参数,
 * MAY_EXEC  - 可进入;
 * MAY_WRITE - 可写;
 * MAY_READ  - 可读。*/
#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4

/*
 *  permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
/* [5] permission,
 * 检查当前任务对inode所指节点对应文件是否有mask对应权限。*/
static int permission(struct m_inode * inode,int mask)
{
    int mode = inode->i_mode;

    /* i节点链接数为0时表明其对应文件已被删除 */
/* special case: not even root can read/write a deleted file */
    if (inode->i_dev && !inode->i_nlinks)
        return 0;
    else if (current->euid==inode->i_uid)
        mode >>= 6;
    else if (current->egid==inode->i_gid)
        mode >>= 3;

    /* 当前用户或组员拥有该i节点对应文件的权限,
     * 即i_mode字段中记录的权限; 若当前任务为超级
     * 任务则拥有访问inode节点对应文件的权限。*/
    if (((mode & mask & 0007) == mask) || suser())
        return 1;
    return 0;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */
/* [6] match,
 * 将长度为len名为name的字符序列和de指向目录项中的name字段比较,
 * 二者相同返回1, 不同返回0。*/
static int match(int len,const char * name,struct dir_entry * de)
{
    register int same __asm__("ax");

    if (!de || !de->inode || len > NAME_LEN)
        return 0;
    if (len < NAME_LEN && de->name[len])
        return 0;

    /* 逐字节比较name和de->name，
     * 若二者相同则输出1, 否则输出0。
     * 
     * 内联汇编输入。
     * "0" (0), %0中约束即eax = 0;
     * "S" ((long) name), ESI = (long) name;
     * "D" ((long) de->name), EDI = (long) de->name;
     * "c" (len), ecx = len。
     *
     * 内联汇编指令。
     * cld fs; repe; cmpsb 相当于
     * while (ecx--)
     *    if (*((char *)fs:esi) == *(char *(fs:edi)))
     *      al = 1
     *      break;
     *    esi += 1
     *    edi += 1
     *
     * 内联汇编输出。
     * same = eax。*/
    __asm__("cld\n\t"
        "fs ; repe ; cmpsb\n\t"
        "setz %%al"
        :"=a" (same)
        :"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
        :"cx","di","si");
    return same;
}

/*
 *  find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * This also takes care of the few special cases due to '..'-traversal
 * over a pseudo-root and a mount point.
 */
/* [4] find_entry, name=".."情况未分析-------------------------------
 * 查看在(*dir)指向i节点对应目录下是否包含
 * 长度为namelen名为name的目录或文件,
 * 若包含则将name对应的目录项地址赋给*res_dir,
 * 并返回包含该目录项的缓冲区块地址;
 * 若不包含则返回NULL。*/
static struct buffer_head * find_entry(struct m_inode ** dir,
    const char * name, int namelen, struct dir_entry ** res_dir)
{
    int entries;
    int block,i;
    struct buffer_head * bh;
    struct dir_entry * de;
    struct super_block * sb;

#ifdef NO_TRUNCATE
    if (namelen > NAME_LEN)
        return NULL;
#else
    if (namelen > NAME_LEN)
        namelen = NAME_LEN;
#endif

    /* 计算(*dir)对应目录中的目录项数。*/
    entries = (*dir)->i_size / (sizeof (struct dir_entry));
    *res_dir = NULL;
    if (!namelen)
        return NULL;
/* check for '..', as we might have to do some "magic" for it */
    if (namelen==2 && get_fs_byte(name)=='.' && get_fs_byte(name+1)=='.') {
/* '..' in a pseudo-root results in a faked '.' (just change namelen) */
        if ((*dir) == current->root)
            namelen=1;
        else if ((*dir)->i_num == ROOT_INO) {
/* '..' over a mount-point results in 'dir' being exchanged for the mounted
   directory-inode. NOTE! We set mounted, so that we can iput the new dir */
            sb=get_super((*dir)->i_dev);
            if (sb->s_imount) {
                iput(*dir);
                (*dir)=sb->s_imount;
                (*dir)->i_count++;
            }
        }
    }

    /* 获取目录第1数据逻辑块的逻辑块号,
     * 并将该逻辑块中的数据读取到bh指向的缓冲区块中。*/
    if (!(block = (*dir)->i_zone[0]))
        return NULL;
    if (!(bh = bread((*dir)->i_dev,block)))
        return NULL;

    /* 在目录数据块中搜索名为name长度为namelen的目录项 */
    i = 0;
    de = (struct dir_entry *) bh->b_data;
    while (i < entries) {
        /* 若在第1个数据逻辑块中没有搜索到名为name的目录项,
         * 则继续在(*dir)对应目录的数据块中寻找。*/
        if ((char *)de >= BLOCK_SIZE+bh->b_data) {
            brelse(bh);
            bh = NULL;
            /* 获取有效的第i个目录项的逻辑块号,
             * 并将该逻辑块号对应的逻辑块读到bh所指向的缓冲区块中。*/
            if (!(block = bmap(*dir,i/DIR_ENTRIES_PER_BLOCK)) ||
                !(bh = bread((*dir)->i_dev,block))) {
                i += DIR_ENTRIES_PER_BLOCK;
                continue;
            }
            /* 目录项指针指向下一个包含目录项的有效逻辑块 */
            de = (struct dir_entry *) bh->b_data;
        }
        /* 若找到了长度为namelen的name的目录项,
         * 则将该目录项的地址输出给出参res_dir返回。*/
        if (match(namelen,name,de)) {
            *res_dir = de;
            return bh;
        }
        de++;
        i++;
    }
    /* 若最终没有在某目录下找到找到名为name的目录项,
     * 则释放最后一个数据逻辑块的缓冲区块并返回NULL。*/
    brelse(bh);
    return NULL;
}

/*
 *  add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
/* [7] add_entry,
 * 在dir所指i节点对应目录中,
 * 添加长度为namelen名为name的目录项,
 * 若添加成功则返回新目录项的地址和其所在的缓冲区块地址。*/
static struct buffer_head * add_entry(struct m_inode * dir,
    const char * name, int namelen, struct dir_entry ** res_dir)
{
    int block,i;
    struct buffer_head * bh;
    struct dir_entry * de;

    *res_dir = NULL;
#ifdef NO_TRUNCATE
    if (namelen > NAME_LEN)
        return NULL;
#else
    if (namelen > NAME_LEN)
        namelen = NAME_LEN;
#endif
    if (!namelen)
        return NULL;

    /* 获取dir所指i节点对应目录的第一个数据逻辑块号,
     * 并将其对应逻辑块读到缓冲区块中由bh指向。*/
    if (!(block = dir->i_zone[0]))
        return NULL;
    if (!(bh = bread(dir->i_dev,block)))
        return NULL;

    /* 在dir所指i节点对应设备中的逻辑块中添加
     * 长度为namelen名为name的文件或目录项。*/
    i = 0;
    de = (struct dir_entry *) bh->b_data;
    while (1) {
        /* dir所指i节点对应目录首个空闲目录项
         * 不再本缓冲区块中, */
        if ((char *)de >= BLOCK_SIZE+bh->b_data) {
            /* 则释放原来的缓冲区块,
             * 为第i块逻辑块创建逻辑块并将其读取到
             * 缓冲区块中由bh指向。*/
            brelse(bh);
            bh = NULL;
            block = create_block(dir,i/DIR_ENTRIES_PER_BLOCK);
            if (!block)
                return NULL;
            if (!(bh = bread(dir->i_dev,block))) {
                i += DIR_ENTRIES_PER_BLOCK;
                continue;
            }
            /* 若创建成功, 则更新dir所指i节点对应目录
             * 的有效逻辑块供插入新的目录或文件项。*/
            de = (struct dir_entry *) bh->b_data;
        }
        /* 在dir所指i节点对应目录的逻辑块中最后一个目录项后
         * 添加新的目录或文件项。*/
        if (i*sizeof(struct dir_entry) >= dir->i_size) {
            de->inode=0; /* i节点号置为0 */
            /* 更新dir所指i节点对应目录大小,
             * 更新dir所指i节点已修改标志和最后访问时间。*/
            dir->i_size = (i+1)*sizeof(struct dir_entry);
            dir->i_dirt = 1;
            dir->i_ctime = CURRENT_TIME;
        }
        if (!de->inode) {
            dir->i_mtime = CURRENT_TIME;
            /* 所添加新项的名字 */
            for (i=0; i < NAME_LEN ; i++)
                de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
            /* 跟新de所指项所在缓冲区块已修改标志,
             * 将在目录中添加的新项通过参数返回,
             * 将该新项所在的缓冲区块的首地址返回。*/
            bh->b_dirt = 1;
            *res_dir = de;
            return bh;
        }
        de++;
        i++;
    }
    /* 似乎不会运行到此处 */
    brelse(bh);
    return NULL;
}

/*
 *  get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 */
/* [3] get_dir,
 * 判断路径名pathname的有效性,
 * 并返回最后一个目录名的i节点指针。
 *
 * 如pathname=/usr/local/lib/hw.c
 * 则在当前任务根目录下判断是否存在目录usr,
 * 再在usr目录下判断目录local是否存在,
 * 若在local目录下存在lib目录, 则返回lib目录的i节点指针。*/
static struct m_inode * get_dir(const char * pathname)
{
    char c;
    const char * thisname;
    struct m_inode * inode;
    struct buffer_head * bh;
    int namelen,inr,idev;
    struct dir_entry * de;

    if (!current->root || !current->root->i_count)
        panic("No root inode");
    if (!current->pwd || !current->pwd->i_count)
        panic("No cwd inode");

    /* 判断路径名pathname在本任务中的顶层目录。
     *
     * 若路径名第一个字符为'/'则表明该路径为绝对路径,
     * 则应从当前任务的根目录搜索;
     * 若第一个字符非'/'则表明为相对路径,
     * 则应从当前任务当前目录开始搜索;
     * 若路径名为NULL则参数不合法应返回NULL。*/
    if ((c=get_fs_byte(pathname))=='/') {
        inode = current->root;
        pathname++;
    } else if (c)
        inode = current->pwd;
    else
        return NULL;    /* empty name is bad */
    inode->i_count++;

    /* 从顶层目录开始层层检查pathname路径中
     * 所包含的每一个目录是否存在。
     * 
     * 如pathname=usr/local/lib/,
     * 则会依次检查usr,local,lib目录
     * 在当前inode所指i节点对应目录中是否存在。*/
    while (1) {
        thisname = pathname;
        /* 若inode所指i节点不是某目录对应的i节点,
         * 或者对该目录无进入访问权限则释放inode节点并返回NULL。*/
        if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {
            iput(inode);
            return NULL;
        }

        /* 获取路径中靠最上一层的目录长度,
         * 并让pathname指向下一个目录名。*/
        for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)
            /* nothing */ ;
        /* 若遍历完中的所有目录则返回最后一个目录对应的i节点指针 */
        if (!c)
            return inode;

        /* 从inode所指i节点对应目录中查找名为
         * [*thisname, *pathname)的目录项于de指向,
         * 失败则表明目录中不包含指定内容,则在释放资源后返回NULL。*/
        if (!(bh = find_entry(&inode,thisname,namelen,&de))) {
            iput(inode);
            return NULL;
        }
        inr = de->inode;
        idev = inode->i_dev;
        brelse(bh);
        iput(inode);
        
        /* 将搜索的根目录更换为当前目录对应的i节点 */
        if (!(inode = iget(idev,inr)))
            return NULL;
    }
}

/*
 *  dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
/* [2] dir_namei,
 * 返回pathname中最底层目录名(以/结尾)的i节点指针,
 * 获取pathname中不以'/'结尾的目录名(或文件名)和长度
 * 并分别赋值给*name和*namelen. */
static struct m_inode * dir_namei(const char * pathname,
    int * namelen, const char ** name)
{
    char c;
    const char * basename;
    struct m_inode * dir;

    /* 判断pathname是否有效,
     * 若皆有效则返回pathname最底层目录的i节点指针。*/
    if (!(dir = get_dir(pathname)))
        return NULL;

    /* 从pathname中找到最底层的命名,
     * 如/usr/local/hw.c,
     * 以下程序将会分别返回hw.c及其长度4给出参*name和*namelen。*/
    basename = pathname;
    while (c=get_fs_byte(pathname++))
        if (c=='/')
            basename=pathname;
    *namelen = pathname-basename-1;
    *name = basename;
    return dir;
}

/*
 *  namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
/* [1] namei,
 * 获取pathname的i节点。
 *
 * 如pathname=/usr/local/hw,
 * 则该函数最终返回/usr/local目录下hw的i节点。
 *
 * 如pathname=/usr/local/,
 * 则该函数最终返回usr目录下local目录的i节点。*/
struct m_inode * namei(const char * pathname)
{
    const char * basename;
    int inr,dev,namelen;
    struct m_inode * dir;
    struct buffer_head * bh;
    struct dir_entry * de;

    /* 获取pathname中非以'/'结尾的最底层的命名和长度,
     * 分别赋给basename和namelen;
     * 返回pathname中最底层以'/'结尾命名的i节点指针。
     *
     * 如/usr/local,
     * dir为usr目录对应i节点,
     * basename指向"local", namelen=5;
     *
     * 如/usr/local/,
     * dir为local目录对应i节点,
     * basename指向NULL, namelen=0.*/
    if (!(dir = dir_namei(pathname,&namelen,&basename)))
        return NULL;
    /* pathname为/usr/local/情形,则返回local对应i节点 */
    if (!namelen)   /* special case: '/usr/' etc */
        return dir;

    /* 程序运行到这里, 说明pathname的格式为/usr/local类型。
     * 则从/usr目录下找名为local的目录项,并将包含该目录项
     * 的数据逻辑块读入到内存缓冲区中由bh指向,目的获取local
     * 的目录项并从中获取其i节点号和所关联的设备号。*/
    bh = find_entry(&dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        return NULL;
    }
    inr = de->inode;
    dev = dir->i_dev;
    brelse(bh);
    iput(dir);

    /* 获取设备号dev和i节点号inr对应i节点
     * 并修改其最后访问时间和已修改标志,
     * 然后将local的i节点返回。*/
    dir=iget(dev,inr);
    if (dir) {
        dir->i_atime=CURRENT_TIME;
        dir->i_dirt=1;
    }
    return dir;
}

/*
 *  open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 */
/* [8] open_namei,
 * 以flag访问标志打开pathname(最底层目录或文件),若pathname所
 * 代表文件不存在则以mode属性创建。打开文件或所创建文件的i节
 * 点由出参res_inode指向。成功打开或创建文件时返回0,失败返回
 * 相应错误号。*/
int open_namei(const char * pathname, int flag, int mode,
    struct m_inode ** res_inode)
{
    const char * basename;
    int inr,dev,namelen;
    struct m_inode * dir, *inode;
    struct buffer_head * bh;
    struct dir_entry * de;

    if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
        flag |= O_WRONLY;
    mode &= 0777 & ~current->umask;
    mode |= I_REGULAR;

    /* 检查路径名pathname, 若正确则返回最底层以'/'结束目录
     * 的i节点地址给dir;并将pathname中最底层目录(不以'/'结尾)
     * 或文件名赋给basenmae, 其长度赋给namelen。*/
    if (!(dir = dir_namei(pathname,&namelen,&basename)))
        return -ENOENT;
    if (!namelen) { /* special case: '/usr/' etc */
        /* 若访问标志不为 读写||创建||清0,则将pathname
         * 最底层目录的i节点地址赋给出参并返回0。*/
        if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
            *res_inode=dir;
            return 0;
        }
        /* 否则释放dir所指i节点
         * 并返回标识pathname最底层为目录的返回值。*/
        iput(dir);
        return -EISDIR;
    }

    /* 若pathname格式不为 '/usr/'则
     * 寻找pathname中最底层目录或文件basename
     * 的目录项地址给de,并返回包含de的缓冲区块地址。*/
    bh = find_entry(&dir,basename,namelen,&de);
    if (!bh) { /* pathname最底层目录或文件不存在 */
        /* 若不创建还未存在的目录或文件则返回无条目的错误码 */
        if (!(flag & O_CREAT)) {
            iput(dir);
            return -ENOENT;
        }
        /* 如果当前任务对basename所在目录
         * 无写权限则返回非法访问的错误码。*/
        if (!permission(dir,MAY_WRITE)) {
            iput(dir);
            return -EACCES;
        }
        /* 若可以在basename所在目录创建
         * basename,则在相应设备上为
         * basename创建一个i节点。*/
        inode = new_inode(dir->i_dev);
        if (!inode) {
            iput(dir);
            return -ENOSPC; /* 无空间错误码 */
        }
        /* 创建成功后初始化i节点,
         * i节点用户id为当前任务所属的用户id,
         * i节点对应文件的属性为实参mode,
         * 置i节点已被修改标志。*/
        inode->i_uid = current->euid;
        inode->i_mode = mode;
        inode->i_dirt = 1;
        /* 在basename所在目录添加basename的目录项 */
        bh = add_entry(dir,basename,namelen,&de);
        /* 若添加失败则释放相关资源, 返回无空间错误码 */
        if (!bh) {
            inode->i_nlinks--;
            iput(inode);
            iput(dir);
            return -ENOSPC;
        }
        /* 若basename的i节点分配成功,
         * 若basename在所在目录中添加成功目录项,
         * 则将basename的i节点号关联给其目录项。
         *
         * 同时置basename目录项所在缓冲区被修改标志。
         * 释放bh指向的缓冲区块和dir指向的i节点,
         * 将为basename创建的i节点地址赋给出参*res_inode. */
        de->inode = inode->i_num;
        bh->b_dirt = 1;
        brelse(bh);
        iput(dir);
        *res_inode = inode;
        return 0;
    }
    /* 若成功找到pathname最底层目录或文件
     * 的目录项则获取其i节点号和所关联的设备号。*/
    inr = de->inode;
    dev = dir->i_dev;
    brelse(bh);
    iput(dir);
    if (flag & O_EXCL)
        return -EEXIST;
    /* 获取设备分区号dev对应设备中i节点号为inr
     * 的节点并由inode指向。*/
    if (!(inode=iget(dev,inr)))
        return -EACCES;
    /* 若pathname中最底层路径为目录又带了读写访问标志,
     * 或者当前任务没有权限访问pathname中的最底层目录或文件
     * 则释放i节点并返回非法访问的错误。*/
    if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
        !permission(inode,ACC_MODE(flag))) {
        iput(inode);
        return -EPERM;
    }
    /* 修改pathname最底层目录或文件
     * 对应i节点的最后访问时间。
     * 若访问标志带了清0标志则将相应文件清0.
     * 最后将目标i节点地址赋给出参。*/
    inode->i_atime = CURRENT_TIME;
    if (flag & O_TRUNC)
        truncate(inode);
    *res_inode = inode;
    return 0;
}

/* [9] sys_mknod,
 * 在filename所在目录中再创建一个具mode属性的i节点与filename关联。*/
int sys_mknod(const char * filename, int mode, int dev)
{
    const char * basename;
    int namelen;
    struct m_inode * dir, * inode;
    struct buffer_head * bh;
    struct dir_entry * de;

    /* 若非超级任务则返回无权限错误码 */
    if (!suser())
        return -EPERM;
    /* 获取filename最底层目录(非'/'结尾)或文件于basename中,
     * 返回basename所在目录的i节点给dir。
     * 若basename长度为0则返回无入口的错误码。*/
    if (!(dir = dir_namei(filename,&namelen,&basename)))
        return -ENOENT;
    if (!namelen) {
        iput(dir);
        return -ENOENT;
    }
    /* 检查当前任务对basename所在目录
     * 是否具有写权限。*/
    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        return -EPERM;
    }
    /* 在basename所在目录查找basename的i节点,
     * 成功则返回包含basename i节点的缓冲区块
     * 地址于bh这说明basename应存在则返回已存在错误码,
     * basename i节点于de。*/
    bh = find_entry(&dir,basename,namelen,&de);
    if (bh) {
        brelse(bh);
        iput(dir);
        return -EEXIST;
    }
    
    /* 在basename所在目录对应设备上
     * 分配一个空闲i节点。*/
    inode = new_inode(dir->i_dev);
    if (!inode) {
        iput(dir);
        return -ENOSPC;
    }
    /* 若mode标识属性为块或字符设备,
     * 则在i节点i_zone[0]中存储设备号。*/
    inode->i_mode = mode;
    if (S_ISBLK(mode) || S_ISCHR(mode))
        inode->i_zone[0] = dev;
    inode->i_mtime = inode->i_atime = CURRENT_TIME;
    inode->i_dirt = 1;
    /* 将basename与刚创建的i节点关联 */
    bh = add_entry(dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        inode->i_nlinks=0;
        iput(inode);
        return -ENOSPC;
    }
    de->inode = inode->i_num;
    bh->b_dirt = 1;
    iput(dir);
    iput(inode);
    brelse(bh);
    return 0;
}

/* [10] sys_mkdir,
 * 以mode属性创建pathname表征的目录。*/
int sys_mkdir(const char * pathname, int mode)
{
    const char * basename;
    int namelen;
    struct m_inode * dir, * inode;
    struct buffer_head * bh, *dir_block;
    struct dir_entry * de;

    /* 超级任务才有权限 */
    if (!suser())
        return -EPERM;
    /* 获取pathname最底层目录名于basename, 返回basename
     * 所在目录i节点于dir。若无最底层目录名则返回无入口错误码。*/
    if (!(dir = dir_namei(pathname,&namelen,&basename)))
        return -ENOENT;
    if (!namelen) {
        iput(dir);
        return -ENOENT;
    }
    /* 检查当前任务对basename所在目录是否有写权限 */
    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        return -EPERM;
    }
    /* 若basenme目录已存在, 则返回已存在错误码 */
    bh = find_entry(&dir,basename,namelen,&de);
    if (bh) {
        brelse(bh);
        iput(dir);
        return -EEXIST;
    }
    /* 在dir对应设备上为basename
     * 目录分配一个i节点 */
    inode = new_inode(dir->i_dev);
    if (!inode) {
        iput(dir);
        return -ENOSPC;
    }
    /* 目录初始大小,会在后续在该目录中包含'.'和'..'两个目录项 */
    inode->i_size = 32;
    inode->i_dirt = 1;
    inode->i_mtime = inode->i_atime = CURRENT_TIME;
    /* 为目录分配一块逻辑块,逻辑块号存在i_zone[0]中 */
    if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
        iput(dir);
        inode->i_nlinks--;
        iput(inode);
        return -ENOSPC;
    }
    inode->i_dirt = 1;
    /* 读取新建目录的第一逻辑块到缓冲区块中由dir_block指向 */
    if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {
        iput(dir);
        free_block(inode->i_dev,inode->i_zone[0]);
        inode->i_nlinks--;
        iput(inode);
        return -ERROR;
    }
    /* 在新建目录中默认包含'.'和'..'两个目录项,
     * 这两个目录的i节点号为新建目录i节点的i节点号,
     * 所以新建目录的初始化链接数为2。*/
    de = (struct dir_entry *) dir_block->b_data;
    de->inode=inode->i_num;
    strcpy(de->name,".");
    de++;
    de->inode = dir->i_num;
    strcpy(de->name,"..");
    inode->i_nlinks = 2;
    dir_block->b_dirt = 1;
    brelse(dir_block);
    /* 为新建i节点添加目录属性和当前任务所允许的任务 */
    inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
    inode->i_dirt = 1;
    /* 将basename i节点加到dir目录中 */
    bh = add_entry(dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        free_block(inode->i_dev,inode->i_zone[0]);
        inode->i_nlinks=0;
        iput(inode);
        return -ENOSPC;
    }
    /* basename目录项的i节点号 */
    de->inode = inode->i_num;
    bh->b_dirt = 1;
    dir->i_nlinks++; /* 增加basename所在目录的链接数 */
    dir->i_dirt = 1;
    iput(dir);
    iput(inode);
    brelse(bh);
    return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
/* [11] empty_dir,
 * 检查inode所指i节点对应目录是否为空,
 * 是则返回1, 否则返回0。*/
static int empty_dir(struct m_inode * inode)
{
    int nr,block;
    int len;
    struct buffer_head * bh;
    struct dir_entry * de;

    /* 计算inode所指i节点对应目录的目录项数 */
    len = inode->i_size / sizeof (struct dir_entry);

    /* 读inode所指i节点对应目录的第1逻辑块,
     * 判断该目录的前两个目录项是否为'.'和'..'。*/
    if (len<2 || !inode->i_zone[0] ||
        !(bh=bread(inode->i_dev,inode->i_zone[0]))) {
            printk("warning - bad directory on dev %04x\n",inode->i_dev);
        return 0;
    }
    de = (struct dir_entry *) bh->b_data;
    if (de[0].inode != inode->i_num || !de[1].inode || 
        strcmp(".",de[0].name) || strcmp("..",de[1].name)) {
        printk("warning - bad directory on dev %04x\n",inode->i_dev);
        return 0;
    }

    nr = 2;
    de += 2;
    while (nr<len) {
        if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {
            brelse(bh);
            /* 获取第nr个目录项所在逻辑块的块号 */
            block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);
            if (!block) {
                nr += DIR_ENTRIES_PER_BLOCK;
                continue;
            }
            /* 读取第nr个目录项所在逻辑块到bh指向的缓冲区块中 */
            if (!(bh=bread(inode->i_dev,block)))
                return 0;
            de = (struct dir_entry *) bh->b_data;
        }
        /* 如果inode所指i节点对应目录还包含目录项则返回0 */
        if (de->inode) {
            brelse(bh);
            return 0;
        }
        de++;
        nr++;
    }
    /* 若inode对应目录为NULL, 则返回1 */
    brelse(bh);
    return 1;
}

/* [12] sys_rmdir,
 * 删除name对应的目录。*/
int sys_rmdir(const char * name)
{
    const char * basename;
    int namelen;
    struct m_inode * dir, * inode;
    struct buffer_head * bh;
    struct dir_entry * de;

    if (!suser())
        return -EPERM;

    /* 获取name中最底层目录名于basename中,
     * 将basename所在上层目录的i节点地址返回给dir。*/
    if (!(dir = dir_namei(name,&namelen,&basename)))
        return -ENOENT;
    if (!namelen) {
        iput(dir);
        return -ENOENT;
    }
    /* 检查basename所在目录是否具有写属性 */
    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        return -EPERM;
    }
    /* 在basename所在目录中寻找basename的目录项 */
    bh = find_entry(&dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        return -ENOENT;
    }
    /* 在basename所在目录根据basename对应i节点号获取其i节点 */
    if (!(inode = iget(dir->i_dev, de->inode))) {
        iput(dir);
        brelse(bh);
        return -EPERM;
    }
    if ((dir->i_mode & S_ISVTX) && current->euid &&
        inode->i_uid != current->euid) {
        iput(dir);
        iput(inode);
        brelse(bh);
        return -EPERM;
    }
    if (inode->i_dev != dir->i_dev || inode->i_count>1) {
        iput(dir);
        iput(inode);
        brelse(bh);
        return -EPERM;
    }
    if (inode == dir) { /* we may not delete ".", but "../dir" is ok */
        iput(inode);
        iput(dir);
        brelse(bh);
        return -EPERM;
    }
    if (!S_ISDIR(inode->i_mode)) {
        iput(inode);
        iput(dir);
        brelse(bh);
        return -ENOTDIR;
    }
    if (!empty_dir(inode)) {
        iput(inode);
        iput(dir);
        brelse(bh);
        return -ENOTEMPTY;
    }
    if (inode->i_nlinks != 2)
        printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);
    de->inode = 0;
    bh->b_dirt = 1;
    brelse(bh);
    inode->i_nlinks=0;
    inode->i_dirt=1;
    dir->i_nlinks--;
    dir->i_ctime = dir->i_mtime = CURRENT_TIME;
    dir->i_dirt=1;
    iput(dir);
    iput(inode);
    return 0;
}

/* [13] sys_unlink,
 * 减少name文件的链接数。*/
int sys_unlink(const char * name)
{
    const char * basename;
    int namelen;
    struct m_inode * dir, * inode;
    struct buffer_head * bh;
    struct dir_entry * de;

    /* 获取name所在目录i节点指针于dir, name中目标名于basename */
    if (!(dir = dir_namei(name,&namelen,&basename)))
        return -ENOENT;
    if (!namelen) {
        iput(dir);
        return -ENOENT;
    }
    /* 当前任务对name上层目录具写属性否 */
    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        return -EPERM;
    }
    /* 在name上层目录寻找name的目录项 */
    bh = find_entry(&dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        return -ENOENT;
    }
    /* 由name目录项中的i节点号在name上层目录中寻找
     * name的i节点由inode指向。*/
    if (!(inode = iget(dir->i_dev, de->inode))) {
        iput(dir);
        brelse(bh);
        return -ENOENT;
    }
    if ((dir->i_mode & S_ISVTX) && !suser() &&
        current->euid != inode->i_uid &&
        current->euid != dir->i_uid) {
        iput(dir);
        iput(inode);
        brelse(bh);
        return -EPERM;
    }
    if (S_ISDIR(inode->i_mode)) {
        iput(inode);
        iput(dir);
        brelse(bh);
        return -EPERM;
    }
    if (!inode->i_nlinks) {
        printk("Deleting nonexistent file (%04x:%d), %d\n",
            inode->i_dev,inode->i_num,inode->i_nlinks);
        inode->i_nlinks=1;
    }
    de->inode = 0;
    bh->b_dirt = 1;
    brelse(bh);
    inode->i_nlinks--;
    inode->i_dirt = 1;
    inode->i_ctime = CURRENT_TIME;
    iput(inode);
    iput(dir);
    return 0;
}

/* [14] sys_link,
 * 将newname链接到oldname。
 * 即创建newname的目录项并将oldname的i节点号赋给newname 目录项的i节点号。
 * 则通过newname目录项寻找其i节点时就会找到oldname的i节点,
 * 最终会访问到oldname这个文件。*/
int sys_link(const char * oldname, const char * newname)
{
    struct dir_entry * de;
    struct m_inode * oldinode, * dir;
    struct buffer_head * bh;
    const char * basename;
    int namelen;

    /* 获取oldname的i节点地址,
     * 若其为目录则返回无操作权限错误码。*/
    oldinode=namei(oldname);
    if (!oldinode)
        return -ENOENT;
    if (S_ISDIR(oldinode->i_mode)) {
        iput(oldinode);
        return -EPERM;
    }

    /* 获取newname上层目录i节点于dir,
     * 获取newname有效名于basename.*/
    dir = dir_namei(newname,&namelen,&basename);
    if (!dir) {
        iput(oldinode);
        return -EACCES;
    }
    if (!namelen) {
        iput(oldinode);
        iput(dir);
        return -EPERM;
    }
    /* 检查两者设备分区号是否相同 */
    if (dir->i_dev != oldinode->i_dev) {
        iput(dir);
        iput(oldinode);
        return -EXDEV;
    }
    /* 检查当前任务对newname所在目录是否具写权限 */
    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        iput(oldinode);
        return -EACCES;
    }
    /* 查看newname在其目录中是否已经存在,
     * 若存在则返回已存在错误码。*/
    bh = find_entry(&dir,basename,namelen,&de);
    if (bh) {
        brelse(bh);
        iput(dir);
        iput(oldinode);
        return -EEXIST;
    }
    /* 将basename添加到其上层目录中 */
    bh = add_entry(dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        iput(oldinode);
        return -ENOSPC;
    }
    /* 将oldname对应i节点号赋给新增的目录项的i节点号 */
    de->inode = oldinode->i_num;
    bh->b_dirt = 1;
    brelse(bh);
    iput(dir);
    oldinode->i_nlinks++; /* 增加oldname i节点链接数 etc. */
    oldinode->i_ctime = CURRENT_TIME;
    oldinode->i_dirt = 1;
    iput(oldinode);
    return 0;
}

