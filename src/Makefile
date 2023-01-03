obj-m := swd.o
swd-objs := swd_drv.o swd_gpio/swd_gpio.o core_stm32f10xx.o core_stm32f411xx.o

KERNEL_MAKEFILE_PLACE = /usr/src/linux-headers-$(shell uname -r)

all:
	make -C $(KERNEL_MAKEFILE_PLACE) M=$(PWD) modules
	dtc -@ -I dts -o dtbo -o swd-device-overlay.dtbo swd-device-overlay.dts

clean:
	make -C $(KERNEL_MAKEFILE_PLACE) M=$(PWD) clean
