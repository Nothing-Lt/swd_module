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

#define BIN_FILE    "blink.bin"
#define FLASH_PAGE_SIZE 0x1000

int main(int argc, char **argv)
{
    int i;
    int fd = -1;
    int err = 0;
    uint32_t base;
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
    fp = fopen(BIN_FILE, "rb");
    if (!fp) {
        printf("Err with open %s\n", BIN_FILE);
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

    // write to flash
    base = 0x08000000;
    for (i=0 ; i < buf_size/FLASH_PAGE_SIZE ; i++) {
        // Download to Flash
        params.arg[0] = ((unsigned long)buf) + (i * FLASH_PAGE_SIZE);
        params.arg[1] = base;
        params.arg[2] = FLASH_PAGE_SIZE;
        err = ioctl(fd, SWDDEV_IOC_DWNLDFLSH, &params);
        if (err) {
            printf("Program flash failed with err: %d\n", err);
            goto flash_program_fail;
        }

        base += FLASH_PAGE_SIZE;
    }

    printf("Flash program finished\n");
    ioctl(fd, SWDDEV_IOC_UNHLTCORE);

flash_program_fail:
buf_alloc_fail:
    fclose(fp);

bin_open_fail:
    close(fd);

swd_open_fail:
    return 0;
}
