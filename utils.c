#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "filesystem/singlefilefs.h"
#include "devFunctions.h"

//UTILS FUNCTIONS PROTOTYPES
struct onefilefs_sb_info *get_superblock_info();

//questa funzione restituisce la struttura dati che comprende le informazioni contenute nel superblocco del dispositivo.
struct onefilefs_sb_info *get_superblock_info() {

    int fd;                         //file descriptor da utilizzare per il nostro dispositivo ("image")
    struct file *f;                 //struttura che descrive il nostro dispositivo
    ssize_t ret;                    //valore di ritorno della funzione vfs_read()
    char *buffer;                   //buffer che verrà popolato dalla funzione vfs_read()
    struct onefilefs_sb_info *sb;   //struttura dati che comprende le informazioni contenute nel superblocco

    fd = get_unused_fd_flags(0)             //ottenimento di un file descriptor disponibile
    if (fd < 0)
        return NULL;

    f = filp_open(IMAGE_NAME, O_RDONLY, 0);   //apertura del dispositivo in modalità sola lettura
    if (IS_ERR(f)) 
        return NULL;

    buffer = kmalloc(DEFAULT_BLOCK_SIZE, GFP_KERNEL);   //allocazione della memoria di livello kernel per buffer
    if (!buffer) {
        filp_close(f, FL_CLOSE);
        return NULL;
    }
    sb = kmalloc(DEFAULT_BLOCK_SIZE, GFP_KERNEL); //allocazione della memoria di livello kernel per sb
    if (!sb) {
        filp_close(f, FL_CLOSE);
        kfree(buffer);
        return NULL;
    }
    ret = vfs_read(f, buffer, DEFAULT_BLOCK_SIZE, 0);   //lettura del superblocco del nostro dispositivo
    if (ret < 0) {
        filp_close(f, FL_CLOSE);
        kfree(buffer);
        kfree(sb);
        return NULL;
    }
    //copia dei dati letti dal superblocco nell'apposita struttura
    memcpy(sb, buffer, DEFAULT_BLOCK_SIZE);

    //clean up
    filp_close(f, FL_CLOSE);
    kfree(buffer);

    return sb;

}
