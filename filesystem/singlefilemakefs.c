#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "singlefilefs.h"

/*
	This makefs will write the following information onto the disk
	- BLOCK 0, superblock;
	- BLOCK 1, inode of the unique file (the inode for root is volatile);
	- BLOCK 2, ..., datablocks of the unique file 
*/

int main(int argc, char *argv[])
{
	int fd, nbytes;
	ssize_t ret;
	struct onefilefs_sb_info sb;
	struct onefilefs_inode root_inode;
	struct onefilefs_inode file_inode;
	struct onefilefs_dir_record record;
	char *block_padding;
	char *file_body = "Whatever content you would like.\n";//this is the default content of the unique file 

	//il programma prende come argomento il dispositivo di destinazione in cui verrà creato il file system.
	if (argc != 2) {
		printf("Usage: mkfs-singlefilefs <device>\n");
		return -1;
	}

	//apertura del dispositivo in modalità lettura+scrittura
	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}

	//pack the superblock
	sb.version = 1;//file system version
	sb.magic = MAGIC;
	sb.block_size = DEFAULT_BLOCK_SIZE;
	//scrittura del superblocco (block 0) del fils system, che comprende info come numero di versione, magic number e dimensione dei blocchi.
	ret = write(fd, (char *)&sb, sizeof(sb));

	if (ret != DEFAULT_BLOCK_SIZE) {
		printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
		close(fd);
		return ret;
	}

	printf("Super block written succesfully\n");

	//write file inode
	file_inode.mode = S_IFREG;
	file_inode.inode_no = SINGLEFILEFS_FILE_INODE_NUMBER;
	file_inode.file_size = strlen(file_body);
	printf("File size is %ld\n",file_inode.file_size);
	fflush(stdout);
	//scrittura dell'inode del file (block 1), che comprende info come il numero di inode, la dimensione del file e i permessi d'accesso.
	ret = write(fd, (char *)&file_inode, sizeof(file_inode));

	if (ret != sizeof(root_inode)) {
		printf("The file inode was not written properly.\n");
		close(fd);
		return -1;
	}

	printf("File inode written succesfully.\n");
	
	//padding for block 1
	nbytes = DEFAULT_BLOCK_SIZE - sizeof(file_inode);
	block_padding = malloc(nbytes);

	ret = write(fd, block_padding, nbytes);	//padding per l'inode del file

	if (ret != nbytes) {
		printf("The padding bytes are not written properly. Retry your mkfs\n");
		close(fd);
		return -1;
	}
	printf("Padding in the inode block written sucessfully.\n");

	//write file datablock
	nbytes = strlen(file_body);
	//scrittura del corpo del file (block 2+), dove file_body = "Whatever content you would like.\n"
	ret = write(fd, file_body, nbytes);
	if (ret != nbytes) {
		printf("Writing file datablock has failed.\n");
		close(fd);
		return -1;
	}
	printf("File datablock has been written succesfully.\n");

	close(fd);	//chiusura del file descriptor (i.e. del dispositivo)

	return 0;
}
