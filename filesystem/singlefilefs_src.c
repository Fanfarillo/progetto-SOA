#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "singlefilefs.h"

//direttamente aggiunti da me
#include "singlefilemakefs.h"

static struct super_operations singlefilefs_super_ops = {
};

static struct dentry_operations singlefilefs_dentry_ops = {
};

//funzione che ha il compito di inizializzare il superblocco del filesystem "singlefilefs"
int singlefilefs_fill_super(struct super_block *sb, void *data, int silent) {   

    struct inode *root_inode;
    struct buffer_head *bh;
    struct onefilefs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;

    //qui iniziano le variabili locali definite direttamente da me
    struct onefilefs_inode *inode_disk;
    int num_mounted_blocks;

    //unique identifier of the file system
    sb->s_magic = MAGIC;

    //lettura del superblocco del file ystem
    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if(!sb){
	    return -EIO;    //-EIO = errore di input/output
    }
    sb_disk = (struct onefilefs_sb_info *)bh->b_data;
    magic = sb_disk->magic; //estrazione del magic number a partire dalle informazioni ottenute con sb_bread()
    brelse(bh);             //rilascio del blocco bh (i.e. del superblocco del file system)

    //lettura dell'inode dell'unico file del file system
    bh = sb_bread(sb, SINGLEFILEFS_FILE_INODE_NUMBER);
    if(!sb){
        return -EIO;    //-EIO = errore di input/output
    }
    inode_disk = (struct onefilefs_inode *)bh->b_data;
    num_mounted_blocks = (inode_disk->file_size)/DEFAULT_BLOCK_SIZE;
    //check sul numero di blocchi effettivamente allocati, che non deve essere superiore a quello stabilito a tempo di compilazione (DATA_BLOCKS)
    if (num_mounted_blocks > DATA_BLOCKS) {
        brelse(bh); //rilascio del blocco bh (i.e. del blocco relatio all'inode dell'unico file del file system)
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso struct super_block *sb)
    }

    //check on the expected magic number
    if(magic != sb->s_magic){
	    return -EBADF;  //-EBADF = file descriptor non valido
    }

    sb->s_fs_info = NULL; //FS specific data (the magic number) already reported into the generic superblock
    sb->s_op = &singlefilefs_super_ops;//set our own operations

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
    root_inode->i_ino = SINGLEFILEFS_ROOT_INODE_NUMBER;//this is actually 10
    inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR);//set the root user as owned of the FS root
    root_inode->i_sb = sb;
    root_inode->i_op = &onefilefs_inode_ops;//set our inode operations
    root_inode->i_fop = &onefilefs_dir_operations;//set our file operations
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

    return 0;
}

//called on file system unmounting
//funzione che ha il compito di eliminare il superblocco del filesystem
static void singlefilefs_kill_superblock(struct super_block *s) {
    kill_block_super(s);    //è lei che esegue effettivamente l'eliminazione del superblocco, eliminando le risorse ad esso associate.
    printk(KERN_INFO "%s: singlefilefs unmount succesful.\n",MOD_NAME);
    return;
}

//called on file system mounting
//funzione che ha il compito di allocare e inizializzare una nuova struttura dentry, che rappresenta la directory root del file system.
struct dentry *singlefilefs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    struct dentry *ret;

    /*@param fs_type: tipo di file system
     *@param flags: opzioni di montaggio
     *@param dev_name: nome del dispositivo su cui montare il file system
     *@param data: puntatore ai dati di montaggio
     *@param singlefilefs_fill_super: puntatore a una funzione di callback che viene usata per inizializzare il superblocco del filesystem
     *è questa funzione che monta il file system sul dispositivo specificato e crea la struttura dentry per il filesystem.
     */
    ret = mount_bdev(fs_type, flags, dev_name, data, singlefilefs_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting onefilefs",MOD_NAME);
    else
        printk("%s: singlefilefs is succesfully mounted on from device %s\n",MOD_NAME,dev_name);

    return ret;
}

//file system type: describes file system structure
static struct file_system_type onefilefs_type = {
	.owner      = THIS_MODULE,                  //modulo proprietario del filesystem (i.e. modulo che ha creato l'istanza di file system)
    .name       = "singlefilefs",               //nome del file system; viene usato per il montaggio del file system stesso.
    .mount      = singlefilefs_mount,           //funzione da chiamare quando si vuole montare il file system
    .kill_sb    = singlefilefs_kill_superblock, //funzione da chiamare quando si vuole eliminare un superblocco associato al filesystem
};
