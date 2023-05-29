#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/rculist.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "filesystem/singlefilefs.h"
#include "filesystem/singlefilefs_ker.h"
#include "devFunctions.h"

//UTILS FUNCTIONS PROTOTYPES
struct onefilefs_sb_info *get_superblock_info(struct super_block *);
struct data_block_content *get_block_content(struct super_block *, int);

//questa funzione restituisce il puntatore alla struttura dati che comprende le informazioni contenute nel superblocco del dispositivo.
struct onefilefs_sb_info *get_superblock_info(struct super_block *global_sb) {

    struct buffer_head *bh;
    struct onefilefs_sb_info *sb_disk;

    bh = sb_bread(global_sb, SB_BLOCK_NUMBER);
    if (!(global_sb && bh)) {
        return NULL;
    }

    sb_disk = (struct onefilefs_sb_info *)bh->b_data;
    //rilascio del buffer head bh
    brelse(bh);

    return sb_disk;

}

//questa funzione restituisce il puntatore alla struttura dati che comprende il contenuto di un blocco dati.
struct data_block_content *get_block_content(struct super_block *global_sb, int block_num) {

    struct buffer_head *bh;
    struct data_block_content *db_cont;

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return NULL;
    }

    db_cont = (struct data_block_content *)bh->b_data;
    //rilascio del buffer head bh
    brelse(bh);

    return db_cont;

}
