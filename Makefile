obj-m += singlefilefs.o
singlefilefs-objs += initAndExit.o filesystem/file.o filesystem/dir.o lib/scth.o

A = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)
override DATA_BLOCKS = 10000
TOT_BLOCKS = $(shell expr $(DATA_BLOCKS) + 2)
override MOUNT_DIR = ./mount/

all:
	gcc filesystem/singlefilemakefs.c -o filesystem/singlefilemakefs
#	gcc filesystem/singlefilemakefs.c -o filesystem/singlefilemakefs -fsanitize=address -static-libasan -g
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	gcc user/user.c -o user/user.o
#	gcc user/user.c -o user/user.o -fsanitize=address -static-libasan -g
	gcc test/test.c -o test/test.o -lpthread
#	gcc test/test.c -o test/test.o -lpthread -fsanitize=address -static-libasan -g

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm ./filesystem/singlefilemakefs

del-image:
	rm image

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
	insmod singlefilefs.ko the_syscall_table=$(A)

rmmod:
	rmmod singlefilefs

# USAGE (NB - il modulo relativo alla discovery della system call table deve essere già montato):
# make
# sudo make insmod
# sudo make create-fs
# sudo make mount-fs
#
# make clean
# sudo make unmount-fs
# sudo make rmmod
# sudo make del-image
