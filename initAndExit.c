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

static int singlefilefs_init(void) {

    int ret;

    //register filesystem
    ret = register_filesystem(&onefilefs_type);
    if (likely(ret == 0))
        printk("%s: sucessfully registered singlefilefs\n",MOD_NAME);
    else
        printk("%s: failed to register singlefilefs - error %d", MOD_NAME,ret);

    return ret;
}

static void singlefilefs_exit(void) {

    int ret;

    //unregister filesystem
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
