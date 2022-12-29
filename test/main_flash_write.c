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

#define FLASH_BASE 0x08000000
#define FLASH_PAGE_SIZE 0x1000

int main(int argc, char **argv)
{
    int i;
    int fd = -1;
    uint32_t offset;
    uint32_t buf_ori[BUFSIZ];
    uint32_t buf_result[BUFSIZ];
    struct swd_parameters params;

    srand((unsigned long)time(NULL));

    // Initialize test data
    for (i = 0 ; i < BUFSIZ ; i++) {
        buf_ori[i] = (uint32_t)rand();
    }

    printf("bufsiz:%d\n", BUFSIZ);
    fd = open("/dev/swd", O_RDWR);
    if(fd < 0){
        printf("Err with open dev\n");
        return -1;
    }

    printf("Erasing flash by page\n");
    params.arg[0] = 0;
    params.arg[1] = sizeof(buf_ori);
    ioctl(fd, SWDDEV_IOC_ERSFLSH_PG, &params);

    offset = 0;
    for (i=0 ; i < BUFSIZ/(FLASH_PAGE_SIZE/4) ; i++) {
        // Download to Flash
        params.arg[0] = (unsigned long)&(buf_ori[i*(FLASH_PAGE_SIZE/4)]);
        params.arg[1] = offset;
        params.arg[2] = FLASH_PAGE_SIZE;
        if (ioctl(fd, SWDDEV_IOC_DWNLDFLSH, &params)) {
            i-=1;
            continue;
        }

        printf("Programmed -%d/%d-\n", i+1, BUFSIZ/(FLASH_PAGE_SIZE/4));
        offset += FLASH_PAGE_SIZE;
    }

    // Read and verify
    lseek(fd, FLASH_BASE, SEEK_SET);
    read(fd, buf_result, sizeof(buf_result));
    if (!memcmp(buf_ori, buf_result, sizeof(buf_ori))) {
        printf("Verify programmed data Success\n");
    } else {
        printf("Err, Verify programmed data failed\n");
    }

    // ioctl(fd, SWDDEV_IOC_ERSFLSH);

    close(fd);

    return 0;
}
