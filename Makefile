obj-m += singlefilefs.o
singlefilefs-objs += initAndExit.o filesystem/file.o filesystem/dir.o

override DATA_BLOCKS = 10
TOT_BLOCKS = $(shell expr $(DATA_BLOCKS) + 2)
override MOUNT_DIR = ./mount/

all:
	gcc filesystem/singlefilemakefs.c -o filesystem/singlefilemakefs
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm ./filesystem/singlefilemakefs

del-files:
	rm -fv ./*.o
	rm -fv ./*.mod
	rm -fv ./*.mod.c
	rm -fv ./*.ko
	rm -fv ./*.symvers
	rm -fv ./*.order
	rm -fv ./filesystem/*.o
	rm image
	rm ./filesystem/.dir.o.cmd ./filesystem/.file.o.cmd 
	rm .Module.symvers.cmd .modules.order.cmd
	rm .initAndExit.o.cmd .singlefilefs.ko.cmd .singlefilefs.mod.cmd
	rm .singlefilefs.mod.o.cmd .singlefilefs.o.cmd

create-fs:
	dd bs=4096 count=$(TOT_BLOCKS) if=/dev/zero of=image
	./filesystem/singlefilemakefs image $(DATA_BLOCKS)
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

# USAGE:
# make
# sudo make insmod
# sudo make create-fs
# sudo make mount-fs
#
# sudo make clean
# sudo make unmount-fs
# sudo make rmmod
# sudo make del-files
