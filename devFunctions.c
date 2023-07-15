/* DISCLAIMER: per l'acquisizione dei mutex write_mutex e off_mutex si è scelto di utilizzare la funzione mutex_trylock()
 * anziché la funzione mutex_lock() (e quindi di gestire le retry dell'acquisizione del lock a livello user) poiché ho
 * avuto problemi con l'esecuzione concorrente nel caso in cui si utilizza mutex_lock(): in particolare, se due thread
 * provano ad acquisire uno stesso lock concorrentemente, solo un thread riesce ad acquisirlo mentre l'altro resta in
 * attesa (il che è corretto). Tuttavia, quando il primo thread rilascia il lock, l'altro thread resta in attesa, causando
 * un deadlock.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0)
#include <linux/atomic.h>
#else
#include <asm/atomic_32.h>
#endif

#include "filesystem/singlefilefs.h"
#include "filesystem/singlefilefs_init.h"
#include "filesystem/singlefilefs_ker.h"
#include "devFunctions.h"
#include "utils.c"

int is_first_call;  //variabile globale che indica se il chiamante di dev_read() si trova alla prima iterazione o meno

//SYSTEM CALLS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size)
#else
asmlinkage int sys_put_data(char *source, size_t size)
#endif
{
    char *kernel_lvl_src;       //buffer di livello kernel (inizializzato con una copy_from_user()) in cui verrà posto l'input della put
    size_t bytes_to_write;      //non è detto che size e la lunghezza di source corrispondano.
    int offset; //variabile che tiene traccia del numero di iterazione all'interno del ciclo che itera sui blocchi; sarà il valore di ritorno della system call.
    int ret;
    unsigned long ulong_ret;    //serve specificatamente per la copy_from_user().
    int new_first_valid;        //nuovo valore che dovrà assumere first_valid nel superblocco; sarà diverso dall'originale solo se quest'ultimo è pari a -1.
    struct onefilefs_sb_info *sb_disk;
    struct data_block_metadata *db_meta;

    //incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(au_info.usages));

    //sanity checks
    if (!au_info.is_mounted) {
        printk("%s: impossibile eseguire la system call put_data(): il file system non è stato montato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if (size > DEFAULT_BLOCK_SIZE-METADATA_SIZE) {
        printk("%s: impossibile eseguire la system call put_data(): la dimensione dei dati da scrivere eccede la dimensione di un blocco\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso size_t size)
    }
    if (source == NULL) {
        printk("%s: impossibile eseguire la system call put_data(): non vi sono dati da scrivere\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso size_t size e/o char *source)
    }

    kernel_lvl_src = kmalloc(size, GFP_KERNEL);
    if (!kernel_lvl_src) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore con l'allocazione della memoria\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENOMEM; //-ENOMEM = errore di esaurimento della memoria         
    }
    ulong_ret = copy_from_user(kernel_lvl_src, source, (unsigned long)size);  //ulong_ret è il numero di byte NON copiati (su un massimo di size).
    bytes_to_write = size - (size_t)ulong_ret;    //il numero di byte da scrivere nel blocco è pari a size meno i residui di copy_from_user().

    //utilizzo di mutex per sincronizzare le scritture tra loro
    ret = mutex_trylock(&(au_info.write_mutex));
    if (ret == 0) {
        kfree(kernel_lvl_src);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EBUSY;
    }
    printk("%s: [put_data] mutex_lock correttamente acquisito\n", MOD_NAME);

    //recupero dei dati memorizzati nel superblocco
    sb_disk = get_superblock_info(global_sb);
    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        kfree(kernel_lvl_src);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }

    //iterazione sui blocchi del dispositivo per cercare un blocco libero.
    for(offset=0; offset<sb_disk->total_data_blocks; offset++) {

        db_meta = get_block_metadata(global_sb, offset+2);  //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
        if (db_meta == NULL) {
            printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore col recupero dei metadati del blocco %d\n", MOD_NAME, offset);
            kfree(kernel_lvl_src);
            mutex_unlock(&(au_info.write_mutex));
            printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
            atomic_fetch_add(-1, &(au_info.usages));
            return -EIO; //-EIO = errore di input/output
        }

        if (!(db_meta->is_valid))   //sto cercando un blocco libero, ovvero un blocco non valido.
            break;

        else if (db_meta->is_valid && db_meta->is_last) {   //arrivo qui se nessun nodo è libero.
            printk("%s: impossibile eseguire la system call put_data(): non ci sono blocchi liberi\n", MOD_NAME);
            kfree(kernel_lvl_src);
            mutex_unlock(&(au_info.write_mutex));
            printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
            atomic_fetch_add(-1, &(au_info.usages));
            return -ENOMEM; //-ENOMEM = errore di esaurimento della memoria
        }

    }

    //TODO: qui va l'attesa della fine del grace period.

    //aggiornamento del campo next_valid del vecchio ultimo blocco valido
    ret = set_block_metadata(global_sb, (sb_disk->last_valid)+2, offset, YES);
    if (ret < 0) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore con la scrittura dei metadati sul blocco %d\n", MOD_NAME, sb_disk->last_valid);
        kfree(kernel_lvl_src);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output        
    }

    //scrittura del blocco target (metadati+payload)
    ret = set_block_content(global_sb, offset+2, sb_disk->last_valid, kernel_lvl_src, bytes_to_write);
    if (ret < 0) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore con la scrittura dei dati sul blocco %d\n", MOD_NAME, offset);
        kfree(kernel_lvl_src);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output        
    }

    if (sb_disk->first_valid == -1) //se prima della put_data() non vi erano blocchi validi, allora first_valid deve essere settato nel superblocco.
        new_first_valid = offset;
    else                            //altrimenti first_valid resta invariato.
        new_first_valid = sb_disk->first_valid;

    //scrittura del superblocco del dispositivo (in particolare dei campi first_valid, last_valid)
    ret = set_superblock_info(global_sb, new_first_valid, offset);
    if (ret < 0) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore con la scrittura dei dati sul superblocco\n", MOD_NAME);
        kfree(kernel_lvl_src);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output        
    }

    //cleanup
    printk("%s: la system call put_data() sul blocco %d è stata eseguita con successo\n", MOD_NAME, offset);
    kfree(kernel_lvl_src);
    mutex_unlock(&(au_info.write_mutex));
    printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
    atomic_fetch_add(-1, &(au_info.usages));
    return offset;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size)
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size)
#endif
{   
    int readable_bytes;             //numero di byte effettivamente presentu nel blocco target prima del terminatore di stringa ('\0')
    int lost_bytes_copy_to_user;    //numero di byte (tra quelli letti con kernel_read()) che non è stato possibile consegnare all'utente con copy_to_user()
    struct onefilefs_sb_info *sb_disk;
    struct data_block_content *db_cont;

    //incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(au_info.usages));

    //sanity checks (notare che il caso size>DEFAULT_BLOCK_SIZE-METADATA_SIZE viene accettato e omologato al caso size==DEFAULT_BLOCK_SIZE-METADATA_SIZE)
    if (!au_info.is_mounted) {
        printk("%s: impossibile eseguire la system call get_data(): il file system non è stato montato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if (destination == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): non è stato specificato alcun buffer di destinazione\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso char *destination)
    }

    if (size > DEFAULT_BLOCK_SIZE-METADATA_SIZE) {
        size = DEFAULT_BLOCK_SIZE-METADATA_SIZE;    //in tal modo si leggono esclusivamente i dati posti nel blocco
    }

    //TODO: qui va l'acquisizione dell'RCU read lock.

    //recupero dei dati memorizzati nel superblocco
    sb_disk = get_superblock_info(global_sb);
    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb_disk->total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1.
        printk("%s: impossibile eseguire la system call get_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    //recupero del contenuto del blocco da leggere
    db_cont = get_block_content(global_sb, offset+2);   //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
    if (db_cont == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore col recupero dei dati del blocco %d\n", MOD_NAME, offset);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }

    //TODO: qui va il rilascio dell'RCU read lock.

    //check sulla validità del blocco target
    if(!(db_cont->metadata.is_valid)) {
        printk("%s: impossibile eseguire la system call get_data(): il blocco specificato (%d) non è valido\n", MOD_NAME, offset);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODATA; //-ENODATA = nessun dato disponibile
    }

    //consegna dei dati all'utente
    lost_bytes_copy_to_user = copy_to_user(destination, &(db_cont->payload[0]), size);

    printk("%s: la system call get_data() sul blocco %d è stata eseguita con successo\n", MOD_NAME, offset);
    atomic_fetch_add(-1, &(au_info.usages));
    return size - lost_bytes_copy_to_user;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset)
#else
asmlinkage int sys_invalidate_data(int offset)
#endif
{
    int ret;
    int superblock_to_set;  //booleano che indica se bisognerà aggiornare first_valid e/o last_valid nel superblocco
    int next_to_set;        //booleano che indica se bisognerà aggiornare prev_valid nel blocco successivo a quello da invalidare
    int prev_to_set;        //booleano che indica se bisognerà aggiornare next_valid nel blocco precedente a quello da invalidare
    int new_first_valid;
    int new_last_valid;
    struct onefilefs_sb_info *sb_disk;
    struct data_block_metadata *db_meta;

    superblock_to_set = NO;
    next_to_set = NO;
    prev_to_set = NO;
    new_first_valid = -1;
    new_last_valid = -1;

    //incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(au_info.usages));

    //sanity checks
    if (!au_info.is_mounted) {
        printk("%s: impossibile eseguire la system call invalidate_data(): il file system non è stato montato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODEV; //-ENODEV = file system non esistente
    }

    //utilizzo di mutex per sincronizzare le scritture tra loro (di fatto anche l'invalidazione risulta essere una scrittura nella RCU list)
    ret = mutex_trylock(&(au_info.write_mutex));
    if (ret == 0) {
        atomic_fetch_add(-1, &(au_info.usages));
        return -EBUSY;
    }
    printk("%s: [invalidate_data] mutex_lock correttamente acquisito\n", MOD_NAME);

    //recupero dei dati memorizzati nel superblocco
    sb_disk = get_superblock_info(global_sb);
    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call invalidate_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb_disk->total_data_blocks) {   //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
        printk("%s: impossibile eseguire la system call invalidate_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    //recupero dei metadati del blocco da leggere
    db_meta = get_block_metadata(global_sb, offset+2);   //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
    if (db_meta == NULL) {
        printk("%s: impossibile eseguire la system call invalidate_data(): si è verificato un errore col recupero dei metadati del blocco %d\n", MOD_NAME, offset);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }

    //check sulla validità del blocco target
    if (!(db_meta->is_valid)) {
        printk("%s: impossibile eseguire la system call invalidate_data(): il blocco specificato (%d) è già invalido\n", MOD_NAME, offset);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [invalidate_data] mutex_lock e rcu_read_lock correttamente rilasciati\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODATA; //-ENODATA = nessun dato disponibile
    }

    //caso in cui il blocco target era l'unico blocco valido
    if (offset == sb_disk->first_valid && offset == sb_disk->last_valid) {
        superblock_to_set = YES;
        //new_first_valid e new_last_valid sono già correttamente settati a -1.
    }
    //caso in cui il blocco target era il primo blocco valido (ma non l'unico)
    else if (offset == sb_disk->first_valid && offset != sb_disk->last_valid) {
        superblock_to_set = YES;
        next_to_set = YES;
        new_first_valid = db_meta->next_valid;
        new_last_valid = sb_disk->last_valid;
    }
    //caso in cui il blocco target era l'ultimo blocco valido (ma non il primo)
    else if (offset != sb_disk->first_valid && offset == sb_disk->last_valid) {
        superblock_to_set = YES;
        prev_to_set = YES;
        new_first_valid = sb_disk->first_valid;
        new_last_valid = db_meta->prev_valid;
    }
    //caso in cui il blocco target non era né il primo né l'ultimo blocco valido
    else {
        prev_to_set = YES;
        next_to_set = YES;
    }

    if (superblock_to_set == YES) {

    }

    if (prev_to_set == YES) {

    }

    if (next_to_set == YES) {
        
    }

    //invalidazione del blocco (interessano in particolar modo solo i metadati)
    ret = invalidate_block_content(global_sb, offset+2);    //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
    if (ret < 0) {
        printk("%s: impossibile eseguire la system call invalidate_data(): si è verificato un errore con l'invalidazione dei dati sul blocco %d\n", MOD_NAME, offset);
        mutex_unlock(&(au_info.write_mutex));
        printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output        
    }

    printk("%s: la system call invalidate_data() sul blocco %d è stata eseguita con successo\n", MOD_NAME, offset);
    mutex_unlock(&(au_info.write_mutex));
    printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
    atomic_fetch_add(-1, &(au_info.usages));
    return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;
long sys_get_data = (unsigned long) __x64_sys_get_data;
long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#endif

//FILE OPERATIONS
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);

/* La dev_read() legge i dati dal blocco del dispositivo corrispondente alla posizione indicata dall'offset off.
 * RETURN 0, cosicché poi dev_read() non venga più invocata (mi occuperò di leggere tutti i blocchi in un ciclo definito qui).
 */
static ssize_t dev_read(struct file *filp, char *buf, size_t len, loff_t *off) {

    struct buffer_head *bh;
    struct inode *the_inode;
    uint64_t file_size;
    int ret;
    loff_t offset;
    int block_to_read;  //index of the block to be read from device

    //qui iniziano le variabili definite da me
    struct onefilefs_sb_info *sb_disk;
    int index;  //tiene traccia dell'indice di ciascun nodo della RCU list.
    struct rcu_node *curr_rcu_node;
    struct sorted_node *prev_sorted_node;
    char *read_data;
    size_t num_read_bytes;

    //incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(au_info.usages));

    bh = NULL;
    the_inode = filp->f_inode;
    file_size = the_inode->i_size;

    printk("%s: read operation called with len %ld\n", MOD_NAME, len);

    //sanity check
    if (!au_info.is_mounted) {
        printk("%s: impossibile leggere il dispositivo: il file system non è stato montato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODEV; //-ENODEV = file system non esistente
    }

    if (*off == 0) {    //caso in cui la lettura deve ancora iniziare (è qui che viene inizializzata la lista collegata dei nodi ordinati)
        //blocco off_mutex, il quale mi serve per bloccare anche gli accessi alla sorted list
        #ifdef DEBUG
        printk("%s: [dev_read] acquisizione del mutex_lock in corso...\n", MOD_NAME);
        #endif
        ret = mutex_trylock(&(au_info.off_mutex));
        if (ret == 0) {
            #ifdef DEBUG
            printk("%s: impossibile leggere il dispositivo: il lock è occupato\n", MOD_NAME);
            #endif
            atomic_fetch_add(-1, &(au_info.usages));
            return -EBUSY;
        }
        #ifdef DEBUG
        printk("%s: [dev_read] mutex_lock correttamente acquisito\n", MOD_NAME);
        #endif

        //recupero il superblocco perché mi serve per ottenere la RCU list.
        sb_disk = get_superblock_info(global_sb);
        if (sb_disk == NULL) {
            printk("%s: impossibile leggere il dispositivo: si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
            mutex_unlock(&(au_info.off_mutex));
            #ifdef DEBUG
            if (mutex_is_locked(&(au_info.off_mutex))) {
                printk("%s: [dev_read] c'è un problema con mutex_unlock()\n", MOD_NAME);
            } else {
                printk("%s: [dev_read] mutex_lock correttamente rilasciato\n", MOD_NAME);
            }
            #endif
            atomic_fetch_add(-1, &(au_info.usages));
            return -EIO; //-EIO = errore di input/output
        }

        #ifdef DEBUG
        printk("%s: [dev_read] acquisizione del rcu_read_lock in corso...\n", MOD_NAME);
        #endif
        rcu_read_lock();
        #ifdef DEBUG
        printk("%s: [dev_read] rcu_read_lock correttamente acquisito\n", MOD_NAME);
        #endif

        //all'interno di un ciclo costruisco una lista collegata che mantiene gli offset dei soli blocchi validi in ordine di 'write_counter'.
        index = 0;
        curr_rcu_node = NULL;
        first_sorted_node = NULL;

        /* Costrutto che itera su tutti i nodi della RCU list
        *@param curr_node: struct rcu_node che, a ogni iterazione del ciclo, tiene traccia del nodo corrente della RCU list
        *@param &(sb_disk->rcu_head): puntatore alla list head dell'elemento artificiale del superblock
        *@param lh: campo della struct rcu_node di tipo struct list_head
        */
        list_for_each_entry_rcu(curr_rcu_node, &(sb_disk->rcu_head), lh) {

            if (curr_rcu_node->is_valid && curr_rcu_node->write_counter > 0) {  //la condizione curr_rcu_node->write_counter > 0 esclude superblocco e inode.

                ret = add_sorted_node(index, curr_rcu_node->write_counter, &first_sorted_node);    //considero il primo blocco dati a offset 2 e così via
                if (ret < 0) {
                    printk("%s: impossibile leggere il dispositivo: si è verificato un errore con l'allocazione della memoria\n", MOD_NAME);
                    rcu_read_unlock();
                    mutex_unlock(&(au_info.off_mutex));
                    #ifdef DEBUG
                    if (mutex_is_locked(&(au_info.off_mutex))) {
                        printk("%s: [dev_read] c'è un problema con mutex_unlock()\n", MOD_NAME);
                    } else {
                        printk("%s: [dev_read] mutex_lock e rcu_read_lock correttamente rilasciati\n", MOD_NAME);
                    }
                    #endif
                    atomic_fetch_add(-1, &(au_info.usages));
                    return -EIO; //-EIO = errore di input/output            
                }

            }
            index++;

        }

        rcu_read_unlock();
        #ifdef DEBUG
        printk("%s: [get_data] rcu_read_lock correttamente rilasciato\n", MOD_NAME);
        #endif
        
        *off = (loff_t)file_size;   //segnalo il fatto che la sorted list è stata già messa in piedi per la lettura corrente.

    }

    if (first_sorted_node != NULL) {        //caso in cui ci sono ancora dei dati da leggere
        prev_sorted_node = NULL;
        offset = METADATA_SIZE; //l'offset da cui far partire ciascuna lettura di un singolo blocco deve partire dalla fine dei metadati.
        
        if (len == 0) {
            printk("%s: len == 0: nothing to do\n", MOD_NAME);
            delete_all_sorted_nodes(&first_sorted_node);      //kfree() di tutti gli eventuali sorted node rimasti
            mutex_unlock(&(au_info.off_mutex));
            #ifdef DEBUG
            if (mutex_is_locked(&(au_info.off_mutex))) {
                printk("%s: [dev_read] c'è un problema con mutex_unlock()\n", MOD_NAME);
            } else {
                printk("%s: [dev_read] mutex_lock correttamente rilasciato\n", MOD_NAME);
            }
            #endif
            atomic_fetch_add(-1, &(au_info.usages));
            return 0;
        }
        else if (len + offset > DEFAULT_BLOCK_SIZE)           
            len = DEFAULT_BLOCK_SIZE - offset;  //il numero di byte da leggere a ogni iterazione è al più pari al numero di byte di payload di un singolo blocco.
        
        block_to_read = first_sorted_node->node_index;
        printk("%s: read operation must access block %d of the device", MOD_NAME, block_to_read-2);

        //sb_read acquisisce il contenuto del blocco da leggere (quello di cui abbiamo appena calcolato l'indice).
        bh = (struct buffer_head *)sb_bread(filp->f_path.dentry->d_inode->i_sb, block_to_read);
        if(!bh){
            printk("%s: impossibile leggere il dispositivo: si è verificato un errore con la lettura del blocco %d\n", MOD_NAME, block_to_read);
            mutex_unlock(&(au_info.off_mutex));
            #ifdef DEBUG
            if (mutex_is_locked(&(au_info.off_mutex))) {
                printk("%s: [dev_read] c'è un problema con mutex_unlock()\n", MOD_NAME);
            } else {
                printk("%s: [dev_read] mutex_lock correttamente rilasciato\n", MOD_NAME);
            }
            #endif
            atomic_fetch_add(-1, &(au_info.usages));
	        return -EIO;
        }

        read_data = bh->b_data + offset;
        num_read_bytes = strnlen(read_data, len);
        //ora si copiano i dati dal buffer del kernel (bh->b_data+offset) al buffer dell'applicazione (buf), passato come parametro a onefilefs_read().
        ret = copy_to_user(buf, read_data, num_read_bytes);
        brelse(bh);

        prev_sorted_node = first_sorted_node;
        first_sorted_node = first_sorted_node->next;  //prossimo blocco da leggere

        //kfree() del nodo appena attraversato poiché non serve più
        kfree(prev_sorted_node);

        printk("%s: block %d successfully read\n", MOD_NAME, block_to_read-2);
        atomic_fetch_add(-1, &(au_info.usages));       
        return num_read_bytes-ret;

    }
    else {  //caso in cui la lettura è stata completata
        printk("%s: read operation completed\n", MOD_NAME); 
        mutex_unlock(&(au_info.off_mutex));
        #ifdef DEBUG
        if (mutex_is_locked(&(au_info.off_mutex))) {
            printk("%s: [dev_read] c'è un problema con mutex_unlock()\n", MOD_NAME);
        } else {
            printk("%s: [dev_read] mutex_lock correttamente rilasciato\n", MOD_NAME);
        }
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return 0;

    }

}

//la dev_open() apre il dispositivo (deve farlo in modalità di sola scrittura).
static int dev_open(struct inode *inode, struct file *file) {

    //incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(au_info.usages));

    //sanity checks
    if (!au_info.is_mounted) {
        printk("%s: impossibile aprire il dispositivo: il file system non è stato montato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODEV; //-ENODEV = file system non esistente
    }
    if (file->f_mode & FMODE_WRITE) {    //il dispositivo deve essere aperto in read only
        printk("%s: impossibile aprire il dispositivo in modalità scrittura\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EPERM;  //-EPERM = operazione non consentita
    }

    printk("%s: device successfully opened\n", MOD_NAME);
    atomic_fetch_add(-1, &(au_info.usages));
  	return 0;

}

//la dev_release() chiude il dispositivo.
static int dev_release(struct inode *inode, struct file *file) {

    //incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(au_info.usages));

    //sanity checks
    if (!au_info.is_mounted) {
        printk("%s: impossibile chiudere il dispositivo: il file system non è stato montato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODEV; //-ENODEV = file system non esistente
    }

    printk("%s: device successfully closed\n", MOD_NAME);
    atomic_fetch_add(-1, &(au_info.usages));
  	return 0;

}

const struct file_operations fops = {
  .owner = THIS_MODULE,
  .read = dev_read,
  .open = dev_open,
  .release = dev_release,
};
