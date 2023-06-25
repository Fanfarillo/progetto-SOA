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
TODO

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
