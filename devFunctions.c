#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/version.h>

#include "filesystem/singlefilefs.h"
#include "filesystem/singlefilefs_init.h"
#include "filesystem/singlefilefs_ker.h"
#include "devFunctions.h"
#include "utils.c"

//TODO: verificare che all'interno dei data block i metadati siano stati scritti correttamente.
//SYSTEM CALLS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size)
#else
asmlinkage int sys_put_data(char *source, size_t size)
#endif
{
    struct onefilefs_sb_info *sb_disk;

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile eseguire la system call put_data(): il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if (size > DEFAULT_BLOCK_SIZE-METADATA_SIZE) {
        printk("%s: impossibile eseguire la system call put_data(): la dimensione dei dati da scrivere eccede la dimensione di un blocco\n", MOD_NAME);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso size_t size)
    }
    if (size <= 0 || source == NULL) {
        printk("%s: impossibile eseguire la system call put_data(): non vi sono dati da scrivere\n", MOD_NAME);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso size_t size e/o char *source)
    }

    //TODO: capire se è sufficiente chiamare semplicemente una rcu_read_lock() e una rcu_read_unlock() nel caso in cui bisogna leggere solo dal dispositivo e non dalla RCU list.
    rcu_read_lock();
    sb_disk = get_superblock_info(global_sb);
    rcu_read_unlock();

    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        return -EIO; //-EIO = errore di input/output
    }

    printk("%s: la system call put_data() è stata eseguita con successo\n", MOD_NAME);
    return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size)
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size)
#endif
{   
    int index;                      //variabile che tiene traccia del numero di iterazione all'interno del ciclo che itera sulla RCU list
    struct onefilefs_sb_info *sb_disk;
    struct data_block_content *db_cont;
    struct rcu_node *curr_node;
    int readable_bytes;             //numero di byte effettivamente presentu nel blocco target prima del terminatore di stringa ('\0')
    int lost_bytes_copy_to_user;    //numero di byte (tra quelli letti con kernel_read()) che non è stato possibile consegnare all'utente con copy_to_user()

    //sanity checks (notare che il caso size>DEFAULT_BLOCK_SIZE-METADATA_SIZE viene accettato e omologato al caso size==DEFAULT_BLOCK_SIZE-METADATA_SIZE)
    if (!m_info.is_mounted) {
        printk("%s: impossibile eseguire la system call get_data(): il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if (destination == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): non è stato specificato alcun buffer di destinazione\n", MOD_NAME);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso char *destination)
    }
   
    //TODO: capire se è sufficiente chiamare semplicemente una rcu_read_lock() e una rcu_read_unlock() nel caso in cui bisogna leggere solo dal dispositivo e non dalla RCU list.
    rcu_read_lock();
    sb_disk = get_superblock_info(global_sb);
    rcu_read_unlock();

    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb_disk->user_sb.total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
        printk("%s: impossibile eseguire la system call get_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    if (size > DEFAULT_BLOCK_SIZE-METADATA_SIZE) {
        size = DEFAULT_BLOCK_SIZE-METADATA_SIZE;    //in tal modo si leggono esclusivamente i dati posti nel blocco
    }

    //sincronizzazione RCU per l'operazione di lettura
    rcu_read_lock();
    index = 0;
    curr_node = NULL;

    /* Costrutto che itera su tutti i nodi della RCU list
     *@param curr_node: struct rcu_node che, a ogni iterazione del ciclo, tiene traccia del nodo corrente della RCU list
     *@param &(sb_disk->rcu_head): puntatore alla list head dell'elemento artificiale del superblock
     *@param lh: campo della struct rcu_node di tipo struct list_head
     */
    list_for_each_entry_rcu(curr_node, &(sb_disk->rcu_head), lh) {
        if (index == offset+2)    //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
            break;        
        index++;

    }

    //check sulla validità del blocco target
    if(!(curr_node->is_valid)) {
        printk("%s: impossibile eseguire la system call get_data(): il blocco specificato (%d) non è valido\n", MOD_NAME, offset);
        return -ENODATA; //-ENODATA = nessun dato disponibile
    }

    //recupero del contenuto del blocco da leggere
    db_cont = get_block_content(global_sb, offset+2);   //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
    if (db_cont == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore col recupero dei dati del blocco %d\n", MOD_NAME, offset);
        return -EIO; //-EIO = errore di input/output
    }

    //considero il numero di byte letti come la lunghezza della stringa letta (eventualmente fino a '\0'); in mancanza di '\0', verrà restituito ret.
    readable_bytes = (int)strnlen(&(db_cont->payload[0]), size);
    if (readable_bytes == 0) {
        printk("%s: la system call get_data() è stata eseguita con successo ma non ci sono dati da leggere\n", MOD_NAME);
        return 0;
    }
    else if (readable_bytes > 0 && readable_bytes < size)
        readable_bytes++;   //strnlen() non conta l'eventuale '\0': lo aggiungo a mano.

    //consegna dei dati all'utente
    lost_bytes_copy_to_user = copy_to_user(destination, &(db_cont->payload[0]), readable_bytes);

    rcu_read_unlock();
    printk("%s: la system call get_data() è stata eseguita con successo\n", MOD_NAME);
    return readable_bytes - lost_bytes_copy_to_user;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset)
#else
asmlinkage int sys_invalidate_data(int offset)
#endif
{
    struct onefilefs_sb_info *sb_disk;

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile eseguire la system call invalidate_data(): il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }

    //TODO: capire se è sufficiente chiamare semplicemente una rcu_read_lock() e una rcu_read_unlock() nel caso in cui bisogna leggere solo dal dispositivo e non dalla RCU list.
    rcu_read_lock();
    sb_disk = get_superblock_info(global_sb);
    rcu_read_unlock();

    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call invalidate_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb_disk->user_sb.total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
        printk("%s: impossibile eseguire la system call invalidate_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    printk("%s: la system call invalidate_data() è stata eseguita con successo\n", MOD_NAME);
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

//la dev_read() legge i dati dal blocco del dispositivo corrispondente alla posizione indicata dall'offset off.
static ssize_t dev_read(struct file *filp, char *buf, size_t len, loff_t *off) {

    struct buffer_head *bh = NULL;
    struct inode * the_inode = filp->f_inode;
    uint64_t file_size = the_inode->i_size;
    int ret;
    loff_t offset;
    int block_to_read;//index of the block to be read from device

    printk("%s: read operation called with len %ld - and offset %lld (the current file size is %lld)",MOD_NAME, len, *off, file_size);

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile leggere il dispositivo: il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }

    //QUI INIZIA IL CODICE FORNITO DAL PROFESSORE

    //this operation is not synchronized 
    //param *off can be changed concurrently 
    //add synchronization if you need it for any reason

    //check that *off is within boundaries of file size
    if (*off >= file_size)
        return 0;
    else if (*off + len > file_size)
        len = file_size - *off;

    //determine the block level offset for the operation
    offset = *off % DEFAULT_BLOCK_SIZE; 
    //just read stuff in a single block - residuals will be managed at the applicatin level
    if (offset + len > DEFAULT_BLOCK_SIZE)
        len = DEFAULT_BLOCK_SIZE - offset;

    //compute the actual index of the the block to be read from device
    block_to_read = *off / DEFAULT_BLOCK_SIZE + 2; //the value 2 accounts for superblock and file-inode on device (block 0 & block 1)
    
    printk("%s: read operation must access block %d of the device", MOD_NAME, block_to_read);

    //sb_read acquisisce il contenuto del blocco da leggere (quello di cui abbiamo appena calcolato l'indice).
    bh = (struct buffer_head *)sb_bread(filp->f_path.dentry->d_inode->i_sb, block_to_read);
    if(!bh){
	    return -EIO;
    }
    //ora si copiano i dati dal buffer del kernel (bh->b_data+offset) al buffer dell'applicazione (buf), passato come parametro a onefilefs_read().
    ret = copy_to_user(buf,bh->b_data + offset, len);
    *off += (len - ret);
    brelse(bh);

    printk("%s: device successfully read\n", MOD_NAME);
    return len - ret;   //return: numero di byte effettivamente letti e copiati

}

//la dev_open() apre il dispositivo (deve farlo in modalità di sola scrittura).
static int dev_open(struct inode *inode, struct file *file) {

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile aprire il dispositivo: il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if (file->f_mode & FMODE_WRITE) {    //il dispositivo deve essere aperto in read only
        printk("%s: impossibile aprire il dispositivo in modalità scrittura\n", MOD_NAME);
        return -EPERM;  //-EPERM = operazione non consentita
    }

    printk("%s: device successfully opened\n", MOD_NAME);
  	return 0;

}

//la dev_release() chiude il dispositivo.
static int dev_release(struct inode *inode, struct file *file) {

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile chiudere il dispositivo: il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }

    printk("%s: device successfully closed\n", MOD_NAME);
  	return 0;

}

const struct file_operations fops = {
  .owner = THIS_MODULE,
  .read = dev_read,
  .open = dev_open,
  .release = dev_release,
};
