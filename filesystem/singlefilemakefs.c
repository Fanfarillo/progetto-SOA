#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "singlefilefs.h"
#include "singlefilefs_init.h"

/*
	This singlefilemakefs will write the following information onto the disk
	- BLOCK 0, superblock;
	- BLOCK 1, inode of the unique file (the inode for root is volatile);
	- BLOCK 2, ..., datablocks of the unique file 
*/

int main(int argc, char *argv[])
{
	int fd;
	long unsigned int nbytes;
	ssize_t ret;
	struct onefilefs_sb_info sb;
	struct onefilefs_inode root_inode;
	struct onefilefs_inode file_inode;
	struct onefilefs_dir_record record;
	char *block_padding;

	//qui iniziano le variabili locali definite direttamente da me
	int i, block_index;	//per i cicli for
	int num_data_blocks;
	int num_data_blocks_to_write;
	struct data_block_metadata struct_metadata;
	unsigned char *char_metadata;

	//il programma prende come argomento il dispositivo di destinazione in cui verrà creato il file system.
	if (argc != 3) {
		printf("Usage: mkfs-singlefilefs <device> <num_data_blocks>\n");
		fflush(stdout);
		return -1;
	}

	//inizializzazione di num_data_blocks e di num_data_blocks_to_write
	num_data_blocks = atoi(argv[2]);	
	num_data_blocks_to_write = sizeof(file_body)/sizeof(file_body[0]); //funziona perché stiamo dividendo la dimensione di un array di puntatori per la dimensione di un puntatore.

	//sanity check sui valori di num_data_blocks e di num_data_blocks_to_write
	if (num_data_blocks <= 0) {
		printf("Invalid number of data blocks.\n");
		fflush(stdout);
		return -1;
	}
	if (num_data_blocks_to_write > num_data_blocks) {
		printf("Number of data blocks to write exceeds number of data blocks.\n");
		fflush(stdout);
		return -1;
	}

	//apertura del dispositivo in modalità lettura+scrittura
	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		fflush(stdout);
		return -1;
	}

	//pack the superblock
	sb.version = FS_VERSION;	//file system version
	sb.magic = MAGIC;
	sb.block_size = DEFAULT_BLOCK_SIZE;
	sb.total_data_blocks = num_data_blocks;
	sb.first_valid = 0;
	sb.last_valid = num_data_blocks_to_write - 1;
	//scrittura del superblocco (block 0) del file system, che comprende info come numero di versione, magic number e dimensione dei blocchi.
	ret = write(fd, (char *)&sb, SUPERBLOCK_STRUCT_SIZE);

	if (ret != SUPERBLOCK_STRUCT_SIZE) {
		printf("Bytes written [%d] are not equal to sb size.\n", (int)ret);
		fflush(stdout);
		close(fd);
		return ret;
	}
	printf("Super block written successfully\n");
	fflush(stdout);

	//padding for superblock
	nbytes = DEFAULT_BLOCK_SIZE - SUPERBLOCK_STRUCT_SIZE;
	block_padding = malloc(nbytes);
	ret = write(fd, block_padding, nbytes);	//padding per il superblocco

	if (ret != nbytes) {
		printf("The padding bytes are not written properly. Retry your mkfs\n");
		fflush(stdout);
		free(block_padding);
		close(fd);
		return -1;
	}
	printf("Padding in the superblock written sucessfully.\n");
	fflush(stdout);
	free(block_padding);

	//write file inode
	file_inode.mode = S_IFREG;
	file_inode.inode_no = SINGLEFILEFS_FILE_INODE_NUMBER;
	file_inode.file_size = num_data_blocks * DEFAULT_BLOCK_SIZE;
	printf("File size is %ld\n",file_inode.file_size);
	fflush(stdout);
	//scrittura dell'inode del file (block 1), che comprende info come il numero di inode, la dimensione del file e i permessi d'accesso.
	ret = write(fd, (char *)&file_inode, sizeof(file_inode));

	if (ret != sizeof(root_inode)) {
		printf("The file inode was not written properly.\n");
		fflush(stdout);
		close(fd);
		return -1;
	}
	printf("File inode written successfully.\n");
	fflush(stdout);
	
	//padding for block 1
	nbytes = DEFAULT_BLOCK_SIZE - sizeof(file_inode);
	block_padding = malloc(nbytes);
	ret = write(fd, block_padding, nbytes);	//padding per l'inode del file

	if (ret != nbytes) {
		printf("The padding bytes are not written properly. Retry your mkfs\n");
		fflush(stdout);
		free(block_padding);
		close(fd);
		return -1;
	}
	printf("Padding in the inode block written sucessfully.\n");
	fflush(stdout);

	//write file datablocks
	for(block_index=0; block_index<num_data_blocks; block_index++) {
		free(block_padding);	//deallocazione del vecchio block_padding

		//caso in cui ci sono effettivamente delle informazioni da riportare nel blocco block_index
		if (block_index < num_data_blocks_to_write) {
			//sanity check sulla dimensione dei dati effettivi da scrivere sul blocco block_index (non deve superare la dimensione della parte del blocco riservata al payload)
			if (strlen(file_body[block_index]) > DEFAULT_BLOCK_SIZE-METADATA_SIZE) {
				printf("Size of payload for datablock %d exceeds limit.\n", block_index);
				fflush(stdout);
				close(fd);
				return -1;
			}

			if (block_index == num_data_blocks_to_write-1)	//caso in cui il blocco successivo sarà inizialmente invalido
				struct_metadata.next_valid = -1;
			else
				struct_metadata.next_valid = block_index + 1;
			
			struct_metadata.prev_valid = block_index - 1;	//il blocco di indice 0 avrà prev_valid pari a -1; -1 significa "nessun blocco".
			struct_metadata.is_valid = 1;

			//conversione di struct_metadata in stringa (char_metadata)
			char_metadata = (unsigned char *)&struct_metadata;

			//scrittura dei metadati del blocco (1 solo byte per volta)
			for(i=0; i<sizeof(struct data_block_metadata); i++) {
				ret = write(fd, &char_metadata[i], 1);
				if (ret != 1) {
					printf("Writing file datablock has failed.\n");
					fflush(stdout);
					close(fd);
					return -1;		
				}

			}

			printf("Metadata of datablock %d written successfully.\n", block_index);
			fflush(stdout);
			
			//scrittura del payload (dati effettivi) del blocco
			ret = write(fd, file_body[block_index], strlen(file_body[block_index]));
			if (ret != strlen(file_body[block_index])) {
				printf("Writing file datablock has failed.\n");
				fflush(stdout);
				close(fd);
				return -1;	
			}
			printf("Payload of datablock %d written successfully.\n", block_index);
			fflush(stdout);

			//padding per il blocco
			nbytes = DEFAULT_BLOCK_SIZE - METADATA_SIZE - strlen(file_body[block_index]);
			block_padding = malloc(nbytes);
			ret = write(fd, block_padding, nbytes);	//padding per il blocco dati block_index
			if (ret != nbytes) {
				printf("The padding bytes are not written properly. Retry your mkfs\n");
				fflush(stdout);
				free(block_padding);
				close(fd);
				return -1;
			}
			printf("Padding in datablock %d written sucessfully.\n", block_index);
			fflush(stdout);

		}

		//caso in cui all'interno del blocco block_index va inserito esclusivamente padding
		else {

			struct_metadata.next_valid = -1;	//-1 significa "nessun blocco".
			struct_metadata.prev_valid = -1;	//-1 significa "nessun blocco".
			struct_metadata.is_valid = 0;

			//conversione di struct_metadata in stringa (char_metadata)
			char_metadata = (unsigned char *)&struct_metadata;

			//scrittura dei metadati del blocco (1 solo byte per volta)
			for(i=0; i<sizeof(struct data_block_metadata); i++) {
				ret = write(fd, &char_metadata[i], 1);
				if (ret != 1) {
					printf("Writing file datablock has failed.\n");
					fflush(stdout);
					close(fd);
					return -1;		
				}

			}

			printf("Metadata of datablock %d written successfully.\n", block_index);
			fflush(stdout);

			nbytes = DEFAULT_BLOCK_SIZE - METADATA_SIZE;
			block_padding = malloc(nbytes);
			ret = write(fd, block_padding, nbytes);	//padding per il blocco dati block_index
			if (ret != nbytes) {
				printf("The padding bytes are not written properly. Retry your mkfs\n");
				fflush(stdout);
				free(block_padding);
				close(fd);
				return -1;
			}
			printf("Padding in datablock %d written sucessfully.\n", block_index);
			fflush(stdout);

		}

	}

	free(block_padding);
	close(fd);	//chiusura del file descriptor (i.e. del dispositivo)
	return 0;

}
