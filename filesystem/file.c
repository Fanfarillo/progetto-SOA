#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/types.h>
#include <linux/version.h>

#include "singlefilefs.h"

//è una callback della struttura onefilefs_file_operations (di tipo struct file_operations).
//in particolare, legge i dati dal blocco del file system corrispondente alla posizione indicata dall'offset off.
ssize_t onefilefs_read(struct file * filp, char __user * buf, size_t len, loff_t * off) {

    struct buffer_head *bh = NULL;
    struct inode * the_inode = filp->f_inode;
    uint64_t file_size = the_inode->i_size;
    int ret;
    loff_t offset;
    int block_to_read;//index of the block to be read from device

    printk("%s: read operation called with len %ld - and offset %lld (the current file size is %lld)",MOD_NAME, len, *off, file_size);

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
    
    printk("%s: read operation must access block %d of the device",MOD_NAME, block_to_read);

    //sb_read acquisisce il contenuto del blocco da leggere (quello di cui abbiamo appena calcolato l'indice).
    bh = (struct buffer_head *)sb_bread(filp->f_path.dentry->d_inode->i_sb, block_to_read);
    if(!bh){
	    return -EIO;
    }
    //ora si copiano i dati dal buffer del kernel (bh->b_data+offset) al buffer dell'applicazione (buf), passato come parametro a onefilefs_read().
    ret = copy_to_user(buf,bh->b_data + offset, len);
    *off += (len - ret);
    brelse(bh);

    return len - ret;   //return: numero di byte effettivamente letti e copiati

}

//è una callback della struttura onefilefs_inode_ops (di tipo struct inode_operations).
//viene invocata quando il kernel cerca di accedere a un file / directory nel file system montato.
struct dentry *onefilefs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {

    struct onefilefs_inode *FS_specific_inode;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh = NULL;
    struct inode *the_inode = NULL;

    printk("%s: running the lookup inode-function for name %s",MOD_NAME,child_dentry->d_name.name);

    //controllo su se il nome del file cercato corrisponde al nome del file unico nel file system.
    if(!strcmp(child_dentry->d_name.name, UNIQUE_FILE_NAME)){

        /* praticamente è definita una cache degli inode (ricordi il buffer cache?). La funzione cerca all'interno della cache
         * l'inode del file unico: se c'è, lo restituisce direttamente, altrimenti crea il nuovo inode all'interno della cache
         * e poi lo restituisce.
         */

	    //get a locked inode from the cache 
        the_inode = iget_locked(sb, 1);
        if (!the_inode)
       		 return ERR_PTR(-ENOMEM);

	    //already cached inode - simply return successfully
	    if(!(the_inode->i_state & I_NEW)){
		    return child_dentry;
	    }

	    //this work is done if the inode was not already cached
        //in pratica qui viene definito l'inode impostando i permessi d'accesso, le file operation e le inode operation.
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(5,12,0)
            inode_init_owner(&init_user_ns, the_inode, NULL, S_IFREG);
        #else
            inode_init_owner(the_inode, NULL, S_IFREG);
        #endif
	    the_inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;
        the_inode->i_fop = &onefilefs_file_operations;
	    the_inode->i_op = &onefilefs_inode_ops;

	    //just one link for this file
	    set_nlink(the_inode,1);

	    //now we retrieve the file size via the FS specific inode, putting it into the generic inode
        //viene usata sb_read() per leggere l'inode corrispondente al file cercato. La funzione ritorna un pointer al buffer cache associato a tale inode.
        bh = (struct buffer_head *)sb_bread(sb, SINGLEFILEFS_INODES_BLOCK_NUMBER);
        if(!bh){
		    iput(the_inode);
		    return ERR_PTR(-EIO);
        }
	    FS_specific_inode = (struct onefilefs_inode*)bh->b_data;//puntatore all'area di memoria (all'interno del device) da cui sono estratti i dati acceduti
	    the_inode->i_size = FS_specific_inode->file_size;       //dimensione del buffer che ospita i dati acceduti (l'inode) 
        brelse(bh); //free della memoria usata dal buffer

        d_add(child_dentry, the_inode);
	    dget(child_dentry);

	    //unlock the inode to make it usable 
        unlock_new_inode(the_inode);

	    return child_dentry;
    }

    return NULL;    //caso in cui il nome del file cercato NON corrisponde al nome del file unico nel file system.

}

//look up goes in the inode operations
const struct inode_operations onefilefs_inode_ops = {
    .lookup = onefilefs_lookup,
};

const struct file_operations onefilefs_file_operations = {
    .owner = THIS_MODULE,
    .read = onefilefs_read,
    //.write = onefilefs_write //please implement this function to complete the exercise
};
