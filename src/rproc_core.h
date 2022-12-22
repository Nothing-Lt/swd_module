#ifndef RPROC_CORE_H
#define RPROC_CORE_H

struct rproc_core {
    // functions for core
    int (*init)(void);
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
};

#define SWD_FLASH_BASE  0x08000000

#endif
