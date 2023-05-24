#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>

#include "filesystem/singlefilefs.h"
#include "filesystem/singlefilefs_ker.h"
#include "devFunctions.h"

//UTILS FUNCTIONS PROTOTYPES
struct onefilefs_sb_info *get_superblock_info(void);

//questa funzione restituisce la struttura dati che comprende le informazioni contenute nel superblocco del dispositivo.
struct onefilefs_sb_info *get_superblock_info() {

    int fd;                         //file descriptor da utilizzare per il nostro dispositivo ("image")
    struct file *f;                 //struttura che descrive il nostro dispositivo
    loff_t start_offset;            //valore (offset) che indica il punto del dispositivo da cui deve iniziare la lettura
    loff_t *pos;                    //puntatore che indica il punto del dispositivo da cui deve iniziare la lettura
    ssize_t ret;                    //valore di ritorno della funzione vfs_read()
    char *buffer;                   //buffer che verrà popolato dalla funzione vfs_read()
    struct onefilefs_sb_info *sb;   //struttura dati che comprende le informazioni contenute nel superblocco

    fd = get_unused_fd_flags(0);             //ottenimento di un file descriptor disponibile
    if (fd < 0)
        return NULL;

    f = filp_open(IMAGE_NAME, O_RDONLY, 0);   //apertura del dispositivo in modalità sola lettura
    if (IS_ERR(f)) 
        return NULL;

    buffer = kmalloc(DEFAULT_BLOCK_SIZE, GFP_KERNEL);   //allocazione della memoria di livello kernel per buffer
    if (!buffer) {
        filp_close(f, NULL);
        return NULL;
    }
    sb = kmalloc(DEFAULT_BLOCK_SIZE, GFP_KERNEL); //allocazione della memoria di livello kernel per sb
    if (!sb) {
        filp_close(f, NULL);
        kfree(buffer);
        return NULL;
    }

    start_offset = 0;    //dovendo leggere il superblocco, mi pongo all'inizio del file che descrive il dispositivo (i.e. offset = 0).
    pos = &start_offset;

    #if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
    ret = kernel_read(f, buffer, DEFAULT_BLOCK_SIZE, pos);   //lettura del superblocco del nostro dispositivo
    #else
    ret = vfs_read(f, buffer, DEFAULT_BLOCK_SIZE, pos);
    #endif

    if (ret < 0) {
        filp_close(f, NULL);
        kfree(buffer);
        kfree(sb);
        return NULL;
    }
    //copia dei dati letti dal superblocco nell'apposita struttura
    memcpy(sb, buffer, DEFAULT_BLOCK_SIZE);

    //clean up
    filp_close(f, NULL);
    kfree(buffer);

    return sb;

}
