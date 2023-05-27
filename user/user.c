#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "filesystem/singlefilefs.h"

#define PUT_SYSCALL 134
#define GET_SYSCALL 156
#define INVALIDATE_SYSCALL 174

//FUNCTIONS PROTOTYPES
void put_operation(void);
void get_operation(void);
void invalidate_operation(void);

void put_operation() {

    char *source;
    size_t size;
    int ret;
    int source_size;

    //inizializzazione di size a un valore invalido; rimarrà tale se l'input fornito dall'utente non è conforme.
    size = -1;  //corrisponde a MAX_VALUE_FOR_SIZE_T - 1

    printf("How many bytes would you like to write on the device block?\n");
    fflush(stdout);
    scanf("%lu", &size);
    //sanity check
    if (size == -1) {   //corrisponde a if(size == MAX_VALUE_FOR_SIZE_T - 1)
        printf("Sorry, the provided input is incorrect. Please try again.\n");
        fflush(stdout);
        return;
    }

    //qui viene stabilito quanti byte devono essere allocati per il buffer source.
    if (size < DEFAULT_BLOCK_SIZE-METADATA_SIZE)
        source_size = (int)size;
    else
        source_size = DEFAULT_BLOCK_SIZE-METADATA_SIZE;

    source = malloc(source_size);
    if (!source) {
        printf("[ERRORE] Problema di allocazione della memoria.\n");
        fflush(stdout);
        exit(-1);      
    }

    printf("Please insert here the data you would like to write on the device block.\n");
    fflush(stdout);
    fgets(source, source_size, stdin);

    ret = syscall(PUT_SYSCALL, source, size);
    if (ret < 0) {
        printf("[ERRORE] Si è verificato un problema durante l'esecuzione della system call [%d].\n", ret);
        fflush(stdout);
    }
    else {
        printf("INDEX OF WRITTEN BLOCK: %d", ret);
        fflush(stdout);
    }

    return;

}

void get_operation() {

    int offset;
    char *destination;
    size_t size;
    int ret;
    size_t destination_size;

    //inizializzazione di offset e size a valori invalidi; rimarranno tali se l'input fornito dall'utente non è conforme.
    offset = -1;
    size = -1;  //corrisponde a MAX_VALUE_FOR_SIZE_T - 1

    printf("Which device block would you like to read?\n");
    fflush(stdout);
    scanf("%d", &offset);
    //sanity check
    if (offset < 0) {
        printf("Sorry, the provided input is incorrect. Please try again.\n");
        fflush(stdout);
        return;
    }

    printf("How many bytes would you like to read from the device block?\n");
    fflush(stdout);
    scanf("%lu", &size);
    //sanity check
    if (size == -1) {   //corrisponde a if(size == MAX_VALUE_FOR_SIZE_T - 1)
        printf("Sorry, the provided input is incorrect. Please try again.\n");
        fflush(stdout);
        return;
    }

    //qui viene stabilito quanti byte devono essere allocati per il buffer destination.
    if (size < DEFAULT_BLOCK_SIZE-METADATA_SIZE)
        destination_size = size;
    else
        destination_size = DEFAULT_BLOCK_SIZE-METADATA_SIZE;

    destination = malloc(destination_size);
    if (!destination) {
        printf("[ERRORE] Problema di allocazione della memoria.\n");
        fflush(stdout);
        exit(-1);      
    }

    ret = syscall(GET_SYSCALL, offset, destination, size);
    if (ret < 0) {
        printf("[ERRORE] Si è verificato un problema durante l'esecuzione della system call [%d].\n", ret);
        fflush(stdout);
    }
    else {
        printf("READ DATA: %s\n", destination);
        printf("NUMBER OF READ BYTES: %d", ret);
        fflush(stdout);
    }

    return;

}

void invalidate_operation() {

    int offset;
    int ret;

    //inizializzazione di offset a un valore invalido; rimarrà tale se l'input fornito dall'utente non è conforme.
    offset = -1;

    printf("Which device block would you like to invalidate?\n");
    fflush(stdout);
    scanf("%d", &offset);
    //sanity check
    if (offset < 0) {
        printf("Sorry, the provided input is incorrect. Please try again.\n");
        fflush(stdout);
        return;
    }

    ret = syscall(INVALIDATE_SYSCALL, offset);
    if (ret < 0) {
        printf("[ERRORE] Si è verificato un problema durante l'esecuzione della system call [%d].\n", ret);
        fflush(stdout);
    }
    else {
        printf("L'operazione di invalidazione del blocco %d è stata eseguita con successo.\n", offset);
        fflush(stdout);
    }

    return;    

}

int main(int argc, char **argv) {

    char *selected_command;

    //i due caratteri che deve ospitare select_command sono il codice numerico dell'operazione da effettuare e lo '\0'.
    selected_command = malloc(2);
    if (!selected_command) {
        printf("[ERRORE] Problema di allocazione della memoria.\n");
        fflush(stdout);
        return -1;
    }

    while(1) {

        printf("\033[2J\033[H");    //clean the shell
        printf("*** Hi! What should I do for you? ***\n\n");
        printf("1) Write on a data block\n");
        printf("2) Read a data block\n");
        printf("3) Invalidate a data block\n");
        printf("4) Quit\n");
        fflush(stdout);

        fgets(selected_command, 2, stdin);

        switch (selected_command) {
            case "1":
                put_operation();
                break;
            
            case "2":
                get_operation();
                break;

            case "3":
                invalidate_operation();
                break;

            case "4":
                printf("Bye!\n");
                fflush(stdout);
                return 0;

            default:
                printf("Sorry, the provided input is incorrect. Please try again.\n");
                fflush(stdout);
                break;

        }

        printf("\nPress any key to continue...\n");
        fflush(stdout);
        getchar();

    }

}
