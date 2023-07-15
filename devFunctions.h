#ifndef _DEVFUNCTIONS_H
#define _DEVFUNCTIONS_H

#define SYNC    //se viene attivata questa #define, le operazioni di put_data() vengono eseguite in modo sincrono, senza page-cache write back daemon.

#define IMAGE_NAME "image"
#define YES 1
#define NO 0

extern struct auxiliary_info au_info;

#endif
