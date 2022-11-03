#ifndef SWD_H
#define SWD_H

#include <linux/cdev.h>

#define SWDDEV_NAME "swd" 

struct swd_device 
{
    struct cdev cdev;
    struct class *cls;
    struct device *dev;

    u32 ope_base;
};

#endif