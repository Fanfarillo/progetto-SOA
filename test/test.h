#ifndef _TEST_H
#define _TEST_H

#define NTHREADS 16
#define THREAD_TYPES 4      //invocatori di: 1) put_data(), 2) get_data(), 3) invalidate_data(), 4) dev_read()
#define TEST_BLOCKS 10      //numero di blocchi su cui potenzialmente si va a lavorare durante l'esecuzione di test.c
#define SIZE_SOURCE_STR 64  //dimensione del buffer source da passare come parametro alla syscall put_data()
#define SIZE_FILENAME 32    //dimensione del buffer che ospita il nome del file temporaneo che contiene l'output di cat
#define SIZE_COMMAND_STR 64 //dimensione del buffer che ospita il comando cat

#define RDTSC(value)        \
    asm (                   \
        "xor %%rax, %%rax\n"\
        "mfence\n"          \
        "rdtsc\n"           \   //preleva il timer e carica i 32 bit meno significativi in eax e i 32 più significativi in edx.
        "mfence\n"          \
        : "=a" (value)      \   //dopo l'esecuzione dell'ASM inline, il contenuto di eax andrà nella variabile timestamp.
    )

#endif
