#ifndef _ONEFILEFS_H
#define _ONEFILEFS_H

#include <linux/fs.h>
#include <linux/types.h>

#define MOD_NAME "SINGLE FILE FS"		//nome del modulo

#define MAGIC 0x42424242				//magic number: è un identificatore univoco nel filesystem
#define DEFAULT_BLOCK_SIZE 4096			//dimensione di un blocco di memoria utilizzato dal filesystem
#define SB_BLOCK_NUMBER 0				//numero di blocco del superblock
#define DEFAULT_FILE_INODE_BLOCK 1		//numero di blocco dell'inode del file

#define FILENAME_MAXLEN 255				//lunghezza massima del nome di un file

#define SINGLEFILEFS_ROOT_INODE_NUMBER 10	//numero di inode utilizzato dalla root del filesystem
#define SINGLEFILEFS_FILE_INODE_NUMBER 1	//numero di inode utilizzato dai file del filesystem

#define SINGLEFILEFS_INODES_BLOCK_NUMBER 1	//numero di blocchi di inode utilizzati dal filesystem

#define UNIQUE_FILE_NAME "the-file"

//qui iniziano le define aggiunte da me
#define FS_VERSION 1
#define METADATA_SIZE 8	//numero di byte che compongono i metadati di ciascun blocco
#define SUPERBLOCK_STRUCT_SIZE 4*sizeof(uint64_t)+2*sizeof(int64_t)	//numero di byte occupati da struct onefilefs_sb_info

//inode definition
struct onefilefs_inode {
	mode_t mode;
	uint64_t inode_no;
	union {
		uint64_t file_size;
		uint64_t dir_children_count;
	};
};

//dir definition (how the dir datablock is organized)
struct onefilefs_dir_record {
	char filename[FILENAME_MAXLEN];
	uint64_t inode_no;
};

//superblock definition
struct onefilefs_sb_info {
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
	//qui iniziano i campi definiti da me
	uint64_t total_data_blocks;
	int64_t first_valid;	//primo blocco valido in ordine temporale
	int64_t last_valid;	//ultimo blocco valido in ordine temporale
};

//data block metadata definition
struct data_block_metadata {
	int next_valid : 31;	//indica il prossimo blocco reso valido in ordine temporale; serve a stabilire il corretto ordinamento delle scritture sui blocchi.
	int prev_valid : 31;	//indica il precedente blocco reso valido in ordine temporale; serve a stabilire il corretto ordinamento delle scritture sui blocchi.
	int is_valid : 2;		//flag che indica se il blocco è valido o meno.
} __attribute__((packed));

//data block complete definition
struct data_block_content {
	struct data_block_metadata metadata;
	char payload[DEFAULT_BLOCK_SIZE-METADATA_SIZE];
};

//file.c
extern const struct inode_operations onefilefs_inode_ops;
extern const struct file_operations fops;

//dir.c
extern const struct file_operations onefilefs_dir_operations;

#endif
