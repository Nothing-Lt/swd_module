#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/fs.h>

#include "swd_core.h"
#include "swd_gpio/swd_gpio.h"

#define RETRY       60000

enum SWD_AHB_REGS {
    // debug register (AHB address)
    SWD_DHCSR_REG   = 0xE000EDF0,   // Debug Halting Control and Status Register
    SWD_DEMCR_REG   = 0xE000EDFC,   // Debug Exception and Monitor Control Register
    SWD_AIRCR_REG   = 0xE000ED0C,   // The Application Interrupt and Reset Control Register
};

#define JTAG_TO_SWD 0xE79E

#define SWD_ORUNERRCLR_OFF  4
#define SWD_WDERRCLR_OFF    3
#define SWD_STKERRCLR_OFF   2
#define SWD_STKCMPCLR_OFF   1
#define SWD_DAPABORT_OFF    0
#define SWD_ORUNERRCLR_MSK  BIT(SWD_ORUNERRCLR_OFF)
#define SWD_WDERRCLR_MSK    BIT(SWD_WDERRCLR_OFF)
#define SWD_STKERRCLR_MSK   BIT(SWD_STKERRCLR_OFF)
#define SWD_STKCMPCLR_MSK   BIT(SWD_STKCMPCLR_OFF)
#define SWD_DABABORT_MSK    BIT(SWD_DAPABORT_OFF)

#define SWD_CSYSPWRUPREQ_OFF    30
#define SWD_CDBGPWRUPREQ_OFF    28
#define SWD_WDATAERR_OFF        7
#define SWD_STICKYERR_OFF       5
#define SWD_STICKYCMP_OFF       4
#define SWD_STICKYORUN_OFF      1
#define SWD_CSYSPWRUPREQ_MSK    BIT(SWD_CSYSPWRUPREQ_OFF)
#define SWD_CDBGPWRUPREQ_MSK    BIT(SWD_CDBGPWRUPREQ_OFF)
#define SWD_WDATAERR_MSK        BIT(SWD_WDATAERR_OFF)
#define SWD_STICKYERR_MSK       BIT(SWD_STICKYERR_OFF)
#define SWD_STICKYCMP_MSK       BIT(SWD_STICKYCMP_OFF)
#define SWD_STICKYORUN_MSK      BIT(SWD_STICKYORUN_OFF)

#define SWD_BANK_SIZE   0x1000
#define SWD_MEMAP_BANK_0 0x00000000

#define FLASH_MIR_BASE  0x40023C00
#define FLASH_ACR       FLASH_MIR_BASE
#define FLASH_KEYR      FLASH_MIR_BASE + 0x4
#define FLASH_OPTKEYR   FLASH_MIR_BASE + 0x8
#define FLASH_SR        FLASH_MIR_BASE + 0xC
#define FLASH_CR        FLASH_MIR_BASE + 0x10

#define FLASH_UNLOCK_MAGIC1 0x45670123
#define FLASH_UNLOCK_MAGIC2 0xCDEF89AB

#define FLASH_SR_BSY_OFF    16
#define FLASH_SR_BSY_MSK    BIT(FLASH_SR_BSY_OFF)

#define FLASH_CR_PG_OFF     0
#define FLASH_CR_SER_OFF    1
#define FLASH_CR_MER_OFF    2
#define FLASH_CR_SNB_OFF    3
#define FLASH_CR_PSIZE_OFF  8
#define FLASH_CR_STRT_OFF   16
#define FLASH_CR_LOCK_OFF   31
#define FLASH_CR_PG_MSK     BIT(FLASH_CR_PG_OFF)
#define FLASH_CR_SER_MSK    BIT(FLASH_CR_SER_OFF)
#define FLASH_CR_LOCK_MSK   BIT(FLASH_CR_LOCK_OFF)
#define FLASH_CR_MER_MSK    BIT(FLASH_CR_MER_OFF)
#define FLASH_CR_STRT_MSK   BIT(FLASH_CR_STRT_OFF)

struct core_mem stm32f411ceu6_cm = {
    .sram = {
        .name = "SRAM",
        .attr = 0,
        .offset = 0,
        .len = 128*1024,
        .base = 0x20000000,
        .program_size = 1024,
    },

    .flash = {
        .name = "flash",
        .attr = 1,
        .offset = 1,
        .len = 8,
        .base = 0x08000000,
        .program_size = 4096
    },

    .mem_segs = {
        // mem_segs for SRAM
        {.start = 0x000, .size = 1024},

        // mem_segs for flash
        {.start = 0000000, .size = 16*1024},
        {.start = 16*1024, .size = 16*1024},
        {.start = 32*1024, .size = 16*1024},
        {.start = 48*1024, .size = 16*1024},
        {.start = 64*1024, .size = 64*1024},
        {.start = 128*1024, .size = 128*1024},
        {.start = 256*1024, .size = 128*1024},
        {.start = 384*1024, .size = 128*1024},
        {.start = 0, .size = 0},
    }
};

struct cm_info stm32f411ceu6_ci = {
    .cm = &stm32f411ceu6_cm,
    .cm_size = sizeof(struct core_mem) + \
            8 * sizeof(struct mem_seg) + \
            sizeof(struct mem_seg),
};

static struct swd_gpio *stm32f411xx_sg;

static void stm32f411xx_reset(void)
{
    _swd_reset(stm32f411xx_sg);
}

static void stm32f411xx_setup_swd(void)
{
    _swd_jtag_to_swd(stm32f411xx_sg);
}

static int stm32f411xx_halt_core(void)
{
    u8 ack = 0;

    // set the CTRL.core_reset_ap = 1
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_CSW_REG & 0xC, 0x8, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // enable the auto increment
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_CSW_REG, 0x23000012, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // DHCSR.C_DEBUGEN = 1
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DHCSR_REG, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0xA05F0003, true);
    if (ack != SWD_OK)
        return -ENODEV;

      // DEMCR.VC_CORERESET = 1
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DEMCR_REG, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x1, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // reset the core
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_AIRCR_REG, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x05FA0004, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // CTRL1.core_reset_ap = 0
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_IDR_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_IDR_REG & 0xC, 0x0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // Select MEM BANK 0
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_MEMAP_BANK_0 & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    return 0;
}

static void stm32f411xx_unhalt_core(void)
{
    // DHCSR.C_DEBUGEN = 1
    _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DHCSR_REG, true);
    _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0xA05F0000, true);

    // reset the core
    _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_AIRCR_REG, true);
    _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    _swd_send(stm32f411xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x05FA0007, true);
}

static u32 stm32f411xx_test_alive(void)
{
    u32 data;

    _swd_read(stm32f411xx_sg, SWD_DP, SWD_READ, SWD_DP_IDCODE_REG, &data, false);

    return data;
}

static void stm32f411xx_gpio_bind(struct swd_gpio *sg)
{
    stm32f411xx_sg = sg;
}

static int stm32f411xx_core_init(void)
{
    u8 ack;
    u32 data;
    int retry = RETRY;

    _swd_jtag_to_swd(stm32f411xx_sg);

    // Read IDCODE to wakeup the device
    ack = _swd_read(stm32f411xx_sg, SWD_DP, SWD_READ, SWD_DP_IDCODE_REG, &data, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // Set CSYSPWRUPREQ and CDBGPWRUPREQ
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_CTRLSTAT_REG, SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // wait until the CSYSPWRUPREQ and CDBGPWRUPREQ are set
    do {
        ack = _swd_read(stm32f411xx_sg, SWD_DP, SWD_READ, SWD_DP_CTRLSTAT_REG, &data, true);
        if (ack != SWD_OK)
            return -ENODEV;

        if ((data & (SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK)) == (SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK))
            break;
    } while(retry--);

    // select the first AP bank
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, 0x0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // Select last AP bank
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_IDR_REG & 0xF0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_read(stm32f411xx_sg, SWD_AP, SWD_READ, SWD_AP_IDR_REG & 0xC, &data, true);
    if (ack != SWD_OK)
        return -ENODEV;

    ack = _swd_read(stm32f411xx_sg, SWD_DP, SWD_READ, SWD_DP_RDBUFF_REG, &data, true);
    if (ack != SWD_OK)
        return -ENODEV;

    // select the first AP bank
    ack = _swd_send(stm32f411xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, 0x0, true);
    if (ack != SWD_OK)
        return -ENODEV;

    return 0;
}

static int stm32f411xx_unlock_flash(void)
{
    u32 data;
    int retry = RETRY;

    _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
    if (!(data & FLASH_CR_LOCK_MSK))
        return 0;

    data = FLASH_UNLOCK_MAGIC1;
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_KEYR, sizeof(u32));
    data = FLASH_UNLOCK_MAGIC2;
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_KEYR, sizeof(u32));

    do {
        stm32f411xx_sg->_delay();
        _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

        if (!(data & FLASH_CR_LOCK_MSK))
            return 0;

    } while((retry--) && (data & FLASH_CR_LOCK_MSK));

    return -1;
}

static void stm32f411xx_lock_flash(void)
{
    u32 data = 0;

    _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_LOCK_MSK;
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
}

static void stm32f411xx_erase_flash_all(void)
{
    u32 data;
    int retry;

    if(stm32f411xx_unlock_flash())
        return;

    // Check if Flash is busy.
    _swd_ap_read(stm32f411xx_sg, &data, FLASH_SR, sizeof(u32));
    if (data & FLASH_SR_BSY_MSK)
        return;

    // set MER = 1
    _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_MER_MSK;
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

    // Set STRT = 1
    _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_STRT_MSK;
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

    retry = RETRY;
    do {
        stm32f411xx_sg->_delay();
        _swd_ap_read(stm32f411xx_sg, &data, FLASH_SR, sizeof(u32));
    }
    while((retry--) && (data & FLASH_SR_BSY_MSK));

    // Clear MER
    _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
    data &= (~FLASH_CR_MER_MSK);
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

    stm32f411xx_lock_flash();
}

static void stm32f411xx_erase_flash_sector(struct core_mem *cm, u32 offset, u32 len)
{
    u32 data;
    int retry;
    int memseg_idx;
    u32 sctr_nmb;
    u32 erase_offset;

    if(stm32f411xx_unlock_flash())
        return;

    // check if the flash is busy
    _swd_ap_read(stm32f411xx_sg, &data, FLASH_SR, sizeof(u32));
    if(data & FLASH_SR_BSY_MSK)
        return;

    // do sector erase
    erase_offset = offset;
    for (memseg_idx = cm->flash.offset;
        memseg_idx < cm->flash.offset + cm->flash.len;
        memseg_idx++) {
        if ((cm->mem_segs[memseg_idx].start <= erase_offset) && \
            erase_offset < (cm->mem_segs[memseg_idx].start + cm->mem_segs[memseg_idx].size)) {
            sctr_nmb = memseg_idx - cm->flash.offset;

            // set the sector erase and sector number
            _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
            data |= (FLASH_CR_SER_MSK | (sctr_nmb << FLASH_CR_SNB_OFF));
            _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

            // start the sector erase
            _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
            data |= FLASH_CR_STRT_MSK;
            _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

            // wait until the erase finished
            retry = RETRY;
            do {
                stm32f411xx_sg->_delay();
                _swd_ap_read(stm32f411xx_sg, &data, FLASH_SR, sizeof(u32));
            }while((retry--) && (data & FLASH_SR_BSY_MSK));

            // sector erase completed, go to the next sector
            erase_offset = cm->mem_segs[memseg_idx].start + \
                            cm->mem_segs[memseg_idx].size;

            // check if the demand size is finished
            if ((erase_offset - offset) >= len)
                break;
        }
    }

    // Restore the original value of FLASH_CR
    _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
    data &= ~(FLASH_CR_SER_MSK | (0xf << FLASH_CR_SNB_OFF));
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

    stm32f411xx_lock_flash();
}

static ssize_t stm32f411xx_program_flash(struct core_mem *cm, void *from, u32 offset, u32 len)
{
    int i;
    int retry;
    int err;
    u32 data;
    u32 cur_base;
    u32 *buf = (u32*)from;
    u32 len_to_read = len / sizeof(u32);

    // Unlock flash
    if(stm32f411xx_unlock_flash())
        return -1;

    // check if the flash is busy
    _swd_ap_read(stm32f411xx_sg, &data, FLASH_SR, sizeof(u32));
    if(data & FLASH_SR_BSY_MSK) {
        stm32f411xx_lock_flash();
        return -1;
    }

    // Set the programming bit and psize to be 32bit
    _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= (FLASH_CR_PG_MSK | (0x2 << FLASH_CR_PSIZE_OFF));
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

    // write data to flash
    _swd_ap_write(stm32f411xx_sg, buf, cm->flash.base + offset, len);

    retry = RETRY;
    do{
        stm32f411xx_sg->_delay();
        _swd_ap_read(stm32f411xx_sg, &data, FLASH_SR, sizeof(u32));
    }while(retry-- && (data & FLASH_SR_BSY_MSK));

    _swd_ap_read(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));
    data &= ~(FLASH_CR_PG_MSK | (0x2 << FLASH_CR_PSIZE_OFF));
    _swd_ap_write(stm32f411xx_sg, &data, FLASH_CR, sizeof(u32));

    // verify
    err = 0;
    cur_base = cm->flash.base + offset;
    for (i = 0 ; i < len_to_read ; i++) {
        _swd_ap_read(stm32f411xx_sg, &data, cur_base, sizeof(u32));
        if (data != buf[i])
            err += 4;

        cur_base += sizeof(u32);
    }

    if (err) {
        stm32f411xx_erase_flash_sector(cm, offset, len);
        stm32f411xx_lock_flash();
        return err;
    }

    stm32f411xx_lock_flash();

    return 0;
}

static ssize_t stm32f411xx_write_ram(struct core_mem *cm, void* from, u32 offset, u32 len)
{
    int i;
    int err;
    u32 data;
    u32 cur_base;
    u32 *buf = (u32*)from;
    u32 len_to_read = len / sizeof(u32);

    // write data to ram
    if (_swd_ap_write(stm32f411xx_sg, buf, cm->sram.base + offset, len) < 0)
        return -ENODEV;

    // verify
    err = 0;
    cur_base = cm->sram.base + offset;
    for (i = 0 ; i < len_to_read ; i++) {
        _swd_ap_read(stm32f411xx_sg, &data, cur_base, sizeof(u32));
        if (data != buf[i])
            err += 4;

        cur_base += sizeof(u32);
    }

    return err;
}

static ssize_t stm32f411xx_read(void *to, u32 base, const u32 len)
{
   return _swd_ap_read(stm32f411xx_sg, to, base, len > SWD_BANK_SIZE ? SWD_BANK_SIZE : len);
}

struct rproc_core stm32f411ceu6_rc = {
    .core_name = "stm32f411ceu6",
    .ci = &stm32f411ceu6_ci,
    .gpio_bind = stm32f411xx_gpio_bind,
    .core_init = stm32f411xx_core_init,
    .setup_swd = stm32f411xx_setup_swd,
    .core_reset = stm32f411xx_reset,
    .core_unhalt = stm32f411xx_unhalt_core,
    .core_halt = stm32f411xx_halt_core,
    .test_alive = stm32f411xx_test_alive,
    .erase_flash_all = stm32f411xx_erase_flash_all,
    .erase_flash_page = stm32f411xx_erase_flash_sector,
    .program_flash = stm32f411xx_program_flash,
    .write_ram = stm32f411xx_write_ram,
    .read_ram = stm32f411xx_read
};
