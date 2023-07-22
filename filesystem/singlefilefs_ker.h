#ifndef _ONEFILEFSKER_H
#define _ONEFILEFSKER_H

#include <linux/mutex.h>
#include <linux/srcu.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0)
#include <linux/atomic.h>
#else
#include <asm/atomic_32.h>
#endif

struct auxiliary_info {
	uint64_t is_mounted;
	atomic_t usages;			//tiene traccia del numero di thread che stanno correntemente eseguendo una funzione del modulo; se è > 0, lo smontaggio viene impedito.
	struct mutex write_mutex;	//serve a sincronizzare gli scrittori tra loro (ma non coi lettori).
	struct mutex off_mutex;		//serve a sincronizzare gli aggiornamenti del parametro *off della funzione dev_read().
	struct srcu_struct srcu;	//è una struttura a supporto delle API per la sleepable RCU.
};

#endif
