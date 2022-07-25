#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "../include/swd_module.h"

#define RAM_PAGE_SIZE   0x400

int main(int argc, char **argv)
{
    int i;
    int fd = -1;
    uint32_t base;
    uint32_t buf_ori[BUFSIZ/8];
    uint32_t buf_result[BUFSIZ/8];
    struct swd_parameters params;

    srand((unsigned long)time(NULL));

    // Initialize test data
    for (i = 0 ; i < BUFSIZ/8 ; i++) {
        buf_ori[i] = (uint32_t)rand();
    }

    fd = open("/dev/swd", O_RDWR);
    if(fd < 0){
        printf("Err with open dev\n");
        return -1;
    }

    base = 0x20000000;
    for (i=0 ; i < (BUFSIZ/8)/(RAM_PAGE_SIZE/4) ; i++) {
        // Download to RAM
        params.arg[0] = (unsigned long)&(buf_ori[i*(RAM_PAGE_SIZE/4)]);
        params.arg[1] = base;
        params.arg[2] = RAM_PAGE_SIZE;
        ioctl(fd, SWDDEV_IOC_DWNLDSRAM, &params);
        base += RAM_PAGE_SIZE;
    }


    // Set the read base to RAM
    params.arg[0] = 0x20000000;
    ioctl(fd, SWDDEV_IOC_SETBASE, &params);

    // Read and verify
    read(fd, buf_result, sizeof(buf_result));
    if (!memcmp(buf_ori, buf_result, sizeof(buf_ori))) {
        printf("Verify programmed data Success\n");
    } else {
        for (i = 0 ; i < BUFSIZ/8 ; i++ ) {
            if (buf_ori[i] != buf_result[i])
                printf("buf_ori[%d]:%08x -- buf_result[%d]:%08x\n", 
                    i, buf_ori[i], i, buf_result[i]);
        }
        printf("Err, Verify programmed data failed\n");
    }

    // Restore to the Flash's base address
    params.arg[0] = 0x08000000;
    ioctl(fd, SWDDEV_IOC_SETBASE, &params);

    close(fd);

    return 0;
}
