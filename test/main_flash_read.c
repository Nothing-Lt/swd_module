#include <stdio.h>
#include <stdint.h>

#include "../include/swd_module.h"

#define FLASH_BASE 0x08000000

int main(int argc, char **argv)
{
    int i;
    FILE *f = NULL;
    uint32_t buf[BUFSIZ];

    printf("bufsiz:%d\n", BUFSIZ);
    f = fopen("/dev/swd", "rw");
    if(!f){
        printf("Err with open dev\n");
        return -1;
    }

    // dump flash
    printf("Data in flash\n");
    fseek(f, FLASH_BASE, SEEK_SET);
    fread(buf, BUFSIZ, sizeof(uint32_t), f);

    for (i=0;i<BUFSIZ;i++) {
        printf("%08x\n", buf[i]);
    }

    fclose(f);

    return 0;
}
