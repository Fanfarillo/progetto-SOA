#ifndef _DEVFUNCTIONS_H
#define _DEVFUNCTIONS_H

#define SYNC    //se viene attivata questa #define, le operazioni di put_data() vengono eseguite in modo sincrono, senza page-cache write back daemon.
//#define DEBUG   //se viene attivata questa #define, viene riportato un numero maggiore di messaggi nel log del kernel.
#define IMAGE_NAME "image"

extern struct auxiliary_info au_info;

struct sorted_node {
    int node_index;
    unsigned int write_counter;
    struct sorted_node *next;
};

#endif
