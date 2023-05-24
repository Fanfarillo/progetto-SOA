#ifndef _ONEFILEFSKER_H
#define _ONEFILEFSKER_H

#include <linux/list.h>
#include <linux/types.h>

//superblock (complete) definition
struct onefilefs_sb_info {
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
	//qui iniziano i campi definiti da me
	uint64_t total_data_blocks;
	unsigned int total_writes;	//contatore atomico globale del numero di scritture; corrisponde al numero d'ordine (write_counter) assegnato all'ultimo blocco scritto.
	struct list_head rcu_head;
};

struct rcu_node {
	unsigned int write_counter : 31;	//serve a stabilire il corretto ordinamento delle scritture sui blocchi; è un valore che parte da 1; è un campo a 31 bit.
	unsigned int is_valid : 1;			//flag che indica se il blocco è valido o meno; è un campo a 1 bit.
	struct list_head lh;
};

#endif
