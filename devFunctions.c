#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/version.h>

#include <stdio.h>  //solo per la printf() che sicuramente toglierò

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size)
#else
asmlinkage int sys_put_data(char *source, size_t size)
#endif
{
    //TODO: implementare la system call
    printf("Questa è l'implementazione dummy della system call put_data().\n");
    fflush(stdout);
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size)
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size)
#endif
{
    //TODO: implementare la system call
    printf("Questa è l'implementazione dummy della system call get_data().\n");
    fflush(stdout);
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset)
#else
asmlinkage int sys_invalidate_data(int offset)
#endif
{
    //TODO: implementare la system call
    printf("Questa è l'implementazione dummy della system call invalidate_data().\n");
    fflush(stdout);
    return 0;
}
