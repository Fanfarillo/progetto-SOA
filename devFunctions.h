#ifndef _DEVFUNCTIONS_H
#define _DEVFUNCTIONS_H

#define SYNC    //se (e solo se) viene attivata questa #define, le operazioni di put_data() vengono eseguite in maniera sincrona, senza page-cache write back daemon.
#define IMAGE_NAME "image"

extern struct auxiliary_info au_info;

struct sorted_node {
    int node_offset;
    unsigned int write_counter;
    struct sorted_node *next;
};

#endif
