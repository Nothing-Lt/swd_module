obj-m := swd.o
KERNEL_MAKEFILE_PLACE = /usr/src/linux-headers-$(shell uname -r)

all:
	make -C $(KERNEL_MAKEFILE_PLACE) M=$(PWD) modules

clean:
	make -C $(KERNEL_MAKEFILE_PLACE) M=$(PWD) clean
