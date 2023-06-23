/* Questo file si pone l'obiettivo di testare la sincronizzazione data dal modulo kernel sviluppato.
 * Per far ciò, vengono generati diversi thread, i quali andranno a eseguire concorrentemente le operazioni
 * di put_data(), get_data(), invalidate_data() e dev_read(), riportando anche le informazioni relative alle
 * relative esecuzioni, tra cui il timestamp di inizio esecuzione e il timestamp di fine esecuzione.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"

pthread_barrier_t barrier;  //barriera che serve a far partire tutti i thread contemporaneamente con l'invocazione delle operazioni

//FUNCTIONS PROTOTYPES
void invoke_put_data(void *);
void invoke_get_data(void *);
void invoke_invalidate_data(void *);
void launch_cat(void *);

void invoke_put_data(void *arg) {

    pthread_t tid;
    char *source;   //primo parametro della syscall put_data()
    size_t size;    //secondo parametro della syscall put_data()
    int ret;
    unsigned long timestamp;

    tid = *(pthread_t *)arg;
    source = malloc(SIZE_SOURCE_STR);
    if (!source) {
        printf("[ERRORE] Problema di allocazione della memoria.\n");
        fflush(stdout);
        exit(-1); 
    }

    sprintf(source, "Scrittura da parte del thread %d\n", tid);
    size = strlen(source);

    pthread_barrier_wait(&barrier); //attendo che tutti gli altri thread child raggiungano la barriera.

    RDTSC(timestamp);
    printf("[THREAD %d] Sto per invocare put_data(). Timestamp = %lu.\n", tid, timestamp);
    fflush(stdout);

    ret = syscall(PUT_SYSCALL, source, size);

    RDTSC(timestamp);
    printf("[THREAD %d] Ho terminato l'esecuzione di put_data(). Timestamp = %lu.\n", tid, timestamp);
    fflush(stdout);

    if (ret < 0) {
        printf("[THREAD %d] L'esecuzione di put_data() NON è andata a buon fine.\n", tid);
        fflush(stdout);
        exit(-1);
    }
    else {
        printf("[THREAD %d] L'esecuzione di put_data() è andata a buon fine.\n", tid);
        fflush(stdout);
        exit(0);
    }

}

void invoke_get_data(void *arg) {

    pthread_t tid;

    tid = *(pthread_t *)arg;
    pthread_barrier_wait(&barrier);
    
}

void invoke_invalidate_data(void *arg) {

    pthread_t tid;

    tid = *(pthread_t *)arg;
    pthread_barrier_wait(&barrier);
    
}

//è opportuno riversare l'output di cat in un file temporaneo.
void launch_cat(void *arg) {

    pthread_t tid;

    tid = *(pthread_t *)arg;
    pthread_barrier_wait(&barrier);
    
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
                ret = pthread_create(&tids[thread_index], NULL, invoke_put_data, &tids[thread_index]);
                break;
            
            case 1:
                ret = pthread_create(&tids[thread_index], NULL, invoke_get_data, &tids[thread_index]);
                break;

            case 2:
                ret = pthread_create(&tids[thread_index], NULL, invoke_invalidate_data, &tids[thread_index]);
                break;

            case 3:
                ret = pthread_create(&tids[thread_index], NULL, launch_cat, &tids[thread_index]);
                break;

            default:
                printf("[ERROR] Something went wrong during test execution.\n");
                fflush(stdout);
                return -1;

        }
        if (ret != 0) {
            printf("[ERRORE] Problema di creazione dei thread.\n");
            fflush(stdout);
            return -1;
        }

    }

    //attesa della terminazione di tutti i thread child
    for (thread_index=0; thread_index<NTHREADS; thread_index++) {
        pthread_join(tids[thread_index], NULL);
    }

    //cleanup
    free(tids);
    pthread_barrier_destroy(&barrier);
    return 0;

}
