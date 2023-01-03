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
    int i,j;
    int fd = -1;
    uint32_t offset;
    uint32_t buf_size;
    uint32_t file_size;
    FILE *fp = NULL;
    void *buf;
    struct swd_parameters params;
    void *meminfo_buf = NULL;
    struct user_core_mem *cm;

    meminfo_buf = malloc(4096);
    if (!meminfo_buf)
        return -1;

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

    params.arg[0] = (unsigned long)meminfo_buf;
    ioctl(fd, SWDDEV_IOC_MEMINFO_GET, &params);
    cm = (struct user_core_mem*)meminfo_buf;

    fseek(fp, 0L, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    if (file_size % cm->flash.program_size)
        buf_size = cm->flash.program_size * ((file_size / cm->flash.program_size) + 1);
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
    if (cm->flash.attr) {
        offset = 0;
        for (i = 0 ; i < buf_size / cm->flash.program_size; i++) {
            // Download to Flash
            params.arg[0] = ((unsigned long)buf) + (i * cm->flash.program_size);
            params.arg[1] = offset;
            params.arg[2] = cm->flash.program_size;
            if (ioctl(fd, SWDDEV_IOC_DWNLDFLSH, &params)) {
                // find the corresponding mem_seg
                for (j=cm->flash.offset ; j < cm->flash.offset+cm->flash.len ; j++) {
                        printf("Have write error, sector:%08x, size:%08x\n",
                            cm->mem_segs[j].start, cm->mem_segs[j].size);
                    if ((cm->mem_segs[j].start <= offset) && \
                        (offset < cm->mem_segs[j].start + cm->mem_segs[j].size)) {
                        printf("Have write error, sector:%08x, size:%08x\n",
                            cm->mem_segs[j].start, cm->mem_segs[j].size);
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

            printf("Programmed -%d/%d-\n", i+1, buf_size/cm->flash.program_size);
            offset += cm->flash.program_size;
        }
    } else {
        offset = 0;
        for (i = 0 ; i < buf_size / (cm->flash.program_size / 4); i++) {
            // Download to Flash
            params.arg[0] = ((unsigned long)buf) + (i * cm->flash.program_size);
            params.arg[1] = offset;
            params.arg[2] = cm->flash.program_size;
            if (ioctl(fd, SWDDEV_IOC_DWNLDFLSH, &params)) {
                // find the corresponding mem_seg
                params.arg[0] = offset;
                params.arg[1] = cm->flash.program_size;
                ioctl(fd, SWDDEV_IOC_ERSFLSH_PG, &params);
                break;

                // adjust the i and reprogram that mem seg
                i--;
                continue;
            }

            printf("Programmed -%d/%d-\n", i+1, BUFSIZ/(cm->flash.program_size/4));
            offset += cm->flash.program_size;
        }
    }

    printf("Flash program finished\n");
    ioctl(fd, SWDDEV_IOC_UNHLTCORE);

    free(buf);
    free(meminfo_buf);
buf_alloc_fail:
    fclose(fp);

bin_open_fail:
    close(fd);

swd_open_fail:
    return 0;
}

