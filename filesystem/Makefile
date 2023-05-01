obj-m += singlefilefs.o
singlefilefs-objs += singlefilefs_src.o file.o dir.o

override DATA_BLOCKS = 10
TOT_BLOCKS = $(shell expr $(DATA_BLOCKS) + 2)
override MOUNT_DIR = ./mount/

all:
	gcc singlefilemakefs.c -o singlefilemakefs
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm singlefilemakefs

del-files:
	rm -fv ./*.o
	rm -fv ./*.mod
	rm -fv ./*.mod.c
	rm -fv ./*.ko
	rm -fv ./*.symvers
	rm -fv ./*.order
	rm image .dir.o.cmd .file.o.cmd .Module.symvers.cmd .modules.order.cmd
	rm .singlefilefs_src.o.cmd .singlefilefs.ko.cmd .singlefilefs.mod.cmd
	rm .singlefilefs.mod.o.cmd .singlefilefs.o.cmd

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
