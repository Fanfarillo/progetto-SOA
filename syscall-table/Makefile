obj-m += the_usctm.o
the_usctm-objs += lib/vtpmo.o

all:
	gcc user/user.c -o user/user.o
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

del-files:
	rm -fv ./*.o
	rm -fv ./*.mod
	rm -fv ./*.mod.c
	rm -fv ./*.ko
	rm -fv ./*.symvers
	rm -fv ./*.order
	rm -fv ./lib/*.o
	rm -fv ./user/*.o
	rm .Module.symvers.cmd .modules.order.cmd
	rm .the_usctm.ko.cmd .the_usctm.mod.cmd .the_usctm.mod.o.cmd .the_usctm.o.cmd

insmod:
	insmod the_usctm.ko 

rmmod:
	rmmod the_usctm

# USAGE:
# make
# sudo make insmod
#
# sudo make clean
# sudo make rmmod
# sudo make del-files
