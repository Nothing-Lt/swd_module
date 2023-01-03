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
    uint32_t offset;
    uint32_t buf_ori[BUFSIZ/8];
    uint32_t buf_result[BUFSIZ/8];
    struct swd_parameters params;
    void *meminfo_buf = NULL;
    struct user_core_mem *cm;
    uint32_t ram_page_size;

    meminfo_buf = malloc(4096);
    if (!meminfo_buf)
        return -1;

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

    params.arg[0] = (unsigned long)meminfo_buf;
    ioctl(fd, SWDDEV_IOC_MEMINFO_GET, &params);
    cm = (struct user_core_mem*)meminfo_buf;
    if (!cm->sram.attr)
        ram_page_size = cm->sram.program_size;//mem_segs[cm->sram.offset].size;
    else {
        // the non unified page size in sram is not supported now.
        printf("error in getting  the page size of memory\n");
        goto error;
    }

    printf("sram.start:%08x, size:%08x\n", cm->sram.base, 
            cm->mem_segs[cm->sram.offset].size);

    offset = 0;
    for (i=0 ; i < (BUFSIZ/8)/(ram_page_size/4) ; i++) {
        // Download to RAM
        params.arg[0] = (unsigned long)&(buf_ori[i*(ram_page_size/4)]);
        params.arg[1] = offset;
        params.arg[2] = ram_page_size;
        if (ioctl(fd, SWDDEV_IOC_DWNLDSRAM, &params)) {
            i--;
            continue;
        }

        printf("Programmed -%d/%d-\n", i+1, (BUFSIZ/8)/(ram_page_size/4));
        offset += ram_page_size;
    }

    // Set the read base to RAM
    lseek(fd, cm->sram.base, SEEK_SET);

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

error:
    close(fd);
    free(meminfo_buf);

    return 0;
}
