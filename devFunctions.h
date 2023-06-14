#ifndef _DEVFUNCTIONS_H
#define _DEVFUNCTIONS_H

#define SYNC    //se (e solo se) viene attivata questa #define, le operazioni di put_data() vengono eseguite in maniera sincrona, senza page-cache write back daemon.
#define IMAGE_NAME "image"

extern struct auxiliary_info au_info;

#endif
