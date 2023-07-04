/* Questo file si pone l'obiettivo di testare la sincronizzazione data dal modulo kernel sviluppato.
 * Per far ciò, vengono generati diversi thread, i quali andranno a eseguire concorrentemente le operazioni
 * di put_data(), get_data(), invalidate_data() e dev_read(), riportando anche le informazioni relative alle
 * esecuzioni, tra cui il timestamp di inizio esecuzione e il timestamp di fine esecuzione.
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"
#include "../user/user.h"
#include "../filesystem/singlefilefs.h"

pthread_barrier_t barrier;  //barriera che serve a far partire tutti i thread contemporaneamente con l'invocazione delle operazioni

//FUNCTIONS PROTOTYPES
void *invoke_put_data(void *);
void *invoke_get_data(void *);
void *invoke_invalidate_data(void *);
void *launch_cat(void *);

void *invoke_put_data(void *arg) {

    pthread_t tid;
    char *source;   //primo parametro della syscall put_data()
    size_t size;    //secondo parametro della syscall put_data()
    int ret;
    unsigned long timestamp;

    tid = *(pthread_t *)arg;
    printf("[THREAD %ld] Eccomi qua, all'interno della funzione invoke_put_data().\n", tid);
    fflush(stdout);

    source = malloc(SIZE_SOURCE_STR);
    if (!source) {
        printf("[ERRORE] Problema di allocazione della memoria.\n");
        fflush(stdout);
        exit(-1); 
    }

    sprintf(source, "Scrittura da parte del thread %ld\n", tid);
    size = strlen(source);

    pthread_barrier_wait(&barrier); //attendo che tutti gli altri thread child raggiungano la barriera.

    RDTSC(timestamp);
    printf("\n[THREAD %ld] Sto per invocare put_data(). Timestamp = %lu.\n", tid, timestamp);
    fflush(stdout);

    while(1) {
        ret = syscall(PUT_SYSCALL, source, size);
        if (!(ret < 0 && errno == EBUSY))   //caso in cui non ci sono stati problemi di concorrenza
            break;

    }

    RDTSC(timestamp);
    printf("\n[THREAD %ld] Ho terminato l'esecuzione di put_data() sul blocco %d. Timestamp = %lu.\n", tid, ret, timestamp);
    fflush(stdout);

    if (ret < 0) {
        printf("\n[THREAD %ld] L'esecuzione di put_data() NON è andata a buon fine.\n", tid);
        fflush(stdout);
    }
    else {
        printf("\n[THREAD %ld] L'esecuzione di put_data() è andata a buon fine.\n", tid);
        fflush(stdout);
    }

    //cleanup
    free(source);

}

void *invoke_get_data(void *arg) {

    pthread_t tid;
    int offset;         //primo parametro della syscall get_data()
    char *destination;  //secondo parametro della syscall get_data()
    size_t size;        //terzo parametro della syscall get_data()
    int ret;
    unsigned long timestamp;

    tid = *(pthread_t *)arg;
    printf("[THREAD %ld] Eccomi qua, all'interno della funzione invoke_get_data().\n", tid);
    fflush(stdout);

    size = (size_t)DEFAULT_BLOCK_SIZE-METADATA_SIZE;
    destination = malloc(size);
    if (!destination) {
        printf("[ERRORE] Problema di allocazione della memoria.\n");
        fflush(stdout);
        exit(-1); 
    }

    offset = (int)(tid % TEST_BLOCKS);    //il blocco da leggere viene scelto in base al thread ID.

    pthread_barrier_wait(&barrier); //attendo che tutti gli altri thread child raggiungano la barriera.

    RDTSC(timestamp);
    printf("\n[THREAD %ld] Sto per invocare get_data(). Timestamp = %lu.\n", tid, timestamp);
    fflush(stdout);

    ret = syscall(GET_SYSCALL, offset, destination, size);

    RDTSC(timestamp);
    printf("\n[THREAD %ld] Ho terminato l'esecuzione di get_data() sul blocco %d. Timestamp = %lu.\n", tid, offset, timestamp);
    fflush(stdout);

    if (ret < 0) {
        printf("\n[THREAD %ld] L'esecuzione di get_data() NON è andata a buon fine.\n", tid);
        fflush(stdout);
    }
    else {
        printf("\n[THREAD %ld] L'esecuzione di get_data() è andata a buon fine. READ DATA: %s\n", tid, destination);
        fflush(stdout);
    }

    //cleanup
    free(destination);
    
}

void *invoke_invalidate_data(void *arg) {

    pthread_t tid;
    int offset; //parametro della system call invalidate_data()
    int ret;
    unsigned long timestamp;

    tid = *(pthread_t *)arg;
    printf("[THREAD %ld] Eccomi qua, all'interno della funzione invoke_invalidate_data().\n", tid);
    fflush(stdout);

    offset = (int)(tid % TEST_BLOCKS);    //il blocco da invalidare viene scelto in base al thread ID.

    pthread_barrier_wait(&barrier); //attendo che tutti gli altri thread child raggiungano la barriera.

    RDTSC(timestamp);
    printf("\n[THREAD %ld] Sto per invocare invalidate_data(). Timestamp = %lu.\n", tid, timestamp);
    fflush(stdout);

    while(1) {
        ret = syscall(INVALIDATE_SYSCALL, offset);
        if (!(ret < 0 && errno == EBUSY))   //caso in cui non ci sono stati problemi di concorrenza
            break;

    }

    RDTSC(timestamp);
    printf("\n[THREAD %ld] Ho terminato l'esecuzione di invalidate_data() sul blocco %d. Timestamp = %lu.\n", tid, offset, timestamp);
    fflush(stdout);

    if (ret < 0) {
        printf("\n[THREAD %ld] L'esecuzione di invalidate_data() NON è andata a buon fine.\n", tid);
        fflush(stdout);
    }
    else {
        printf("\n[THREAD %ld] L'esecuzione di invalidate_data() è andata a buon fine.\n", tid);
        fflush(stdout);
    }
    
}

//è opportuno riversare l'output di cat in un file temporaneo.
void *launch_cat(void *arg) {

    pthread_t tid;
    int ret;
    unsigned long timestamp;
    char *command;

    tid = *(pthread_t *)arg;
    printf("[THREAD %ld] Eccomi qua, all'interno della funzione launch_cat().\n", tid);
    fflush(stdout);

    command = malloc(SIZE_COMMAND_STR);
    if (!command) {
        printf("[ERRORE] Problema di allocazione della memoria.\n");
        fflush(stdout);
        exit(-1); 
    }

    sprintf(command, "cat ../mount/the-file");

    pthread_barrier_wait(&barrier); //attendo che tutti gli altri thread child raggiungano la barriera.

    RDTSC(timestamp);
    printf("\n[THREAD %ld] Sto per lanciare il comando cat. Timestamp = %lu.\n", tid, timestamp);
    fflush(stdout);

    ret = system(command);  //esecuzione del comando cat

    RDTSC(timestamp);
    printf("\n[THREAD %ld] Ho terminato l'esecuzione del comando cat. Timestamp = %lu.\n", tid, timestamp);
    fflush(stdout);

    if (ret != 0) {
        printf("\n[THREAD %ld] L'esecuzione del comando cat è NON è andata a buon fine.\n", tid);
        fflush(stdout);
    }
    else {
        printf("\n[THREAD %ld] L'esecuzione del comando cat è andata a buon fine.\n", tid);
        fflush(stdout);
    }

    //cleaup
    free(command);
    
}

int main(int argc, char **argv) {

    int thread_index;   //indice del ciclo for in cui vengono spawnati i thread figli
    int ret;
    long rand;
    int thread_type;    //valore che determina se ogni thread dovrà invocare put_data(), get_data(), invalidate_data() o dev_read()
    pthread_t *tids;    //area di memoria (da allocare) che ospiterà i thread ID di tutti i thread che vengono spawnati

    tids = malloc(NTHREADS*sizeof(pthread_t));
    if (!tids) {
        printf("[ERRORE] Problema di allocazione della memoria.\n");
        fflush(stdout);
        return -1;      
    }

    pthread_barrier_init(&barrier, NULL, NTHREADS); //inizializzazione della barriera

    for(thread_index=0; thread_index<NTHREADS; thread_index++) {

        rand = random();
        thread_type = rand % THREAD_TYPES;

        switch (thread_type) {
            case 0:
                /* Primo parametro: puntatore all'area di memoria che ospiterà il thread ID del nuovo thread
                 * Secondo parametro: puntatore a una tabella contenente info di startup del nuovo thread
                 * Terzo parametro: puntatore alla funzione da cui il nuovo thread inizierà l'esecuzione
                 * Quarto parametro: puntatore al parametro di input del nuovo thread
                 */
                ret = pthread_create(&tids[thread_index], NULL, launch_cat, &tids[thread_index]);
                break;               
            
            case 1:
                ret = pthread_create(&tids[thread_index], NULL, invoke_put_data, &tids[thread_index]);
                break;
                
            case 2:
                ret = pthread_create(&tids[thread_index], NULL, invoke_get_data, &tids[thread_index]);
                break;
                
            case 3:
                ret = pthread_create(&tids[thread_index], NULL, invoke_invalidate_data, &tids[thread_index]);
                break;

            default:
                printf("[ERROR] Something went wrong during test execution.\n");
                fflush(stdout);
                free(tids);
                pthread_barrier_destroy(&barrier);
                return -1;

        }
        if (ret != 0) {
            printf("[ERRORE] Problema di creazione dei thread.\n");
            fflush(stdout);
            free(tids);
            pthread_barrier_destroy(&barrier);
            return -1;
        }

    }

    //attesa della terminazione di tutti i thread child
    for(thread_index=0; thread_index<NTHREADS; thread_index++) {
        pthread_join(tids[thread_index], NULL);
    }

    //cleanup
    free(tids);
    pthread_barrier_destroy(&barrier);
    return 0;

}
