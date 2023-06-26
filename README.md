# Progetto SOA: block-level data management service
## Autore
* :man_technologist: Matteo Fanfarillo (matricola: 0316179)

## Indice
1. [Introduzione](#introduzione)
2. [Strutture dati utilizzate](#strutture-dati-utilizzate)
3. [Montaggio e smontaggio del file system](#montaggio-e-smontaggio-del-file-system)
4. [System call](#system-call)
5. [File operation](#file-operation)
6. [Sincronizzazione](#sincronizzazione)
7. [Software di livello user](#software-di-livello-user)
8. [Howto](#howto)

## Introduzione
Lo scopo del progetto è quello di realizzare un modulo kernel che implementa un device driver composto da molteplici blocchi di memoria, dove ciascun blocco di memoria può ospitare un messaggio user. Ogni blocco ha una dimensione pari a 4KB, alcuni dei quali sono però riservati per ospitare dei metadati. Il device driver supporta sia alcune system call che alcune file operation. Le system call sono elencate qui di seguito:
1. ```int put_data(char *source, size_t size)``` inserisce in un blocco inizialmente non valido (i.e. libero) fino a *size* byte del contenuto del buffer *source*. Restituisce l'indice del blocco che è stato sovrascritto in caso di successo, mentre restituisce l'errore ENOMEM nel caso in cui non ci sono blocchi liberi.
2. ```int get_data(int offset, char *destination, size_t size)``` legge fino a *size* byte del blocco di indice *offset* e riporta i dati letti nel buffer *destination* da consegnare all'utente. Restituisce il numero di byte copiati nel buffer *destination* in caso di successo, mentre restituisce l'errore ENODATA nel caso in cui il blocco specificato non è valido.
3. ```int invalidate_data(int offset)``` invalida il blocco di indice *offset*. Restituisce 0 in caso di successo, mentre restituisce l'errore ENODATA nel caso in cui il blocco specificato era già invalido.

Le file operation, invece, sono riportate di seguito:
1. ```int dev_open(struct inode *inode, struct file *file)``` apre il dispositivo come stream di byte.
2. ```int dev_release(struct inode *inode, struct file *file)``` chiude il file associato al dispositivo.
3. ```ssize_t dev_read(struct file *filp, char *buf, size_t len, loff_t *off)``` legge tutti e soli i blocchi correntemente validi, e li legge esattamente nell'ordine con cui i rispettivi dati sono stati scritti con la system call put_data() (per cui gli indici dei blocchi non sono rilevanti).

Le specifiche del progetto prevedono anche le seguenti proprietà:
* A compile-time deve essere stabilito se le scritture derivanti dalla system call put_data() devono essere effettuate in maniera sincrona oppure tramite il page-cache write back daemon.
* A compile-time deve essere stabilito anche il valore di NBLOCKS, che è il numero di blocchi massimo che possono comporre il dispositivo. Se durante l'operazione di montaggio del device risulta un numero di blocchi realmente esistenti maggiore di NBLOCKS, il montaggio stesso deve fallire.
* Il dispositivo deve poter essere montato su qualunque directory del file system.
* Il device driver può supportare un solo montaggio per volta.
* Quando il dispositivo non è montato, qualunque system call o file operation deve fallire restituendo l'errore ENODEV.

## Strutture dati utilizzate
### Superblocco del dispositivo
È composto dai seguenti campi:
* ```uint64_t version``` indica la versione del file system.
* ```uint64_t magic``` indica il magic number associato al file system.
* ```uint64_t block_size``` indica la dimensione di ciascun blocco di memoria che compone il dispositivo.
* ```uint64_t total_data_blocks``` indica il numero di data block (esclusi superblocco e inode del file) che compogono il dispositivo.
* ```unsigned int total_writes``` è un contatore globale delle scritture effettuate nel dispositivo e funge da timestamp; in particolare, è pari al timestamp (i.e. al numero d'ordine) dell'ultima scrittura effettuata e serve a dare un ordinamento ai blocchi nel momento in cui si vuole leggere il dispositivo con la file operation dev_read().
* ```struct list_head rcu_head``` rappresenta la testa della lista RCU i cui nodi mantengono i metadati di ciascun blocco di memoria (oltre ai puntatori al nodo precedente e successivo all'interno della lista doppiamente collegata).

### Metadati dei blocchi
I blocchi sono stati progettati per mantenere 4 byte di metadati e 4092 byte di payload. I metadati comprendono due campi:
* ```unsigned int write_counter : 31``` è un campo a 31 bit che indica il numero d'ordine (o timestamp) della scrittura effettuata sul relativo blocco (se quest'ultimo è valido). È stato preferito rispetto a un timestamp che tiene traccia dell'istante di tempo espresso ad esempio in microsecondi in cui è avvenuta la scrittura perché è più compatto in termini di memoria occupata.
* ```unsigned int is_valid : 1``` è un campo a un solo bit che indica se il relativo blocco è valido o meno.

### Strutture memorizzate in RAM
A supporto delle operazioni del modulo vengono utilizzate anche delle strutture dati mantenute in memoria RAM. Tra queste figurano:
  ```
  auxiliary_info {
    uint64_t is_mounted;
    struct mutex write_mutex;
    struct mutex off_mutex;
  }
  ```
* ```uint64_t is_mounted``` indica se il file system risulta correntemente montato all'interno del sistema o meno. Viene consultato all'inizio di qualunque system call e file operation per stabilire se l'operazione può essere eseguita o meno.
* ```struct mutex write_mutex``` è il mutex utilizzato per coordinare tra loro le operazioni di scrittura sul dispositivo (in particolare le chiamate a put_data() e invalidate_data()).
* ```struct mutex off_mutex``` è il mutex utilizzato per coordinare tra loro le chiamate a dev_read() che, di fatto, richiedono molta attenzione poiché utilizzano dati condivisi come il puntatore loff_t *off, che punta all'offset del file da cui far partire la lettura, e la lista collegata di struct sorted_node, che permette di leggere i blocchi validi nell'ordine dato dal loro timestamp (write_counter) e i cui dettagli sono riportati di seguito.

```
struct sorted_node {
  int node_index;
  unsigned int write_counter;
  struct sorted_node *next;
}
```
* ```int node_index``` indica l'indice del blocco di riferimento.
* ```unsigned int write_counter``` indica il timestamp di scrittura del blocco di riferimento. Nel momento in cui viene creata la lista collegata di struct sorted_node, viene sfruttato proprio questo campo per stabilire l'ordine con cui i nodi devono essere disposti all'interno della lista stessa.
* ```struct sorted_node *next``` è il puntatore al nodo successivo della lista collegata.

## Montaggio e smontaggio del file system
TODO

## System call
TODO

## File operation
TODO

## Sincronizzazione
TODO

## Software di livello user
TODO

## Howto
TODO
