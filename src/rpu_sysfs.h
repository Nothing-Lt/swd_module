#ifndef RPU_SYSFS_H
#define RPU_SYSFS_H

enum RPU_STATUS {
    RPU_STATUS_HALT = 0,
    RPU_STATUS_UNHALT = 1
};

int rpu_sysfs_init(void);

void rpu_sysfs_exit(void);

#endif