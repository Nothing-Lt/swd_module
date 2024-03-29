#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../../include/swd_module.h"

int main(int argc, char **argv)
{
    int fd = -1;
    struct swd_parameters params;

    fd = open("/dev/swd", O_RDWR);
    if(fd < 0){
        printf("Err with open dev fd:%d\n", fd);
        return -1;
    }

    ioctl(fd, SWDDEV_IOC_TSTALIVE, &params);
    printf("vla:%lx\n", params.ret);

    close(fd);

    return 0;
}
