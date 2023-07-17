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
Lo scopo del progetto è quello di realizzare un modulo kernel che implementa un device driver composto da molteplici blocchi di memoria, dove ciascun blocco di memoria può ospitare un messaggio user. Ogni blocco ha una dimensione pari a 4KB, alcuni dei quali sono riservati per ospitare dei metadati. Il device driver supporta sia alcune system call che alcune file operation. Le system call sono elencate qui di seguito:
1. ```int put_data(char *source, size_t size)``` inserisce in un blocco inizialmente non valido (i.e. libero) fino a *size* byte del contenuto del buffer *source*. Restituisce l'indice del blocco che è stato sovrascritto in caso di successo, mentre restituisce l'errore ENOMEM nel caso in cui non ci sono blocchi liberi.
2. ```int get_data(int offset, char *destination, size_t size)``` legge fino a *size* byte del blocco di indice *offset* e riporta i dati letti nel buffer *destination* da consegnare all'utente. Restituisce il numero di byte copiati nel buffer *destination* in caso di successo, mentre restituisce l'errore ENODATA nel caso in cui il blocco specificato non è valido.
3. ```int invalidate_data(int offset)``` invalida il blocco di indice *offset*. Restituisce 0 in caso di successo, mentre restituisce l'errore ENODATA nel caso in cui il blocco specificato era già invalido.

Le file operation, invece, sono riportate di seguito:
1. ```int dev_open(struct inode *inode, struct file *file)``` apre il dispositivo come stream di byte.
2. ```int dev_release(struct inode *inode, struct file *file)``` chiude il file associato al dispositivo.
3. ```ssize_t dev_read(struct file *filp, char *buf, size_t len, loff_t *off)``` legge solo i blocchi correntemente validi, e li legge esattamente nell'ordine con cui i rispettivi dati sono stati scritti con la system call put_data() (per cui gli indici dei blocchi non sono rilevanti).

Le specifiche del progetto prevedono anche le seguenti proprietà:
* A compile-time deve essere stabilito se le scritture derivanti dalla system call put_data() devono essere effettuate in maniera sincrona oppure tramite il page-cache write back daemon.
* A compile-time deve essere stabilito anche il valore di NBLOCKS, che è il numero di blocchi massimo che possono comporre il dispositivo. Se durante l'operazione di montaggio del device risulta un numero di blocchi realmente esistenti maggiore di NBLOCKS, il montaggio stesso deve fallire.
* Il dispositivo deve poter essere montato su qualunque directory del file system del sistema.
* Il device driver può supportare un solo montaggio per volta.
* Quando il dispositivo non è montato, qualunque system call o file operation deve fallire restituendo l'errore ENODEV.

## Strutture dati utilizzate
### Superblocco del dispositivo
È composto dai seguenti campi:
* ```uint64_t version``` indica la versione del file system.
* ```uint64_t magic``` indica il magic number associato al file system.
* ```uint64_t block_size``` indica la dimensione di ciascun blocco di memoria che compone il dispositivo.
* ```uint64_t total_data_blocks``` indica il numero di data block (esclusi superblocco e inode del file) che compogono il dispositivo.
* ```uint64_t first_valid``` è l'indice del primo blocco, tra quelli attualmente validi, che è stato reso valido.
* ```uint64_t last_valid``` è l'indice dell'ultimo blocco, tra quelli attualmente validi, che è stato reso valido. Assieme a *first_valid*, costituisce la coppia (head, tail) di una lista doppiamente collegata di blocchi validi, il cui ordinamento, a partire dalla testa (i.e. da *first_valid*), corrisponde all'ordine in cui le scritture sono state eseguite. Chiaramente la lista collegata non si manifesta su una struttura dati diversa dal dispositivo a blocchi, bensì sono i metadati dei blocchi stessi a referenziare il blocco precedente e il blocco successivo.

### Metadati dei blocchi
I blocchi sono stati progettati per mantenere 8 byte di metadati e 4088 byte di payload. I metadati comprendono tre campi:
* ```int next_valid : 31``` è un campo a 31 bit che indica l'offset del blocco immediatamente successivo dal punto di vista dell'ordine delle scritture; vale -1 se non c'è alcun blocco successivo (per cui quello corrente è stato l'ultimo a essere scritto).
* ```int prev_valid : 31``` è un campo a 31 bit che indica l'offset del blocco immediatamente precedente dal punto di vista dell'ordine delle scritture; vale -1 se non c'è alcun blocco precedente (per cui quello corrente è stato il primo a essere scritto tra tutti i blocchi validi).
* ```int is_valid : 2``` è un campo a due bit che indica se il relativo blocco è valido o meno. Uno dei due bit in realtà è inutilizzato ma serve per far sì che la dimensione dei metadati di ciascun blocco sia esattamente pari a 8 byte.

### Struttura memorizzata in RAM
A supporto delle operazioni del modulo viene utilizzata anche una struttura dati mantenuta in memoria RAM:
  ```
  auxiliary_info {
    uint64_t is_mounted;
    atomic_t usages;
    struct mutex write_mutex;
    struct mutex off_mutex;
  }
  ```
* ```uint64_t is_mounted``` indica se il file system risulta correntemente montato all'interno del sistema o meno. Viene consultato all'inizio di qualunque system call e file operation per stabilire se l'operazione può essere eseguita o meno.
* ```atomic_t usages``` indica il numero di thread che stanno utilizzando correntemente il file system. Quando è diverso da zero, il file system stesso non può essere smontato dal sistema.
* ```struct mutex write_mutex``` è il mutex utilizzato per coordinare tra loro le operazioni di scrittura sul dispositivo (in particolare le chiamate a put_data() e invalidate_data()).
* ```struct mutex off_mutex``` è il mutex utilizzato per coordinare tra loro le chiamate a dev_read() le quali, di fatto, richiedono molta attenzione poiché utilizzano dati condivisi. Tra questi troviamo il puntatore loff_t *off*, che punta all'offset del file da cui far partire la lettura, e le variabili globali *is_first_call*, *is_last_call*, che indicano rispettivamente se ci troviamo alla prima e all'ultima chiamata a dev_read() all'interno di un ciclo relativo a una particolare lettura del dispositivo (che itera sui blocchi validi del dispositivo stesso).

## Montaggio e smontaggio del file system
### Creazione
Il file system viene anzitutto creato con l'ausilio di un software di livello user. Durante la fase di creazione del file system, vengono inizializzati il superblocco, l'inode del file e i data block (coi relativi metadati); tutti i data block inizialmente non validi vengono inizializzati a zero.

### Montaggio
Il montaggio vero e proprio del file system viene implementato da software di livello kernel. Qui vengono inizializzati i due mutex (*write_mutex* e *off_mutex*), viene impostato a 0 il valore di *usages* e viene impostato a 1 il valore di *is_mounted* con una chiamata a __sync_val_compare_and_swap() (in modo tale che il settaggio della variabile avvenga in modo atomico); se *is_mounted* valeva già 1, allora l'operazione di montaggio termina con un errore. Dopodiché viene effettuato un controllo sul numero di blocchi realmente esistenti all'interno del dispositivo: se eccede il valore di NBLOCKS definito come parametro all'interno del Makefile del progetto, vuol dire che si è verificato un problema interno e, come previsto dalle specifiche, l'operazione di montaggio termina con un errore.

Affinché il dispositivo abbia la possibilità di essere montato ovunque all'interno del file system del sistema, l'operazione di montaggio viene eseguita mediante il seguente comando shell:
  ```
  mount -o loop -t singlefilefs image $(MOUNT_DIR)
  ```
dove $(MOUNT_DIR) corrisponde alla directory dove si vuole montare il dispositivo.

### Smontaggio
L'operazione di smontaggio viene implementata dallo stesso software di livello kernel che prevede l'operazione di montaggio. Il valore di *usages* viene controllato in modo tale che lo smontaggio fallisca se è maggiore di zero, e il valore di *is_mounted* viene riportato a 0 in maniera atomica tramite una chiamata a __sync_val_compare_and_swap(); se *is_mounted* valeva già 0, vuol dire che il file system era già smontato e l'operazione di smontaggio termina con un errore.

## System call
### int put_data(char *source, size_t size)
1. Il valore di *usages* viene incrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().
2. Vengono effettuati dei sanity check in cui si verificano le seguenti condizioni:
   * is_mounted == 1
   * size <= 4092 (che corrisponde alla dimensione massima del payload all'interno di un singolo blocco)
   * source != NULL
3. Mediante una chiamata a copy_from_user(), il contenuto di *source* viene riversato in un buffer di livello kernel (_char *kernel_lvl_src_).
4. All'interno di un ciclo si cerca un blocco libero in cui riportare i dati in input. Nel caso in cui non esiste, la system call termina con l'errore ENOMEM.
5. Viene sovrascritto il blocco dati precedentemente individuato, aggiornandone sia il contenuto che i metadati (*next_valid* = -1, *prev_valid* = vecchio valore di *last_valid* all'interno del superblocco e *is_valid* = 1).
6. Viene sovrascritto il superblocco del dispositivo, in cui vengono aggiornati opportunamente i valori di *first_valid* e *last_valid*.
7. Il valore di *usages* viene decrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().

### int get_data(int offset, char *destination, size_t size)
1. Il valore di *usages* viene incrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().
2. Vengono effettuati dei sanity check in cui si verificano le seguenti condizioni:
   * is_mounted == 1
   * destination != NULL
   * 0 <= offset < NBLOCKS
3. Si accede al blocco di indice *offset*+2 (poiché bisogna tenere in considerazione anche di superblocco e inode del file, mentre il parametro *offset* considera esclusivamente i blocchi dati). Se il blocco è invalido, la system call termina con l'errore ENODATA; in caso contrario, si procede con gli step successivi.
4. Mediante una chiamata a copy_to_user(), il contenuto del buffer di livello kernel viene riportato all'interno di *destination*.
5. Il valore di *usages* viene decrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().

### int invalidate_data(int offset)
1. Il valore di *usages* viene incrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().
2. Vengono effettuati dei sanity check in cui si verificano le seguenti condizioni:
   * is_mounted == 1
   * 0 <= offset < NBLOCKS
3. Si accede al blocco di indice *offset*+2. Se il blocco era già invalido, la system call termina con l'errore ENODATA.
4. Viene sovrascritto il data block target, aggiornandone il metadato *is_valid*, che viene posto pari a zero. Inoltre, vengono modificati i metadati dell'eventuale blocco *prev_valid* e dell'eventuale blocco *next_valid* (in modo tale che non referenzino più il blocco target) e, nel caso in cui il blocco target era il *first_valid* e/o il *last_valid*, anche i metadati del superblocco.
5. Il valore di *usages* viene decrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().

## File operation
### int dev_open(struct inode *inode, struct file *file)
1. Il valore di *usages* viene incrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().
2. Vengono effettuati dei sanity check in cui si verificano le seguenti condizioni:
   * is_mounted == 1
   * Il file viene aperto in modalità read only.
3. Il dispositivo viene effettivamente aperto.
4. Il valore di *usages* viene decrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().

### int dev_release(struct inode *inode, struct file *file)
1. Il valore di *usages* viene incrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().
2. Viene effettuato il seguente sanity check:
   * is_mounted == 1
3. Il dispositivo viene effettivamente chiuso.
4. Il valore di *usages* viene decrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().

### ssize_t dev_read(struct file *filp, char *buf, size_t len, loff_t *off)
__Premessa:__ l'implementazione di questa funzione tiene conto del fatto che può essere invocata da parte di un comando o una funzione user-level (e.g. cat) all'interno di un loop, dove ciascuna iterazione può corrispondere alla lettura di un blocco. Ad esempio, un'unica chiamata al comando cat porta a molteplici invocazioni a dev_read(), una per ciascun blocco valido. Di conseguenza, le operazioni implementate all'interno di dev_read() sono quelle illustrate di seguito.
1. Il valore di *usages* viene incrementato di 1 in modo atomico mediante una chiamata ad atomic_fetch_add().
2. Viene effettuato il seguente sanity check:
   * is_mounted == 1
3. Se il valore della variabile globale *is_first_call* vale YES, vuol dire che si tratta della prima chiamata a dev_read() durante la lettura del dispositivo: in tal caso, è necessario recuperare il superblocco del dispositivo per recuperare il valore di *first_valid* che, se diverso da -1, rappresenta il primo blocco che dovrà essere letto. Il puntatore puntato dal parametro *off* viene aggiornato in modo tale da indicare l'offset corretto (in termini di numero di byte) da cui la lettura dovrà avere inizio. Dopodiché, si pone la variabile *is_first_call* pari a NO per segnalare che non si è più alla prima iterazione.
4. Se la variabile globale *is_last_call* (che indica se la lettura del dispositivo è stata completata) e il parametro *len* di dev_read() è non nullo, vuol dire che vi sono ancora dei dati da leggere, per cui viene restituito all'utente il contenuto (o la porzione del contenuto, in base al valore di *len*) del prossimo blocco da leggere. Il passaggio dei dati allo user avviene mediante la funzione copy_to_user() e, dopo la lettura, il valore di *off* viene aggiornato a seconda del valore di *next_valid* del blocco appena letto in modo tale che, eventualmente, si possa poi leggere il blocco successivo. Come ultima cosa, viene restituito al chiamante il numero di byte effettivamente letti: se tale numero è maggiore di zero, il chiamante invocherà nuovamente la funzione dev_read() in un'altra iterazione.
5. Nel momento in cui non vi sono più dati da leggere, dev_read() restituisce 0 dopo aver decrementato di 1 il valore di *usages* in modo atomico mediante una chiamata ad atomic_fetch_add(). Ciò comporta l'assegnazione del valore 0 alla variabile puntata da *off* e l'uscita dal loop di invocazioni a dev_read() da parte del codice di livello user sovrastante.

## Sincronizzazione
* ```get_data():``` qui si utilizza soltanto il rcu_read_lock(), ovvero il contatore atomico utilizzato dai lettori per determinare il grace period. Anche se viene denominato 'lock', non porta a un accesso esclusivo alle risorse, tant'è vero che, concorrentemente a un lettore, possono accedere al dispositivo sia altri lettori che gli scrittori.
* ```put_data():``` qui si utilizza il write_mutex, che serve per coordinare gli scrittori tra loro, cosa che non viene garantita direttamente dalla sincronizzazione basata sull'RCU.
* ```invalidate_data():``` anche qui si utilizza il medesimo write_mutex sfruttato dalla system call put_data().
* ```dev_read():``` qui si utilizzano il rcu_read_lock() e l'off_mutex. Il rcu_read_lock() è necessario perché si effettuano degli accessi in lettura al dispositivo; d'altra parte, è stato introdotto un ulteriore lock per coordinare tra loro le varie chiamate a dev_read(): infatti, in questa funzione vengono utilizzati dei dati condivisi che richiedono un'attenta sincronizzazione affinché gli accessi risultino corretti. Tali dati condivisi sono la variabile puntata dal pointer *off* (i.e. l'ultimo parametro in ingresso di dev_read()) e le variabili globali *is_first_call*, *is_last_call*.

## Software di livello user
Per utilizzare i servizi del modulo kernel implementato nel presente progetto, sono stati sviluppati due programmi user level: user.c (all'interno della directory user/) e test.c (all'interno della directory test/).
* ```user.c``` è il programma applicativo effettivamente utilizzabile dall'utente: è interattivo, per cui l'utente è in grado di scegliere l'operazione da eseguire e poi di inserire gli input che preferisce.
* ```test.c``` è un programma che serve esclusivamente a eseguire dei casi di test: qui viene generato un insieme di thread che invocano concorrentemente le system call put_data(), get_data() e invalidate_data() e il comando cat. Ciascun thread, durante la sua esecuzione, stampa in stdout le informazioni relative alle proprie operazioni (e.g. la funzione che sta per invocare, l'esito dell'invocazione, e così via).

## Howto
1. Configurare i parametri da definire a tempo di compilazione:
   * __DATA_BLOCKS__ all'interno del Makefile del progetto per definire il numero massimo di blocchi che costituiscono il dispositivo.
   * __MOUNT_DIR__ all'interno del Makefile del progetto per stabilire la directory in cui il dispositivo deve essere montato (NB: nel caso in cui si decide di modificare il valore di questa variabile, sarà necessario modificare di conseguenza la stringa definita come secondo parametro di sprintf() alla riga 189 del file test/test.c).
   * __SYNC__ all'interno del file header devFunctions.h. È da commentare nel caso in cui si vuole che le scritture all'interno del dispositivo avvengano tramite il page-cache write back daemon; è da decommentare nel caso in cui si vuole che le scritture avvengano in maniera sincrona.
2. Entrare nella directory syscall-table/ e lanciare nell'ordine i seguenti comandi:
   * ```make``` per compilare il modulo ausiliario che effettua la discovery della system call table (senza conoscere l'indirizzo di questa tabella non sarebbe possibile installare le tre nuove system call).
   * ```sudo make insmod``` per installare il modulo ausiliario che effettua la discovery della system call table.
3. Tornare alla directory principale e lanciare nell'ordine i seguenti comandi:
   * ```make``` per compilare i programmi user.c, test.c e il modulo del kernel che implementa il device driver.
   * ```sudo make insmod``` per installare il modulo del kernel che implementa il device driver.
   * ```sudo make create-fs``` per creare l'immagine del dispositivo.
   * ```sudo make mount-fs``` per montare effettivamente il dispositivo nella directory specificata dalla variabile $(MOUNT_DIR).
4. Per eseguire il programma user.o (generato dalla compilazione di user.c), basta entrare nella directory user/ e lanciare il comando ```./user.o```.
5. Per eseguire il programma test.o (generato dalla compilazione di test.c), è necessario entrare nella directory test/ e lanciare il comando ```./test.o```.
6. Per rimuovere il modulo che implementa il device driver ed effettuare il clean-up dei relativi file, basta lanciare i seguenti comandi:
   * ```make clean``` per rimuovere i file generati dalla compilazione del modulo.
   * ```sudo make unmount-fs``` per effettuare lo smontaggio del dispositivo.
   * ```sudo make rmmod``` per rimuovere il modulo del kernel dal sistema operativo.
   * ```sudo make del-image``` per rimuovere il file contenente l'immagine del dispositivo.
7. Per rimuovere il modulo ausiliario che effettua la discovery della system call table ed effettuare il clean-up dei relativi file, basta entrare nella directory syscall-table/ e lanciare i seguenti comandi:
   * ```make clean``` per rimuovere i file generati dalla compilazione del modulo ausiliario.
   * ```sudo make rmmod``` per rimuovere il modulo ausiliario dal sistema operativo.
