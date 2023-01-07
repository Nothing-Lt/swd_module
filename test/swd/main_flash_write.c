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

#include "../../include/swd_module.h"

int main(int argc, char **argv)
{
    int i,j;
    int fd = -1;
    uint32_t offset;
    uint32_t buf_ori[BUFSIZ];
    uint32_t buf_result[BUFSIZ];
    struct swd_parameters params;
    void *meminfo_buf = NULL;
    struct user_core_mem *cm;

    meminfo_buf = malloc(4096);
    if (!meminfo_buf)
        return -1;

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

    params.arg[0] = (unsigned long)meminfo_buf;
    ioctl(fd, SWDDEV_IOC_MEMINFO_GET, &params);
    cm = (struct user_core_mem*)meminfo_buf;

    printf("Erasing flash by page\n");
    params.arg[0] = 0;
    params.arg[1] = sizeof(buf_ori);
    ioctl(fd, SWDDEV_IOC_ERSFLSH_PG, &params);
    printf("Flash erased\n");

    if (cm->flash.attr) {
        offset = 0;
        for (i = 0 ; i < BUFSIZ / (cm->flash.program_size / 4); i++) {
            // Download to Flash
            params.arg[0] = (unsigned long)&(buf_ori[i*(cm->flash.program_size/4)]);
            params.arg[1] = offset;
            params.arg[2] = cm->flash.program_size;
            if (ioctl(fd, SWDDEV_IOC_DWNLDFLSH, &params)) {
                // find the corresponding mem_seg
                for (j=cm->flash.offset ; j < cm->flash.offset+cm->flash.len ; j++) {
                        printf("Have write error, sector:%08x, size:%08x\n",
                            cm->mem_segs[j].start, cm->mem_segs[j].size);
                    if ((cm->mem_segs[j].start <= offset) && \
                        (offset < cm->mem_segs[j].start + cm->mem_segs[j].size)) {
                        params.arg[0] = cm->mem_segs[j].start;
                        params.arg[1] = cm->mem_segs[j].size;
                        ioctl(fd, SWDDEV_IOC_ERSFLSH_PG, &params);
                        break;
                    }
                }
                // adjust the i and reprogram that mem seg
                i = i - ((offset - cm->mem_segs[j].start) / cm->flash.program_size) - 1;
                offset = cm->mem_segs[j].start;
                continue;
            }

            printf("Programmed -%d/%d-\n", i+1, BUFSIZ/(cm->flash.program_size/4));
            offset += cm->flash.program_size;
        }
    } else {
        offset = 0;
        for (i = 0 ; i < BUFSIZ / (cm->flash.program_size / 4); i++) {
            // Download to Flash
            params.arg[0] = (unsigned long)&(buf_ori[i*(cm->flash.program_size/4)]);
            params.arg[1] = offset;
            params.arg[2] = cm->flash.program_size;
            if (ioctl(fd, SWDDEV_IOC_DWNLDFLSH, &params)) {
                // find the corresponding mem_seg
                params.arg[0] = offset;
                params.arg[1] = cm->flash.program_size;
                ioctl(fd, SWDDEV_IOC_ERSFLSH_PG, &params);

                // adjust the i and reprogram that mem seg
                i--;
                continue;
            }

            printf("Programmed -%d/%d-\n", i+1, BUFSIZ/(cm->flash.program_size/4));
            offset += cm->flash.program_size;
        }
    }

    // Read and verify
    lseek(fd, cm->flash.base, SEEK_SET);
    read(fd, buf_result, sizeof(buf_result));
    if (!memcmp(buf_ori, buf_result, sizeof(buf_ori))) {
        printf("Verify programmed data Success\n");
    } else {
        printf("Err, Verify programmed data failed\n");
    }

    ioctl(fd, SWDDEV_IOC_ERSFLSH);

    free(meminfo_buf);
    close(fd);

    return 0;
}
