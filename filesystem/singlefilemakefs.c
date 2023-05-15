#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "singlefilefs.h"

//direttamente aggiunti da me
#include "singlefilemakefs.h"

char *file_body[] = {	//this is the default content of the unique file
	"Abbiamo lezione solo a sogene non mi va di spostarmi avanti e indietro anche se non mi piace andare a sogene\n",
	"Forse non ci siamo capiti nell'audio di ieri\n",
	"Intanto solo per te un saluto dal mitico\n",
	"però ecco... io sono l'opposto ma solo perché sono pazzo io. What's app è la cosa minore, alla fine non mi danno fastidio i messaggi sfusi (se sono meno di 6)\n",
	"Come mi sono persa questa cosaaaaaaaaa\n"
};

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

	//qui iniziano le variabili locali definite direttamente da me
	char *block_metadata;
	int num_data_blocks;
	int num_data_blocks_to_write;

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
	sb.version = 1;//file system version
	sb.magic = MAGIC;
	sb.block_size = DEFAULT_BLOCK_SIZE;
	//scrittura del superblocco (block 0) del fils system, che comprende info come numero di versione, magic number e dimensione dei blocchi.
	ret = write(fd, (char *)&sb, sizeof(sb));

	if (ret != DEFAULT_BLOCK_SIZE) {
		printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
		fflush(stdout);
		close(fd);
		return ret;
	}

	printf("Super block written succesfully\n");
	fflush(stdout);

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

	printf("File inode written succesfully.\n");
	fflush(stdout);
	
	//padding for block 1
	nbytes = DEFAULT_BLOCK_SIZE - sizeof(file_inode);
	block_padding = malloc(nbytes);

	ret = write(fd, block_padding, nbytes);	//padding per l'inode del file

	if (ret != nbytes) {
		printf("The padding bytes are not written properly. Retry your mkfs\n");
		fflush(stdout);
		close(fd);
		return -1;
	}
	printf("Padding in the inode block written sucessfully.\n");
	fflush(stdout);

	//write file datablocks
	for (int block_index=0; block_index<num_data_blocks; block_index++) {
		//PROVVISORIAMENTE questo pointer rappresenta i metadati dei vari blocchi. TODO: è da aggiustare secondo le nostre esigenze.
		const char *metadata = "META";

		//caso in cui ci sono effettivamente delle informazioni da riportare nel blocco block_index
		if (block_index < num_data_blocks_to_write) {
			//sanity check sulla dimensione dei dati effettivi da scrivere sul blocco block_index (non deve superare la dimensione della parte del blocco riservata al payload)
			if (strlen(file_body[block_index]) > DEFAULT_BLOCK_SIZE-METADATA_SIZE) {
				printf("Size of payload for datablock %d exceeds limit");
				fflush(stdout);
				close(fd);
				return -1;
			}

			//scrittura dei metadati del blocco
			nbytes = METADATA_SIZE;
			block_metadata = malloc(nbytes);
			strncpy(block_metadata, metadata, nbytes);
			ret = write(fd, block_metadata, nbytes);
			if (ret != METADATA_SIZE) {
				printf("Writing file datablock has failed.\n");
				fflush(stdout);
				close(fd);
				return -1;				
			}
			printf("Metadata of datablock %d written succesfully.\n", block_index);
			fflush(stdout);
			
			//scrittura del payload (dati effettivi) del blocco
			ret = write(fd, file_body[block_index], strlen(file_body[block_index]));
			if (ret != strlen(file_body[block_index])) {
				printf("Writing file datablock has failed.\n");
				fflush(stdout);
				close(fd);
				return -1;	
			}
			printf("Payload of datablock %d written succesfully.\n", block_index);
			fflush(stdout);

			//padding per il blocco
			nbytes = DEFAULT_BLOCK_SIZE - METADATA_SIZE - strlen(file_body[block_index]);
			block_padding = malloc(nbytes);
			ret = write(fd, block_padding, nbytes);	//padding per il blocco dati block_index
			if (ret != nbytes) {
				printf("The padding bytes are not written properly. Retry your mkfs\n");
				fflush(stdout);
				close(fd);
				return -1;
			}
			printf("Padding in datablock %d written sucessfully.\n", block_index);
			fflush(stdout);

		}

		//caso in cui all'interno del blocco block_index va inserito esclusivamente padding
		else {
			nbytes = DEFAULT_BLOCK_SIZE;
			block_padding = malloc(nbytes);
			ret = write(fd, block_padding, nbytes);	//padding per il blocco dati block_index
			if (ret != nbytes) {
				printf("The padding bytes are not written properly. Retry your mkfs\n");
				fflush(stdout);
				close(fd);
				return -1;
			}
			printf("Padding in datablock %d written sucessfully.\n", block_index);
			fflush(stdout);

		}

	}

	close(fd);	//chiusura del file descriptor (i.e. del dispositivo)
	return 0;

}
