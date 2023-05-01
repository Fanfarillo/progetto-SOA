#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "filesystem/singlefilefs_src.c"

//funzione che registra il file system "singlefilefs" nel kernel Linux.
static int singlefilefs_init(void) {

    int ret;

    //register filesystem (type: onefilefs_type)
    ret = register_filesystem(&onefilefs_type);
    if (likely(ret == 0))   //likely(): macro usata per fornire un suggerimento al compilatore sul percorso d'esecuzione più probabile.
        printk("%s: sucessfully registered singlefilefs\n",MOD_NAME);
    else
        printk("%s: failed to register singlefilefs - error %d", MOD_NAME,ret);

    return ret; //return: valore intero che rappresenta il risultato della registrazione del file system
}

//funzione che deregistra il file system "singlefilefs" precedentemente registrato con singlefilefs_init().
static void singlefilefs_exit(void) {

    int ret;

    //unregister file system
    ret = unregister_filesystem(&onefilefs_type);

    if (likely(ret == 0))
        printk("%s: sucessfully unregistered file system driver\n",MOD_NAME);
    else
        printk("%s: failed to unregister singlefilefs driver - error %d", MOD_NAME, ret);
}

module_init(singlefilefs_init);
module_exit(singlefilefs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matteo Fanfarillo <matteo.fanfarillo@gmail.com>");
MODULE_DESCRIPTION("SINGLE-FILE-FS");
