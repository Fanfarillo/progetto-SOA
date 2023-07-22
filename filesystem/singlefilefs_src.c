#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0)
#include <linux/atomic.h>
#else
#include <asm/atomic_32.h>
#endif

#include "singlefilefs.h"
#include "singlefilefs_ker.h"
#include "singlefilefs_init.h"

static struct super_operations singlefilefs_super_ops = {
};

static struct dentry_operations singlefilefs_dentry_ops = {
};

//qui iniziano le variabili globali definite direttamente da me
struct auxiliary_info au_info = {0};
struct super_block *global_sb;

//funzione che ha il compito di istanziare il superblocco del filesystem "singlefilefs"
int singlefilefs_fill_super(struct super_block *sb, void *data, int silent) {   

    struct inode *root_inode;
    struct buffer_head *bh;
    struct onefilefs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;

    //qui iniziano le variabili locali definite direttamente da me
    struct onefilefs_inode *inode_disk;
    int num_mounted_blocks;
    int num_expected_blocks;

    //controllo preliminare sulla dimensione della struct onefilefs_sb_info (che mantiene tutti i dati del superblocco): se eccede la dimensione di un blocco, c'è un GROSSO problema.
    if (sizeof(struct onefilefs_sb_info) > DEFAULT_BLOCK_SIZE) {
        return -ENOMEM; //-ENOMEM = errore dovuto a una quantità di memoria a disposizione insufficiente
    }
    //inizializzazione di global_sb
    global_sb = sb;

    //unique identifier of the file system
    sb->s_magic = MAGIC;

    //lettura del superblocco del file ystem
    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if (!(sb && bh)){
	    return -EIO;    //-EIO = errore di input/output
    }
    sb_disk = (struct onefilefs_sb_info *)bh->b_data;
    magic = sb_disk->magic; //estrazione del magic number a partire dalle informazioni ottenute con sb_bread()
    num_expected_blocks = sb_disk->total_data_blocks;   //estrazione del numero massimo di blocchi che è stato imposto a tempo di compilazione (DATA_BLOCKS)

    brelse(bh);  //rilascio del buffer head bh

    //lettura dell'inode dell'unico file del file system
    bh = sb_bread(sb, SINGLEFILEFS_FILE_INODE_NUMBER);
    if (!sb){
        return -EIO;    //-EIO = errore di input/output
    }
    inode_disk = (struct onefilefs_inode *)bh->b_data;
    num_mounted_blocks = (inode_disk->file_size)/DEFAULT_BLOCK_SIZE;

    //check sul numero di blocchi effettivamente allocati, che non deve essere superiore a quello stabilito a tempo di compilazione (DATA_BLOCKS)
    if (num_mounted_blocks > num_expected_blocks) {
        brelse(bh); //rilascio del buffer head bh
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso struct super_block *sb)
    }

    //check on the expected magic number
    if (magic != sb->s_magic){
	    return -EBADF;  //-EBADF = file descriptor non valido
    }

    sb->s_fs_info = NULL; //FS specific data (the magic number) already reported into the generic superblock
    sb->s_op = &singlefilefs_super_ops;

    //di seguito verrà allocato un inode per la root del file system
    root_inode = iget_locked(sb, 0);//get a root inode indexed with 0 from cache
    if (!root_inode){
        return -ENOMEM; //-ENOMEM = errore di esaurimento della memoria
    }

    /* inizializzazione dell'inode con le informazioni necessarie, come:
     * i_sb: informazioni del superblocco
     * i_op: inode operations
     * i_fop: file operations
     * i_mode: permessi di accesso
     * i_atime: timestamp
     */
    root_inode->i_ino = SINGLEFILEFS_ROOT_INODE_NUMBER; //this is actually 10
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(6,3,0)
        inode_init_owner(&nop_mnt_idmap, root_inode, NULL, S_IFDIR);    //set the root user as owned of the FS root
    #elif LINUX_VERSION_CODE >= KERNEL_VERSION(5,12,0)
        inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR);
    #else
        inode_init_owner(root_inode, NULL, S_IFDIR);
    #endif
    root_inode->i_sb = sb;
    root_inode->i_op = &onefilefs_inode_ops;        //set our inode operations
    root_inode->i_fop = &onefilefs_dir_operations;  //set our file operations
    //update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    //baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    //no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    //sb->s_root = puntatore al root inode
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM; //-ENOMEM = errore di esaurimento della memoria

    sb->s_root->d_op = &singlefilefs_dentry_ops;//set our dentry operations

    //unlock the inode to make it usable
    unlock_new_inode(root_inode);

    printk("%s: singlefilefs_fill_super() function executed successfully\n", MOD_NAME);
    return 0;
    
}

//called on file system unmounting
//funzione che ha il compito di eliminare il superblocco del filesystem
static void singlefilefs_kill_superblock(struct super_block *s) {

    //qui iniziano le variabili locali definite direttamente da me
    long unsigned int cmp_swap_output;

    if (atomic_read(&(au_info.usages)) != 0) {
        printk("%s: impossible to unmount the file system: some thread is executing some fs operations\n", MOD_NAME);
        return;
    }

    cmp_swap_output = __sync_val_compare_and_swap(&(au_info.is_mounted), 1, 0);
    if (cmp_swap_output != 1) { //caso in cui il file system non risultava montato
        printk("%s: impossible to unmount the file system: it was already unmounted\n", MOD_NAME);
        return;
    }

    cleanup_srcu_struct(&(au_info.srcu));   //cleanup struct srcu_struct
    kill_block_super(s);    //è lei che esegue effettivamente l'eliminazione del superblocco, eliminando le risorse ad esso associate.
    printk("%s: singlefilefs unmount successful\n", MOD_NAME);
    return;

}

//called on file system mounting
//funzione che ha il compito di allocare e inizializzare una nuova struttura dentry, che rappresenta la directory root del file system.
struct dentry *singlefilefs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    struct dentry *ret;

    //qui iniziano le variabili locali definite direttamente da me
    static DEFINE_MUTEX(w_mutex);   //dichiarazione e definizione del mutex per le scritture
    static DEFINE_MUTEX(o_mutex);   //dichiarazione e definizione del mutex per il parametro *off di dev_read()
    int init_srcu_output;
    long unsigned int cmp_swap_output;

    //inizializzazione dei campi di tipo struct mutex e struct srcu_struct
    au_info.write_mutex = w_mutex;
    au_info.off_mutex = o_mutex;
    
    init_srcu_output = init_srcu_struct(&(au_info.srcu));
    if (init_srcu_output != 0) {    //error
        printk("%s: error mounting onefilefs", MOD_NAME);
        return ERR_PTR(-ENOMEM);    //-ENOMEM = errore di esaurimento di memoria; è una causa tipica del fallimento di init_srcu_struct().
    }

    cmp_swap_output = __sync_val_compare_and_swap(&(au_info.is_mounted), 0, 1);
    if (cmp_swap_output != 0) { //caso in cui il file system era già montato
        printk("%s: singlefilefs was already mounted\n", MOD_NAME);
        return ERR_PTR(-EEXIST);    //-EEXIST = file system già esistente; uso ERR_PTR perché devo restituire un valore di tipo pointer.
    }

    /*@param fs_type: tipo di file system
     *@param flags: opzioni di montaggio
     *@param dev_name: nome del dispositivo su cui montare il file system
     *@param data: puntatore ai dati di montaggio
     *@param singlefilefs_fill_super: puntatore a una funzione di callback che viene usata per inizializzare il superblocco del filesystem
     *è questa funzione che monta il file system sul dispositivo specificato e crea la struttura dentry per il filesystem.
     */
    ret = mount_bdev(fs_type, flags, dev_name, data, singlefilefs_fill_super);

    if (unlikely(IS_ERR(ret)))  //unlikely() è il duale di likely().
        printk("%s: error mounting onefilefs", MOD_NAME);
    else
        printk("%s: singlefilefs is successfully mounted on from device %s\n", MOD_NAME, dev_name);

    return ret;

}

//file system type: describes file system structure
static struct file_system_type onefilefs_type = {
	.owner      = THIS_MODULE,                  //modulo proprietario del filesystem (i.e. modulo che ha creato l'istanza di file system)
    .name       = "singlefilefs",               //nome del file system; viene usato per il montaggio del file system stesso.
    .mount      = singlefilefs_mount,           //funzione da chiamare quando si vuole montare il file system
    .kill_sb    = singlefilefs_kill_superblock, //funzione da chiamare quando si vuole eliminare un superblocco associato al filesystem
};
