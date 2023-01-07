#ifndef SWD_H
#define SWD_H

#include <linux/cdev.h>

#include "rproc_core.h"

#define SWDDEV_NAME "swd" 

struct swd_device 
{
    struct cdev cdev;
    struct class *cls;
    struct device *dev;
    struct rproc_core *rc;
};

#endif