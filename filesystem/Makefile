obj-m += singlefilefs.o
singlefilefs-objs += singlefilefs.o file.o dir.o

override DATA_BLOCKS = 10
TOT_BLOCKS = $(shell expr $(DATA_BLOCKS) + 2)
override MOUNT_DIR = ./mount/

all:
	gcc singlefilemakefs.c -o singlefilemakefs
	gcc -I /usr/src/linux-headers-$(uname -r) singlefilefs_src.c -o singlefilefs_src
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm singlefilemakefs

create-fs:
# Necessario aggiungere il parametro $(DATA_BLOCKS) a ./singlefilemakefs image
	dd bs=4096 count=$(TOT_BLOCKS) if=/dev/zero of=image
	./singlefilemakefs image
	mkdir ./mount
	
mount-fs:
	mount -o loop -t singlefilefs image $(MOUNT_DIR)

unmount-fs:
	umount $(MOUNT_DIR)
	rmdir $(MOUNT_DIR)

insmod:
	insmod singlefilefs.ko

rmmod:
	rmmod singlefilefs
