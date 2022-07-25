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

int main(int argc, char **argv)
{
    int i;
    int fd = -1;
    uint32_t buf_ori[BUFSIZ/8];
    uint32_t buf_result[BUFSIZ/8];

    srand((unsigned long)time(NULL));

    // Initialize test data
    for (i = 0 ; i < BUFSIZ/8 ; i++) {
        buf_ori[i] = (uint32_t)rand();
    }

    printf("bufsiz:%d\n", BUFSIZ/8);
    fd = open("/dev/swd", O_RDWR);
    if(fd < 0){
        printf("Err with open dev\n");
        return -1;
    }

    // Download to Flash
    if(write(fd, buf_ori, sizeof(buf_ori)) < 0) {
        printf("Failed to write flash\n");
    }
    else {
        printf("Write flash Success\n");
    }

    // Read and verify
    read(fd, buf_result, sizeof(buf_result));
    if (!memcmp(buf_ori, buf_result, sizeof(buf_ori))) {
        printf("Verify programmed data Success\n");
    } else {
        printf("Err, Verify programmed data failed\n");
    }

    ioctl(fd, SWDDEV_IOC_ERSFLSH);

    close(fd);

    return 0;
}
