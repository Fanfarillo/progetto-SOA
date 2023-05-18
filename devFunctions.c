#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/fs.h>

#include "filesystem/singlefilefs.h"
//#define MOD_NAME "SINGLE FILE FS"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size)
#else
asmlinkage int sys_put_data(char *source, size_t size)
#endif
{
    //TODO: implementare la system call
    printk("%s: questa è l'implementazione dummy della system call put_data()\n", MOD_NAME);
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size)
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size)
#endif
{
    //TODO: implementare la system call
    printk("%s: questa è l'implementazione dummy della system call get_data()\n", MOD_NAME);
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset)
#else
asmlinkage int sys_invalidate_data(int offset)
#endif
{
    //TODO: implementare la system call
    printk("%s: questa è l'implementazione dummy della system call invalidate_data()\n", MOD_NAME);
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;
long sys_get_data = (unsigned long) __x64_sys_get_data;
long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#endif
