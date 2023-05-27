#include <linux/fdtable.h>
#include <linux/file.h>
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
    struct onefilefs_sb_info *sb_struct;

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
    sb_struct = get_superblock_info();
    rcu_read_unlock();

    printk("%s: la system call put_data() è stata eseguita con successo\n", MOD_NAME);
    return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size)
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size)
#endif
{   
    int i, ret;
    struct onefilefs_sb_info *sb_struct;
    struct list_head *head;
    struct list_head *curr;
    struct rcu_node *node_to_read;
    int fd;                         //file descriptor da utilizzare per il nostro dispositivo ("image")
    struct file *f;                 //struttura che descrive il nostro dispositivo
    loff_t bytes_offset;            //valore (offset) che indica il punto del dispositivo da cui deve iniziare la lettura
    loff_t *pos;                    //puntatore che indica il punto del dispositivo da cui deve iniziare la lettura
    int bytes_read;

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
    sb_struct = get_superblock_info();
    rcu_read_unlock();

    if (sb_struct == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb_struct->user_sb.total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
        printk("%s: impossibile eseguire la system call get_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    if (size > DEFAULT_BLOCK_SIZE-METADATA_SIZE) {
        size = DEFAULT_BLOCK_SIZE-METADATA_SIZE;    //in tal modo si leggono esclusivamente i dati posti nel blocco
    }

    //sincronizzazione RCU per l'operazione di lettura
    head = &(sb_struct->rcu_head);
    rcu_read_lock();

    curr = get_first_data_block(head);
    for(i=0; i<offset; i++) {
        curr = list_next_or_null_rcu(head, curr, struct rcu_node, lh);
    }

    /* Funzione che restituisce il puntatore alla struct rcu_node contenente la struct_list head specificata.
     *@param curr: puntatore alla struct list_head dell'elemento della RCU list di cui si vuole recuperare l'indirizzo base
     *@param struct rcu_node: tipo di dato all'interno del quale è embeddato la list_head (i.e. tipo di dato dei nodi della RCU list)
     *@param lh: nome del campo di tipo list_head all'interno dei nodi della RCU list
     */
    node_to_read = list_entry_rcu(curr, struct rcu_node, lh);

    //check sulla validità del blocco target
    if(!(node_to_read->is_valid)) {
        printk("%s: impossibile eseguire la system call get_data(): il blocco specificato (%d) non è valido\n", MOD_NAME, offset);
        return -ENODATA; //-ENODATA = nessun dato disponibile
    }

    //preparazione alla lettura effettiva del blocco
    fd = get_unused_fd_flags(0);    //ottenimento di un file descriptor disponibile
    if (fd < 0) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore nell'ottenimento di un file descriptor\n", MOD_NAME);
        return -EIO;    //-EIO = errore di input/output
    }
    f = filp_open(IMAGE_NAME, O_RDONLY, 0); //apertura del dispositivo in modalità sola lettura
    if (IS_ERR(f)) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore nell'apertura del dispositivo\n", MOD_NAME);
        return -EIO;    //-EIO = errore di input/output
    }

    /* Per ottenere l'offset iniziale, mi devo spostare di:
     * 2+offset blocchi, che sono tutti quelli che precedono il target (compresi superblocco e inode del file)
     * METADATA_SIZE byte, perché voglio leggere esclusivamente il payload
     */
    bytes_offset = (2+offset)*DEFAULT_BLOCK_SIZE + METADATA_SIZE;
    pos = &bytes_offset;

    //lettura effettiva del blocco
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
    ret = kernel_read(f, destination, size, pos);
    #else
    ret = vfs_read(f, destination, size, pos);
    #endif
    
    if (ret < 0) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore durante la lettura del blocco %d\n", MOD_NAME, offset);
        filp_close(f, NULL);
        return -EIO;    //-EIO = errore di input/output
    }

    //clean up
    filp_close(f, NULL);
    rcu_read_unlock();

    //considero il numero di byte letti come la lunghezza della stringa letta (eventualmente fino a '\0'); in mancanza di '\0', verrà restituito ret.
    bytes_read = (int)strnlen(destination, ret);
    if (bytes_read < ret)
        bytes_read++;   //strnlen() non conta l'eventuale '\0': lo aggiungo a mano.

    printk("%s: la system call get_data() è stata eseguita con successo\n", MOD_NAME);
    return bytes_read;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset)
#else
asmlinkage int sys_invalidate_data(int offset)
#endif
{
    struct onefilefs_sb_info *sb_struct;

    //sanity checks
    if (!m_info.is_mounted) {
        printk("%s: impossibile eseguire la system call invalidate_data(): il file system non è stato montato\n", MOD_NAME);
        return -ENODEV; //-ENODEV = file system non esistente
    }

    //TODO: capire se è sufficiente chiamare semplicemente una rcu_read_lock() e una rcu_read_unlock() nel caso in cui bisogna leggere solo dal dispositivo e non dalla RCU list.
    rcu_read_lock();
    sb_struct = get_superblock_info();
    rcu_read_unlock();

    if (sb_struct == NULL) {
        printk("%s: impossibile eseguire la system call invalidate_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb_struct->user_sb.total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
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
