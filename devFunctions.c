#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/version.h>

#include "filesystem/singlefilefs.h"
#include "devFunctions.h"
#include "utils.c"

//SYSTEM CALLS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size)
#else
asmlinkage int sys_put_data(char *source, size_t size)
#endif
{
    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile eseguire l'operazione put_data(): il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if (size > DEFAULT_BLOCK_SIZE-METADATA_SIZE) {
        printk("%s: impossibile eseguire l'operazione put_data(): la dimensione dei dati da scrivere eccede la dimensione di un blocco\n", MOD_NAME);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso size_t size)
    }
    if (size <= 0 || source == NULL) {
        printk("%s: impossibile eseguire l'operazione put_data(): non vi sono dati da scrivere\n", MOD_NAME);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso size_t size e/o char *source)
    }

    printk("%s: questa è l'implementazione dummy della system call put_data()\n", MOD_NAME);
    return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size)
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size)
#endif
{   
    struct onefilefs_sb_info *sb;

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile eseguire l'operazione get_data(): il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if (destination == NULL) {
        printk("%s: impossibile eseguire l'operazione get_data(): non è stato specificato alcun buffer di destinazione\n", MOD_NAME);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso char *destination)
    }
    if (size <= 0) {    //il caso size>DEFAULT_BLOCK_SIZE-METADATA_SIZE viene accettato e omologato al caso size==DEFAULT_BLOCK_SIZE-METADATA_SIZE
        printk("%s: impossibile eseguire l'operazione get_data(): la quantità di dati da leggere deve essere strettamente positiva\n", MOD_NAME);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso size_t size)
    }

    sb = get_superblock_info();
    if (sb == NULL) {
        printk("%s: impossibile eseguire l'operazione get_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb->total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
        printk("%s: impossibile eseguire l'operazione get_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    printk("%s: questa è l'implementazione dummy della system call get_data()\n", MOD_NAME);
    return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset)
#else
asmlinkage int sys_invalidate_data(int offset)
#endif
{
    struct onefilefs_sb_info *sb;

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile eseguire l'operazione invalidate_data(): il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }

    sb = get_superblock_info();
    if (sb == NULL) {
        printk("%s: impossibile eseguire l'operazione invalidate_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb->total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
        printk("%s: impossibile eseguire l'operazione invalidate_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    printk("%s: questa è l'implementazione dummy della system call invalidate_data()\n", MOD_NAME);
    return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;
long sys_get_data = (unsigned long) __x64_sys_get_data;
long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#endif

//FILE OPERATIONS
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile leggere il dispositivo: il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }

    printk("%s: device successfully read\n", MOD_NAME);
  	return 0;

}

static int dev_open(struct inode *inode, struct file *file) {

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile aprire il dispositivo: il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if ((file->f_mode & FMODE_WRITE) || (file->f_mode & FMODE_APPEND)) {    //il dispositivo deve essere aperto in read only
        printk("%s: impossibile aprire il dispositivo in modalità scrittura o append\n", MOD_NAME);
        return -EPERM;  //-EPERM = operazione non consentita
    }

    printk("%s: device successfully opened\n", MOD_NAME);
  	return 0;

}

static int dev_release(struct inode *inode, struct file *file) {

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile chiudere il dispositivo: il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }

    printk("%s: device successfully closed\n", MOD_NAME);
  	return 0;

}

static struct file_operations fops = {
  .owner = THIS_MODULE,
  .read = dev_read,
  .open = dev_open,
  .release = dev_release,
};
