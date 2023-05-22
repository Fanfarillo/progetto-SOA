#ifndef _ONEFILEFS_H
#define _ONEFILEFS_H

#include <linux/fs.h>
#include <linux/types.h>

#define MOD_NAME "SINGLE FILE FS"		//nome del modulo

#define MAGIC 0x42424242				//magic number: è un identificatore univoco nel filesystem
#define DEFAULT_BLOCK_SIZE 4096			//dimensione di un blocco di memoria utilizzato dal filesystem
#define SB_BLOCK_NUMBER 0				//numero di blocco del superblock
#define DEFAULT_FILE_INODE_BLOCK 1

#define FILENAME_MAXLEN 255				//lunghezza massima del nome di un file

#define SINGLEFILEFS_ROOT_INODE_NUMBER 10	//numero di inode utilizzato dalla root del filesystem
#define SINGLEFILEFS_FILE_INODE_NUMBER 1	//numero di inode utilizzato dai file del filesystem

#define SINGLEFILEFS_INODES_BLOCK_NUMBER 1	//numero di blocchi di inode utilizzati dal filesystem

#define UNIQUE_FILE_NAME "the-file"

//qui iniziano le define aggiunte direttamente da me
#define METADATA_SIZE 4					//numero di byte che compongono i metadati di ciascun blocco

//inode definition
struct onefilefs_inode {
	mode_t mode;//not exploited
	uint64_t inode_no;
	uint64_t data_block_number;//not exploited

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
	uint64_t inodes_count;//not exploited
	uint64_t free_blocks;//not exploited
	//qui iniziano i campi definiti da me
	uint64_t total_data_blocks;
};

//qui iniziano le strutture aggiunte direttamente da me
struct mount_info {
	uint64_t is_mounted;
};

// file.c
extern const struct inode_operations onefilefs_inode_ops;
extern const struct file_operations fops;

// dir.c
extern const struct file_operations onefilefs_dir_operations;

#endif
