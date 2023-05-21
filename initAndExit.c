#include <linux/buffer_head.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/version.h>

#include "lib/include/scth.h"
#include "filesystem/singlefilefs_src.c"
#include "devFunctions.c"

//variabile in cui verrà memorizzato l'indirizzo in cui è posta la syscall table (tale indirizzo è passato come parametro al presente modulo)
unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);

unsigned long the_ni_syscall;
unsigned long new_syscall_array[] = {0x0, 0x0, 0x0};
#define HACKED_ENTRIES (int)(sizeof(new_syscall_array)/sizeof(unsigned long))
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)]-1};

//funzione che registra il file system "singlefilefs" nel kernel Linux.
static int singlefilefs_init(void) {

    int ret;

    printk("%s: usleep example received sys_call_table address %px\n",MOD_NAME,(void*)the_syscall_table);
    printk("%s: initializing - hacked entries %d\n",MOD_NAME,HACKED_ENTRIES);

    //definizione delle system call da sostuire alle prime tre ni_syscall
    new_syscall_array[0] = (unsigned long)sys_put_data;
    new_syscall_array[1] = (unsigned long)sys_get_data;
    new_syscall_array[2] = (unsigned long)sys_invalidate_data;

    ret = get_entries(restore, HACKED_ENTRIES, (unsigned long *)the_syscall_table, &the_ni_syscall);
    if (ret != HACKED_ENTRIES){
        printk("%s: could not hack %d entries (just %d)\n", MOD_NAME, HACKED_ENTRIES, ret);
        return -1;
    }

    unprotect_memory();
    for(int i=0; i<HACKED_ENTRIES; i++) {
        ((unsigned long *)the_syscall_table)[restore[i]] = (unsigned long)new_syscall_array[i];
    }
    protect_memory();
    printk("%s: all new system calls correctly installed on syscall table\n", MOD_NAME);

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

    printk("%s: shutting down\n", MOD_NAME);
    unprotect_memory();
    for(int i=0; i<HACKED_ENTRIES; i++) {
        ((unsigned long *)the_syscall_table)[restore[i]] = the_ni_syscall;
    }
    protect_memory();
    printk("%s: syscall table restored to its original content\n", MOD_NAME);

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
MODULE_AUTHOR("Francesco Quaglia <francesco.quaglia@uniroma2.it>, Matteo Fanfarillo <matteo.fanfarillo99@gmail.com>");
MODULE_DESCRIPTION("SINGLE-FILE-FS");
