obj-m := swd_rpu.o
swd_rpu-objs := swd.o rpu_sysfs.o lib_swd.o

KERNEL_MAKEFILE_PLACE = /usr/src/linux-headers-$(shell uname -r)

all:
	make -C $(KERNEL_MAKEFILE_PLACE) M=$(PWD) modules

clean:
	make -C $(KERNEL_MAKEFILE_PLACE) M=$(PWD) clean
