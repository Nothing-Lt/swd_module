
#ifndef SWD_MODULE_H
#define SWD_MODULE_H

struct swd_parameters
{
    unsigned long arg[4];
    unsigned long ret;
};

struct user_mem_seg {
    uint32_t start;
    uint32_t size;
};

struct user_mem_info {
    char name[16];
    uint32_t attr;
    uint32_t offset; // offset in mem_segs
    uint32_t len;
    uint32_t base;
    uint32_t program_size;
};

struct user_core_mem {
    struct user_mem_info sram;
    struct user_mem_info flash;
    struct user_mem_seg mem_segs[];
};

#define SWDDEV_IOC_MAGIC    '6'
#define SWDDEV_IOC_RSTLN    _IO(SWDDEV_IOC_MAGIC, 0)            //  0. reset line
#define SWDDEV_IOC_HLTCORE  _IO(SWDDEV_IOC_MAGIC, 1)            //  1. halt core
#define SWDDEV_IOC_UNHLTCORE  _IO(SWDDEV_IOC_MAGIC, 2)            //  2. unhalt core
#define SWDDEV_IOC_TSTALIVE _IOR(SWDDEV_IOC_MAGIC, 3, struct swd_parameters)      //  2. test alive
#define SWDDEV_IOC_DWNLDSRAM    _IOW(SWDDEV_IOC_MAGIC, 4, struct swd_parameters)  //  4. download to sram
#define SWDDEV_IOC_DWNLDFLSH    _IOW(SWDDEV_IOC_MAGIC, 5, struct swd_parameters)  //  5. download to flash
#define SWDDEV_IOC_ERSFLSH      _IO(SWDDEV_IOC_MAGIC, 6)        //  6. erase flash
#define SWDDEV_IOC_ERSFLSH_PG    _IOW(SWDDEV_IOC_MAGIC, 7, struct swd_parameters)  //  7. erase flash by page
#define SWDDEV_IOC_MEMINFO_GET  _IOR(SWDDEV_IOC_MAGIC, 8, struct swd_parameters)  //  8. verify

#endif