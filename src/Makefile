obj-m := swd_rpu.o
swd_rpu-objs := swd.o rpu_sysfs.o lib_swd.o

KERNEL_MAKEFILE_PLACE = ~/workspace/bbb/tibsp/kernelbuildscripts/KERNEL
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

all:
	make -C $(KERNEL_MAKEFILE_PLACE) M=$(PWD) ARCH=arm CROSS_COMPILE=${CC} modules

clean:
	make -C $(KERNEL_MAKEFILE_PLACE) M=$(PWD) CROSS_COMPILE=${CC} clean
