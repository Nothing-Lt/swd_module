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

#define FLASH_PAGE_SIZE 0x1000

int main(int argc, char **argv)
{
    int i;
    int fd = -1;
    uint32_t offset;
    uint32_t buf_size;
    uint32_t file_size;
    FILE *fp = NULL;
    void *buf;
    struct swd_parameters params;

    fd = open("/dev/swd", O_RDWR);
    if(fd < 0){
        printf("Err with open dev\n");
        goto swd_open_fail;
    }
    fp = fopen(argv[1], "rb");
    if (!fp) {
        printf("Err with open %s\n", argv[1]);
        goto bin_open_fail;
    }

    fseek(fp, 0L, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    if (file_size % FLASH_PAGE_SIZE)
        buf_size = FLASH_PAGE_SIZE * ((file_size / FLASH_PAGE_SIZE) + 1);
    else 
        buf_size = file_size;

    buf = malloc(buf_size);
    if (!buf) {
        printf("Err with allocating buffer\n");
        goto buf_alloc_fail;
    }

    fread(buf, sizeof(uint32_t), file_size/sizeof(uint32_t), fp);

    printf("Erasing flash by page\n");
    params.arg[0] = 0;
    params.arg[1] = buf_size;
    ioctl(fd, SWDDEV_IOC_ERSFLSH_PG, &params);

    printf("Pragramming flash\n");
    // write to flash
    offset = 0;
    for (i=0 ; i < buf_size/FLASH_PAGE_SIZE ; i++) {
        // Download to Flash
        params.arg[0] = ((unsigned long)buf) + (i * FLASH_PAGE_SIZE);
        params.arg[1] = offset;
        params.arg[2] = FLASH_PAGE_SIZE;
        if (ioctl(fd, SWDDEV_IOC_DWNLDFLSH, &params)) {
            i--;
            continue;
        }

        printf("Programmed -%d/%d-\n", i+1, buf_size/FLASH_PAGE_SIZE);
        offset += FLASH_PAGE_SIZE;
    }

    printf("Flash program finished\n");
    ioctl(fd, SWDDEV_IOC_UNHLTCORE);

buf_alloc_fail:
    fclose(fp);

bin_open_fail:
    close(fd);

swd_open_fail:
    return 0;
}

