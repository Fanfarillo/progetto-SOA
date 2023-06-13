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
int set_superblock_info(struct super_block *, unsigned int);
int set_block_content(struct super_block *, int, unsigned int, char *, size_t);
int invalidate_block_content(struct super_block *, int);

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

//questa funzione scrive sul superblocco del dispositivo
int set_superblock_info(struct super_block *global_sb, unsigned int new_total_writes) {

    struct buffer_head *bh;
    struct onefilefs_sb_info *new_sb_disk;

    bh = sb_bread(global_sb, SB_BLOCK_NUMBER);
    if (!(global_sb && bh)) {
        return -1;  //error condition
    }

    new_sb_disk = (struct onefilefs_sb_info *)bh->b_data;
    new_sb_disk->user_sb.total_writes = new_total_writes;

    //segnalazione al SO che il superblocco è stato modificato e che le modifiche devono essere sincronizzate col device sottostante
    mark_buffer_dirty(bh);
    //rilascio del buffer head bh
    brelse(bh);
    return 0;

}

//questa funzione scrive su uno specifico blocco all'interno del dispositivo (metadati+payload)
int set_block_content(struct super_block *global_sb, int block_num, unsigned int new_write_counter, char *source, size_t size) {

    struct buffer_head *bh;
    struct data_block_content *new_db_cont;
    unsigned int numeric_metadata;  //metadati del blocco da scrivere (sottoforma di unsigned int)
    int i;  //indice per il ciclo for

    numeric_metadata = new_write_counter << 1;  //lascio spazio per il bit di validità.
    numeric_metadata++;                         //bit di validità = 1

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return -1;  //error condition
    }

    new_db_cont = (struct data_block_content *)bh->b_data;
    new_db_cont->metadata = numeric_metadata;
    //ora ricopio un byte per volta la stringa di input all'interno della porzione del blocco riservata al payload.
    for(i=0; i<size; i++) {
        new_db_cont->payload[i] = source[i];
    }

    //segnalazione al SO che il superblocco è stato modificato e che le modifiche devono essere sincronizzate col device sottostante
    mark_buffer_dirty(bh);
    //rilascio del buffer head bh
    brelse(bh);
    return 0;

}

//questa funzione marca uno specifico blocco all'interno del dispositivo come invalido
int invalidate_block_content(struct super_block *global_sb, int block_num) {

    struct buffer_head *bh;
    struct data_block_content *new_db_cont;
    unsigned int numeric_metadata;  //metadati del blocco da scrivere (sottoforma di unsigned int)

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return -1;  //error condition
    }

    new_db_cont = (struct data_block_content *)bh->b_data;
    numeric_metadata = new_db_cont->metadata - 1;   //in questo modo sto settando l'ultimo bit dei metadati del blocco (i.e. il bit di validità) a 0
    new_db_cont->metadata = numeric_metadata;

    //segnalazione al SO che il superblocco è stato modificato e che le modifiche devono essere sincronizzate col device sottostante
    mark_buffer_dirty(bh);
    //rilascio del buffer head bh
    brelse(bh);
    return 0;    

}
