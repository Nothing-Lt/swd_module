#ifndef RPU_SYSFS_H
#define RPU_SYSFS_H

#include "swd_drv.h"

enum RPU_STATUS {
    RPU_STATUS_HALT = 0,
    RPU_STATUS_UNHALT = 1
};

int rpu_sysfs_init(struct swd_device *swd_dev);

void rpu_sysfs_exit(struct swd_device *swd_dev);

#endif