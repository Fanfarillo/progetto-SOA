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

int add_sorted_node(int, unsigned int, struct sorted_node **);

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

    //se non si vuole utilizzare il page-cache write back daemon, la scrittura del superblocco viene riportata nel device in maniera sincrona
    #ifdef SYNC
    sync_dirty_buffer(bh);
    #endif

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

    //se non si vuole utilizzare il page-cache write back daemon, la scrittura del blocco viene riportata nel device in maniera sincrona
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

    //se non si vuole utilizzare il page-cache write back daemon, la scrittura del blocco viene riportata nel device in maniera sincrona
    #ifdef SYNC
    sync_dirty_buffer(bh);
    #endif

    //rilascio del buffer head bh
    brelse(bh);
    return 0;    

}

//questa funzione aggiunge un nodo alla lista collegata che serve per effettuare le letture dei blocchi in ordine di 'write_counter'.
int add_sorted_node(int index, unsigned int write_counter, struct sorted_node **first_sorted_node_ptr) {

    struct sorted_node *prev_sorted_node;
    struct sorted_node *curr_sorted_node;
    struct sorted_node *new_sorted_node;

    new_sorted_node = kmalloc(sizeof(struct sorted_node), GFP_KERNEL);
    if (!new_sorted_node) {
        return -1;  //error condition
    }

    new_sorted_node->node_index = index;
    new_sorted_node->write_counter = write_counter;

    //ciclo in cui navigo la lista collegata per stabilire la posizione corretta in cui inserire il nuovo nodo; se first_sorted_node == NULL, però, si tratta del primo nodo.
    if (*first_sorted_node_ptr == NULL) {
        *first_sorted_node_ptr = new_sorted_node;
        return 0;
    }

    prev_sorted_node = NULL;
    curr_sorted_node = *first_sorted_node_ptr;

    while(curr_sorted_node != NULL) {
        if (new_sorted_node->write_counter < curr_sorted_node) {

            if (prev_sorted_node == NULL) { //caso in cui new_sorted_node sarà il primo nodo della lista collegata di struct sorted_node
                new_sorted_node->next = *first_sorted_node_ptr;
                *first_sorted_node_ptr = new_sorted_node;
            }
            else {
                prev_sorted_node->next = new_sorted_node;
                new_sorted_node->next = curr_sorted_node;
            }
            break;

        }
        else if (curr_sorted_node->next == NULL) {  //caso in cui new_sorted_node sarà l'ultimo nodo della lista collegata di struct sorted_node
            curr_sorted_node->next = new_sorted_node;
            new_sorted_node->next = NULL;
            break;
        }

        prev_sorted_node = curr_sorted_node;
        curr_sorted_node = curr_sorted_node->next;

    }

    return 0;

}
