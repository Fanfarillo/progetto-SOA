#ifndef _ONEFILEFSKER_H
#define _ONEFILEFSKER_H

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "singlefilefs.h"

//superblock (complete) definition
struct onefilefs_sb_info {
	struct onefilefs_sb_user_info user_sb;
	struct list_head rcu_head;
};

//data block (complete) definition
struct data_block_content {
	unsigned int metadata;
	char payload[DEFAULT_BLOCK_SIZE-METADATA_SIZE];
};

struct rcu_node {
	unsigned int write_counter : 31;	//serve a stabilire il corretto ordinamento delle scritture sui blocchi; è un valore che parte da 1; è un campo a 31 bit.
	unsigned int is_valid : 1;			//flag che indica se il blocco è valido o meno; è un campo a 1 bit.
	struct list_head lh;
};

struct auxiliary_info {
	uint64_t is_mounted;
	atomic_t usages;					//tiene traccia del numero di thread che stanno correntemente eseguendo una funzione del modulo; se è > 0, lo smontaggio viene impedito.
	struct mutex write_mutex;			//serve a sincronizzare gli scrittori tra loro (ma non coi lettori).
	struct mutex off_mutex;				//serve a sincronizzare gli aggiornamenti del parametro *off della funzione dev_read().
};

#endif
