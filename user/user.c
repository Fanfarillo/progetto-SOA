#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "user.h"
#include "../filesystem/singlefilefs.h"

//FUNCTIONS PROTOTYPES
char multichoice(char *, char[], int);
void clear_stdin(void);
void clear_stdin_after_fgets(char *, int);

void put_operation(void);
void get_operation(void);
void invalidate_operation(void);

//funzione ausiliaria che si occupa di acquisire da stdin un valore tra tanti possibili
char multichoice(char *question, char choices[], int num)
{
	char input_str[3];
    int i, j;

	//genera la stringa delle possibilità
	char *possib = malloc(2*num*sizeof(char));

    j=0;
	for(i=0; i<num; i++) {
		possib[j++] = choices[i];
		possib[j++] = '/';
	}
	possib[j-1] = '\0'; //per eliminare l'ultima '/'

	//chiede la risposta
	while(1) {
		//mostra la domanda
		printf("%s [%s]: ", question, possib);
        fflush(stdout);

		fgets(input_str, 3, stdin);
		char c = input_str[0];

		//controlla se è un carattere valido
		for(i=0; i<num; i++) {
			if (c == choices[i])
				return c;

		}

	}

}

//funzione ausiliaria che serve a far sì che non rimangano eventuali residui nello standard input
void clear_stdin() {

    int c;
    while ((c=getchar()) != '\n' && c != EOF) {}

}

//funzione ausiliaria che controlla se la stringa source è più lunga di source_size byte: solo in quel caso invoca clear_stdin().
void clear_stdin_after_fgets(char *source, int source_size) {

    int i;

    for (i=0; i<source_size; i++) {
        if (source[i] == '\n')
            return;

    }
    //caso in cui sono rimasti dei residui nello standard input
    clear_stdin();

}

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
    //caso in cui sono rimasti dei residui nello standard input
    if (getchar() != '\n')
        clear_stdin();    

    //sanity check
    if (size > INT_MAX) {
        printf("Sorry, the provided input is incorrect. Please try again.\nPress Enter to continue...\n");
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
    clear_stdin_after_fgets(source, source_size);

    while(1) {
        ret = syscall(PUT_SYSCALL, source, size);
        if (!(ret < 0 && errno == EBUSY))   //caso in cui non ci sono stati problemi di concorrenza
            break;
        //caso in cui ci sono stati problemi di concorrenza
        printf("[CONCURRENCY ISSUE] Trying to call put_data() again...\n");
        fflush(stdout);

    }
    if (ret < 0) {
        printf("[ERROR] A problem occurred during syscall execution. Maybe there is no free device block.\nPress Enter to continue...\n");
        fflush(stdout);
    }
    else {
        printf("INDEX OF WRITTEN BLOCK: %d\nPress Enter to continue...\n", ret);
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
    //caso in cui sono rimasti dei residui nello standard input
    if (getchar() != '\n')
        clear_stdin(); 

    //sanity check
    if (offset < 0) {
        printf("Sorry, the provided input is incorrect. Please try again.\nPress Enter to continue...\n");
        fflush(stdout);
        return;
    }

    printf("How many bytes would you like to read from the device block?\n");
    fflush(stdout);
    scanf("%lu", &size);
    //caso in cui sono rimasti dei residui nello standard input
    if (getchar() != '\n')
        clear_stdin(); 

    //sanity check
    if (size > INT_MAX) {
        printf("Sorry, the provided input is incorrect. Please try again.\nPress Enter to continue...\n");
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
        printf("[ERROR] A problem occurred during syscall execution. Maybe the selected device block is invalid or does not exist.\nPress Enter to continue...\n");
        fflush(stdout);
    }
    else {
        printf("READ DATA: %s\n", destination);
        printf("NUMBER OF READ BYTES: %d\nPress Enter to continue...\n", ret);
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
    //caso in cui sono rimasti dei residui nello standard input
    if (getchar() != '\n')
        clear_stdin(); 

    //sanity check
    if (offset < 0) {
        printf("Sorry, the provided input is incorrect. Please try again.\nPress Enter to continue...\n");
        fflush(stdout);
        return;
    }

    while(1) {
        ret = syscall(INVALIDATE_SYSCALL, offset);
        if (!(ret < 0 && errno == EBUSY))   //caso in cui non ci sono stati problemi di concorrenza
            break;
        //caso in cui ci sono stati problemi di concorrenza
        printf("[CONCURRENCY ISSUE] Trying to call invalidate_data() again...\n");
        fflush(stdout);

    }
    if (ret < 0) {
        printf("[ERROR] A problem occurred during syscall execution. Maybe the selected device block is already invalid or does not exist.\nPress Enter to continue...\n");
        fflush(stdout);
    }
    else {
        printf("Invalidation of device block %d was successful.\nPress Enter to continue...\n", offset);
        fflush(stdout);
    }

    return;    

}

int main(int argc, char **argv) {

    char options[] = {'1', '2', '3', '4'};
    char selected_command;

    while(1) {

        printf("\n\n*** Hi! What should I do for you? ***\n");
        printf("1) Write on a data block\n");
        printf("2) Read a data block\n");
        printf("3) Invalidate a data block\n");
        printf("4) Quit\n");
        fflush(stdout);

        selected_command = multichoice("Please select an option", options, sizeof(options)/sizeof(char));

        switch (selected_command) {
            case '1':
                put_operation();
                break;
            
            case '2':
                get_operation();
                break;

            case '3':
                invalidate_operation();
                break;

            case '4':
                printf("Bye!\n");
                fflush(stdout);
                return 0;

            default:
                printf("Sorry, the provided input is incorrect. Please try again.\n");
                fflush(stdout);
                break;

        }
        
        getchar();

    }

}
