VAR_DATA_BLOCKS = 30
VAR_MOUNT_DIR = ./mount/

all:
	make -C filesystem

clean:
	make -C filesystem clean

create-fs:
	make -C filesystem DATA_BLOCKS=$(VAR_DATA_BLOCKS) create-fs

mount-fs:
	make -C filesystem MOUNT_DIR=$(VAR_MOUNT_DIR) mount-fs

unmount-fs:
	make -C filesystem MOUNT_DIR=$(VAR_MOUNT_DIR) unmount-fs

insmod:
	make -C filesystem insmod

rmmod:
	make -C filesystem rmmod
