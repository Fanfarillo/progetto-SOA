#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "filesystem/singlefilefs.h"
#include "filesystem/singlefilefs_ker.h"
#include "devFunctions.h"

//UTILS FUNCTIONS PROTOTYPES
struct onefilefs_sb_info *get_superblock_info(struct super_block *);
struct data_block_content *get_block_content(struct super_block *, int);
struct data_block_metadata *get_block_metadata(struct super_block *, int);
int set_superblock_info(struct super_block *, int, int);
int set_block_content(struct super_block *, int, int, char *, size_t);
int set_block_metadata(struct super_block *, int, int, int);
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

//questa funzione restituisce il puntatore alla struttura dati che comprende i metadati di un blocco dati.
struct data_block_metadata *get_block_metadata(struct super_block *global_sb, int block_num) {

    struct data_block_content *db_cont;

    db_cont = get_block_content(global_sb, block_num);
    if (db_cont == NULL) {
        return NULL;
    }
    return &(db_cont->metadata);

}

//questa funzione scrive sul superblocco del dispositivo
int set_superblock_info(struct super_block *global_sb, int new_first_valid, int new_last_valid) {

    struct buffer_head *bh;
    struct onefilefs_sb_info *new_sb_disk;

    bh = sb_bread(global_sb, SB_BLOCK_NUMBER);
    if (!(global_sb && bh)) {
        return -1;  //error condition
    }
    new_sb_disk = (struct onefilefs_sb_info *)bh->b_data;

    new_sb_disk->first_valid = new_first_valid;
    new_sb_disk->last_valid = new_last_valid;

    //segnalazione al SO che il superblocco è stato modificato e che le modifiche devono essere sincronizzate col device sottostante
    mark_buffer_dirty(bh);

    //se non si vuole utilizzare il page-cache write back daemon, la scrittura del superblocco viene riportata nel device in maniera sincrona
    #ifdef SYNC
    sync_dirty_buffer(bh);
    #endif

    //rilascio del buffer head bh
    brelse(bh);
    return 0;

}

//questa funzione scrive su uno specifico blocco all'interno del dispositivo (metadati+payload)
int set_block_content(struct super_block *global_sb, int block_num, int new_prev_valid, char *source, size_t size) {

    struct buffer_head *bh;
    struct data_block_content *new_db_cont;
    int i;  //indice per il ciclo for

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return -1;  //error condition
    }
    new_db_cont = (struct data_block_content *)bh->b_data;

    //modifico opportunamente i metadati del blocco target (eccetto is_last che è un valore fisso).
    new_db_cont->metadata.next_valid = -1;              //è l'ultimo blocco reso valido in ordine temporale per cui non può avere un next.
    new_db_cont->metadata.prev_valid = new_prev_valid;  //settaggio del blocco valido precedente nell'ordine temporale
    new_db_cont->metadata.is_valid = 1;                 //il blocco interessato nella put_data() deve chiaramente risultare valido.

    //ora ricopio un byte per volta la stringa di input all'interno della porzione del blocco riservata al payload.
    for(i=0; i<DEFAULT_BLOCK_SIZE-METADATA_SIZE; i++) {
        if (i<size)
            new_db_cont->payload[i] = source[i];
        else
            new_db_cont->payload[i] = '\0';
            
    }

    //segnalazione al SO che il blocco è stato modificato e che le modifiche devono essere sincronizzate col device sottostante
    mark_buffer_dirty(bh);

    //se non si vuole utilizzare il page-cache write back daemon, la scrittura del blocco viene riportata nel device in maniera sincrona.
    #ifdef SYNC
    sync_dirty_buffer(bh);
    #endif

    //rilascio del buffer head bh
    brelse(bh);
    return 0;

}

//questa funzione scrive solo i metadati su uno specifico blocco all'interno del dispositivo
int set_block_metadata(struct super_block *global_sb, int block_num, int pointed_block, int set_next) {

    struct buffer_head *bh;
    struct data_block_content *new_db_cont;

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return -1;  //error condition
    }
    new_db_cont = (struct data_block_content *)bh->b_data;

    //modifico opportunamente i metadati del blocco target
    if (set_next == YES)
        new_db_cont->metadata.next_valid = pointed_block;
    else
        new_db_cont->metadata.prev_valid = pointed_block;

    //segnalazione al SO che il blocco è stato modificato e che le modifiche devono essere sincronizzate col device sottostante
    mark_buffer_dirty(bh);

    //se non si vuole utilizzare il page-cache write back daemon, la scrittura del blocco viene riportata nel device in maniera sincrona.
    #ifdef SYNC
    sync_dirty_buffer(bh);
    #endif

    //rilascio del buffer head bh
    brelse(bh);
    return 0;

}

//questa funzione marca uno specifico blocco all'interno del dispositivo come invalido
int invalidate_block_content(struct super_block *global_sb, int block_num) {

    struct buffer_head *bh;
    struct data_block_content *new_db_cont;

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return -1;  //error condition
    }
    new_db_cont = (struct data_block_content *)bh->b_data;

    //è sufficiente portare a 0 il valore di is_valid affinché il blocco target risulti invalido.
    new_db_cont->metadata.is_valid = 0;

    //segnalazione al SO che il blocco è stato modificato e che le modifiche devono essere sincronizzate col device sottostante
    mark_buffer_dirty(bh);

    //se non si vuole utilizzare il page-cache write back daemon, la scrittura del blocco viene riportata nel device in maniera sincrona
    #ifdef SYNC
    sync_dirty_buffer(bh);
    #endif

    //rilascio del buffer head bh
    brelse(bh);
    return 0;    

}
