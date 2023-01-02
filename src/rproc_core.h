#ifndef RPROC_CORE_H
#define RPROC_CORE_H

#include "swd_gpio/swd_gpio.h"

struct mem_seg {
    u32 start;
    u32 size;
};

struct mem_info {
    char name[16];
    u32 attr;
    u32 offset; // offset in mem_segs
    u32 len;
    u32 base;
    u32 program_size;
};

struct core_mem {
    struct mem_info sram;
    struct mem_info flash;
    struct mem_seg mem_segs[];
};

struct cm_info {
    u32 cm_size;
    struct core_mem *cm;
};

struct rproc_core {
    // set swd_gpio binding
    void (*gpio_bind)(struct swd_gpio *sg);

    // functions for core
    int (*core_init)(void);
    void (*setup_swd)(void);
    void (*reset)(void);
    void (*unhalt_core)(void);
    int  (*halt_core)(void);
    u32 (*test_alive)(void);

    // functions for flash
    void (*erase_flash_all)(void);
    void (*erase_flash_page)(u32, u32);
    ssize_t (*program_flash)(void *, u32, u32);

    // functions for ram
    ssize_t (*write_ram)(void*, u32, u32);
    ssize_t (*read_ram)(void*, u32, const u32);

    struct cm_info *ci;
};

#endif
