#ifndef _ONEFILEFSKER_H
#define _ONEFILEFSKER_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "singlefilefs.h"

//superblock (complete) definition
struct onefilefs_sb_info {
	struct onefilefs_sb_user_info user_sb;
	struct list_head rcu_head;
	struct mutex write_mutex;			//serve a sincronizzare gli scrittori tra loro (ma non coi lettori)
};

//data block (complete) definition
struct data_block_content {
	unsigned int metadata;
	char payload[DEFAULT_BLOCK_SIZE-METADATA_SIZE];
}

struct rcu_node {
	unsigned int write_counter : 31;	//serve a stabilire il corretto ordinamento delle scritture sui blocchi; è un valore che parte da 1; è un campo a 31 bit.
	unsigned int is_valid : 1;			//flag che indica se il blocco è valido o meno; è un campo a 1 bit.
	struct list_head lh;
};

#endif
