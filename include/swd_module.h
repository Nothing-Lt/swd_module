
#ifndef SWD_MODULE_H
#define SWD_MODULE_H

struct swd_parameters
{
    unsigned long arg[4];
    unsigned long ret;
};

#define SWDDEV_IOC_MAGIC    '6'
#define SWDDEV_IOC_RSTLN    _IO(SWDDEV_IOC_MAGIC, 0)            //  1. reset line
#define SWDDEV_IOC_RDDPREG  _IOR(SWDDEV_IOC_MAGIC, 1, struct swd_parameters)      //  2. read dp reg
#define SWDDEV_IOC_WRDPREG  _IOW(SWDDEV_IOC_MAGIC, 2, struct swd_parameters)      //  3. write dp reg
#define SWDDEV_IOC_HLTCORE  _IO(SWDDEV_IOC_MAGIC, 3)            //  4. halt core  //  3. write dp reg
#define SWDDEV_IOC_UNHLTCORE  _IO(SWDDEV_IOC_MAGIC, 4)            //  4. halt core
#define SWDDEV_IOC_TSTALIVE _IOR(SWDDEV_IOC_MAGIC, 5, struct swd_parameters)      //  5. test alive
#define SWDDEV_IOC_SETBASE  _IOW(SWDDEV_IOC_MAGIC, 6, struct swd_parameters)       //  6. set base
#define SWDDEV_IOC_DWNLDSRAM    _IOW(SWDDEV_IOC_MAGIC, 7, struct swd_parameters)  //  7. download to sram
#define SWDDEV_IOC_DWNLDFLSH    _IOW(SWDDEV_IOC_MAGIC, 8, struct swd_parameters)  //  8. download to flash
#define SWDDEV_IOC_ERSFLSH      _IO(SWDDEV_IOC_MAGIC, 9)        //  9. erase flash
#define SWDDEV_IOC_VRFY         _IOR(SWDDEV_IOC_MAGIC, 10, struct swd_parameters)  //  10. verify

#endif