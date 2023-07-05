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
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/string.h>
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

struct sorted_node *first_sorted_node;

//SYSTEM CALLS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size)
#else
asmlinkage int sys_put_data(char *source, size_t size)
#endif
{
    char *kernel_lvl_src;   //buffer di livello kernel (inizializzato con una copy_from_user()) in cui verrà posto l'input della put
    size_t bytes_to_write;  //non è detto che size e la lunghezzaa di source corrispondano.
    int index;  //variabile che tiene traccia del numero di iterazione all'interno del ciclo che itera sulla RCU list; index-2 sarà il valore di ritorno della system call.
    int ret;
    unsigned long ulong_ret;    //serve specificatamente per la copy_from_user().
    struct onefilefs_sb_info *sb_disk;
    struct rcu_node *new_node;
    struct rcu_node *curr_node;
    int old_total_writes;   //valore di total_writes originariamente letto dal superblocco (se la put va a buon fine, verrà incrementato di 1)

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

    //riporto in un buffer del kernel delle informazioni da scrivere su un blocco del dispositivo
    bytes_to_write = strnlen(source, size);    //bytes_to_write determina la quantità di memoria da allocare per il kernel buffer in cui verrà posto l'input della put
    if (bytes_to_write < size)
        bytes_to_write++;   //strnlen() non conta l'eventuale '\0': lo aggiungo a mano.

    kernel_lvl_src = kmalloc(bytes_to_write, GFP_KERNEL);
    if (!kernel_lvl_src) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore con l'allocazione della memoria\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENOMEM; //-ENOMEM = errore di esaurimento della memoria         
    }
    ulong_ret = copy_from_user(kernel_lvl_src, source, (unsigned long)bytes_to_write);  //ulong_ret è il numero di byte NON copiati (su un massimo di bytrs_to_write).
    bytes_to_write = bytes_to_write - (size_t)ulong_ret;    //dai bytes_to_write togliamo i residui di copy_from_user().

    //creazione del nodo da inserire nella RCU list al posto di quello da modificare
    new_node = kmalloc(sizeof(struct rcu_node), GFP_KERNEL);
    if (!new_node) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore con l'allocazione della memoria\n", MOD_NAME);
        kfree(kernel_lvl_src);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENOMEM; //-ENOMEM = errore di esaurimento della memoria 
    }

    //utilizzo di mutex per sincronizzare le scritture tra loro
    #ifdef DEBUG
    printk("%s: [put_data] acquisizione del mutex_lock in corso...\n", MOD_NAME);
    #endif
    ret = mutex_trylock(&(au_info.write_mutex));
    if (ret == 0) {
        #ifdef DEBUG
        printk("%s: impossibile eseguire la system call put_data(): il lock è occupato\n", MOD_NAME);
        #endif
        kfree(kernel_lvl_src);
        kfree(new_node);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EBUSY;
    }
    #ifdef DEBUG
    printk("%s: [put_data] mutex_lock correttamente acquisito\n", MOD_NAME);
    #endif

    sb_disk = get_superblock_info(global_sb);
    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        kfree(kernel_lvl_src);
        kfree(new_node);
        mutex_unlock(&(au_info.write_mutex));
        #ifdef DEBUG
        if (mutex_is_locked(&(au_info.write_mutex))) {
            printk("%s: [put_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
        } else {
            printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        }
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }

    //sincronizzazione RCU per individuare un eventuale blocco libero; è sufficiente scandire la RCU list senza leggere dati dal device.
    #ifdef DEBUG
    printk("%s: [put_data] acquisizione del rcu_read_lock in corso...\n", MOD_NAME);
    #endif
    rcu_read_lock();
    #ifdef DEBUG
    printk("%s: [put_data] rcu_read_lock correttamente acquisito\n", MOD_NAME);
    #endif

    index = 0;
    curr_node = NULL;
    old_total_writes = sb_disk->user_sb.total_writes;

    /* Costrutto che itera su tutti i nodi della RCU list
     *@param curr_node: struct rcu_node che, a ogni iterazione del ciclo, tiene traccia del nodo corrente della RCU list
     *@param &(sb_disk->rcu_head): puntatore alla list head dell'elemento artificiale del superblock
     *@param lh: campo della struct rcu_node di tipo struct list_head
     */
    list_for_each_entry_rcu(curr_node, &(sb_disk->rcu_head), lh) {        
        if (index >= 2 && !(curr_node->is_valid))    //sto cercando un blocco libero, ovvero un blocco non valido (index >= 2 perché non voglio il superblocco o l'inode del file).
            break;

        index++;
        if (index == sb_disk->user_sb.total_data_blocks+2) {  //sono andato oltre l'ultimo nodo della RCU list; significa che nessun nodo rispetta la condizione (nessun nodo è libero).
            printk("%s: impossibile eseguire la system call put_data(): non ci sono blocchi liberi\n", MOD_NAME);
            kfree(kernel_lvl_src);
            kfree(new_node);
            rcu_read_unlock();
            mutex_unlock(&(au_info.write_mutex));
            #ifdef DEBUG
            if (mutex_is_locked(&(au_info.write_mutex))) {
                printk("%s: [put_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
            } else {
                printk("%s: [put_data] mutex_lock e rcu_read_lock correttamente rilasciati\n", MOD_NAME);
            }
            #endif
            atomic_fetch_add(-1, &(au_info.usages));
            return -ENOMEM; //-ENOMEM = errore di esaurimento della memoria
        }

    }
    rcu_read_unlock();
    #ifdef DEBUG
    printk("%s: [put_data] rcu_read_lock correttamente rilasciato\n", MOD_NAME);
    #endif

    //inizializzazione del nuovo nodo
    new_node->write_counter = old_total_writes+1; //l'ultimo write_counter e total_writes assumono lo stesso valore: il nuovo write_counter deve essere pari a old_total_writes+1
    new_node->is_valid = 1;

    //scrittura vera e propria del superblocco del dispositivo
    ret = set_superblock_info(global_sb, new_node->write_counter);
    if (ret < 0) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore con la scrittura dei dati sul superblocco\n", MOD_NAME);
        kfree(kernel_lvl_src);
        kfree(new_node);
        mutex_unlock(&(au_info.write_mutex));
        #ifdef DEBUG
        if (mutex_is_locked(&(au_info.write_mutex))) {
            printk("%s: [put_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
        } else {
            printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        }
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output        
    }

    //scrittura vera e propria del blocco all'interno del dispositivo (metadati+payload)
    ret = set_block_content(global_sb, index, new_node->write_counter, kernel_lvl_src, bytes_to_write);
    if (ret < 0) {
        printk("%s: impossibile eseguire la system call put_data(): si è verificato un errore con la scrittura dei dati sul blocco %d\n", MOD_NAME, index-2);
        kfree(kernel_lvl_src);
        kfree(new_node);
        mutex_unlock(&(au_info.write_mutex));
        #ifdef DEBUG
        if (mutex_is_locked(&(au_info.write_mutex))) {
            printk("%s: [put_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
        } else {
            printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        }
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output        
    }

    //sostituzione di curr_node con new_node all'interno della RCU list
    list_replace_rcu(&(curr_node->lh), &(new_node->lh));

    mutex_unlock(&(au_info.write_mutex));
    #ifdef DEBUG
    if (mutex_is_locked(&(au_info.write_mutex))) {
        printk("%s: [put_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
    } else {
        printk("%s: [put_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
    }
    #endif

    synchronize_rcu();  //funzione che serve a far sì che lo scrittore attenda la terminazione del grace period
    kfree(kernel_lvl_src);
    kfree(curr_node);

    printk("%s: la system call put_data() sul blocco %d è stata eseguita con successo\n", MOD_NAME, index-2);
    atomic_fetch_add(-1, &(au_info.usages));
    return index-2;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size)
#else
asmlinkage int sys_get_data(int offset, char *destination, size_t size)
#endif
{   
    int index;                      //variabile che tiene traccia del numero di iterazione all'interno del ciclo che itera sulla RCU list
    struct onefilefs_sb_info *sb_disk;
    struct data_block_content *db_cont;
    struct rcu_node *curr_node;
    int readable_bytes;             //numero di byte effettivamente presentu nel blocco target prima del terminatore di stringa ('\0')
    int lost_bytes_copy_to_user;    //numero di byte (tra quelli letti con kernel_read()) che non è stato possibile consegnare all'utente con copy_to_user()

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

    sb_disk = get_superblock_info(global_sb);
    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb_disk->user_sb.total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
        printk("%s: impossibile eseguire la system call get_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    //sincronizzazione RCU per effettuare l'operazione di lettura del blocco dati di posizione offset
    #ifdef DEBUG
    printk("%s: [get_data] acquisizione del rcu_read_lock in corso...\n", MOD_NAME);
    #endif
    rcu_read_lock();
    #ifdef DEBUG
    printk("%s: [get_data] rcu_read_lock correttamente acquisito\n", MOD_NAME);
    #endif

    index = 0;
    curr_node = NULL;

    /* Costrutto che itera su tutti i nodi della RCU list
     *@param curr_node: struct rcu_node che, a ogni iterazione del ciclo, tiene traccia del nodo corrente della RCU list
     *@param &(sb_disk->rcu_head): puntatore alla list head dell'elemento artificiale del superblock
     *@param lh: campo della struct rcu_node di tipo struct list_head
     */
    list_for_each_entry_rcu(curr_node, &(sb_disk->rcu_head), lh) {
        if (index == offset+2)    //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
            break;        
        index++;

    }

    //check sulla validità del blocco target
    if(!(curr_node->is_valid)) {
        printk("%s: impossibile eseguire la system call get_data(): il blocco specificato (%d) non è valido\n", MOD_NAME, offset);
        rcu_read_unlock();
        #ifdef DEBUG
        printk("%s: [get_data] rcu_read_lock correttamente rilasciato\n", MOD_NAME);
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODATA; //-ENODATA = nessun dato disponibile
    }

    rcu_read_unlock();
    #ifdef DEBUG
    printk("%s: [get_data] rcu_read_lock correttamente rilasciato\n", MOD_NAME);
    #endif

    //recupero del contenuto del blocco da leggere
    db_cont = get_block_content(global_sb, offset+2);   //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
    if (db_cont == NULL) {
        printk("%s: impossibile eseguire la system call get_data(): si è verificato un errore col recupero dei dati del blocco %d\n", MOD_NAME, offset);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }

    //considero il numero di byte letti come la lunghezza della stringa letta (eventualmente fino a '\0'); in mancanza di '\0', verrà restituito ret.
    readable_bytes = (int)strnlen(&(db_cont->payload[0]), size);
    if (readable_bytes == 0) {
        printk("%s: la system call get_data() sul blocco %d è stata eseguita con successo ma non ci sono dati da leggere\n", MOD_NAME, offset);
        atomic_fetch_add(-1, &(au_info.usages));
        return 0;
    }
    else if (readable_bytes > 0 && readable_bytes < size)
        readable_bytes++;   //strnlen() non conta l'eventuale '\0': lo aggiungo a mano.

    //consegna dei dati all'utente
    lost_bytes_copy_to_user = copy_to_user(destination, &(db_cont->payload[0]), readable_bytes);

    printk("%s: la system call get_data() sul blocco %d è stata eseguita con successo\n", MOD_NAME, offset);
    atomic_fetch_add(-1, &(au_info.usages));
    return readable_bytes - lost_bytes_copy_to_user;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset)
#else
asmlinkage int sys_invalidate_data(int offset)
#endif
{
    int index;
    int ret;
    struct onefilefs_sb_info *sb_disk;
    struct rcu_node *new_node;
    struct rcu_node *curr_node;

    //incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(au_info.usages));

    //sanity checks
    if (!au_info.is_mounted) {
        printk("%s: impossibile eseguire la system call invalidate_data(): il file system non è stato montato\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODEV; //-ENODEV = file system non esistente
    }

    //creazione del nodo da inserire nella RCU list al posto di quello da invalidare
    new_node = kmalloc(sizeof(struct rcu_node), GFP_KERNEL);
    if (!new_node) {
        printk("%s: impossibile eseguire la system call invalidate_data(): si è verificato un errore con l'allocazione della memoria\n", MOD_NAME);
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENOMEM; //-ENOMEM = errore di esaurimento della memoria 
    }

    //utilizzo di mutex per sincronizzare le scritture tra loro (di fatto anche l'invalidazione risulta essere una scrittura nella RCU list)
    #ifdef DEBUG
    printk("%s: [invalidate_data] acquisizione del mutex_lock in corso...\n", MOD_NAME);
    #endif
    ret = mutex_trylock(&(au_info.write_mutex));
    if (ret == 0) {
        #ifdef DEBUG
        printk("%s: impossibile eseguire la system call put_data(): il lock è occupato\n", MOD_NAME);
        #endif
        kfree(new_node);
        atomic_fetch_add(-1, &(au_info.usages));
        return -EBUSY;
    }
    #ifdef DEBUG
    printk("%s: [invalidate_data] mutex_lock correttamente acquisito\n", MOD_NAME);
    #endif

    sb_disk = get_superblock_info(global_sb);
    if (sb_disk == NULL) {
        printk("%s: impossibile eseguire la system call invalidate_data(): si è verificato un errore col recupero dei dati del superblocco\n", MOD_NAME);
        kfree(new_node);
        mutex_unlock(&(au_info.write_mutex));
        #ifdef DEBUG
        if (mutex_is_locked(&(au_info.write_mutex))) {
            printk("%s: [invalidate_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
        } else {
            printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        }
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output
    }
    if (offset < 0 || offset >= sb_disk->user_sb.total_data_blocks) {    //stiamo assumendo offset che vanno da 0 a NBLOCKS-1
        printk("%s: impossibile eseguire la system call invalidate_data(): il blocco specificato (%d) non esiste\n", MOD_NAME, offset);
        kfree(new_node);
        mutex_unlock(&(au_info.write_mutex));
        #ifdef DEBUG
        if (mutex_is_locked(&(au_info.write_mutex))) {
            printk("%s: [invalidate_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
        } else {
            printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        }
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return -EINVAL; //-EINVAL = parametri non validi (in questo caso int offset)
    }

    //sincronizzazione RCU per effettuare il retrieve del blocco dati di posizione offset
    #ifdef DEBUG
    printk("%s: [invalidate_data] acquisizione del rcu_read_lock in corso...\n", MOD_NAME);
    #endif
    rcu_read_lock();
    #ifdef DEBUG
    printk("%s: [invalidate_data] rcu_read_lock correttamente acquisito\n", MOD_NAME);
    #endif

    index = 0;
    curr_node = NULL;

    /* Costrutto che itera su tutti i nodi della RCU list
     *@param curr_node: struct rcu_node che, a ogni iterazione del ciclo, tiene traccia del nodo corrente della RCU list
     *@param &(sb_disk->rcu_head): puntatore alla list head dell'elemento artificiale del superblock
     *@param lh: campo della struct rcu_node di tipo struct list_head
     */
    list_for_each_entry_rcu(curr_node, &(sb_disk->rcu_head), lh) {
        if (index == offset+2)    //il +2 è dato dal fatto che bisogna contare anche superblocco e inode del file.
            break;        
        index++;

    }

    //check sulla validità del blocco target
    if(!(curr_node->is_valid)) {
        printk("%s: impossibile eseguire la system call invalidate_data(): il blocco specificato (%d) è già invalido\n", MOD_NAME, offset);
        kfree(new_node);
        rcu_read_unlock();
        mutex_unlock(&(au_info.write_mutex));
        #ifdef DEBUG
        if (mutex_is_locked(&(au_info.write_mutex))) {
            printk("%s: [invalidate_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
        } else {
            printk("%s: [invalidate_data] mutex_lock e rcu_read_lock correttamente rilasciati\n", MOD_NAME);
        }
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return -ENODATA; //-ENODATA = nessun dato disponibile
    }
    rcu_read_unlock();
    #ifdef DEBUG
    printk("%s: [invalidate_data] rcu_read_lock correttamente rilasciato\n", MOD_NAME);
    #endif

    //inizializzazione del nuovo nodo
    new_node->write_counter = curr_node->write_counter; //non vado a modificare il write counter (non si tratta di un'operazione di put_data)
    new_node->is_valid = 0;

    //invalidazione vera e propria del blocco all'interno del dispositivo (interessano in particolar modo solo i metadati)
    ret = invalidate_block_content(global_sb, offset+2);
    if (ret < 0) {
        printk("%s: impossibile eseguire la system call invalidate_data(): si è verificato un errore con l'invalidazione dei dati sul blocco %d\n", MOD_NAME, offset);
        kfree(new_node);
        mutex_unlock(&(au_info.write_mutex));
        #ifdef DEBUG
        if (mutex_is_locked(&(au_info.write_mutex))) {
            printk("%s: [invalidate_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
        } else {
            printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
        }
        #endif
        atomic_fetch_add(-1, &(au_info.usages));
        return -EIO; //-EIO = errore di input/output        
    }

    //sostituzione di curr_node con new_node all'interno della RCU list
    list_replace_rcu(&(curr_node->lh), &(new_node->lh));

    mutex_unlock(&(au_info.write_mutex));
    #ifdef DEBUG
    if (mutex_is_locked(&(au_info.write_mutex))) {
        printk("%s: [invalidate_data] c'è un problema con mutex_unlock()\n", MOD_NAME);
    } else {
        printk("%s: [invalidate_data] mutex_lock correttamente rilasciato\n", MOD_NAME);
    }
    #endif

    synchronize_rcu();  //funzione che serve a far sì che lo scrittore attenda la terminazione del grace period
    kfree(curr_node);

    printk("%s: la system call invalidate_data() sul blocco %d è stata eseguita con successo\n", MOD_NAME, offset);
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
