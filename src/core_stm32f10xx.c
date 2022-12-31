#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/fs.h>

#include "rproc_core.h"
#include "swd_gpio/swd_gpio.h"

#define RETRY       600

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

#define FLASH_BASE  0x08000000
#define SWD_RAM_BASE    0x20000000

#define FLASH_PAGE_SIZE 0x1000
#define RAM_PAGE_SIZE 0x400

#define FLASH_MIR_BASE  0x40022000
#define FLASH_ACR       FLASH_MIR_BASE
#define FLASH_KEYR      FLASH_MIR_BASE + 0x4
#define FLASH_OPTKEYR   FLASH_MIR_BASE + 0x8
#define FLASH_SR        FLASH_MIR_BASE + 0xC
#define FLASH_CR        FLASH_MIR_BASE + 0x10
#define FLASH_AR        FLASH_MIR_BASE + 0x14

#define FLASH_UNLOCK_MAGIC1 0x45670123
#define FLASH_UNLOCK_MAGIC2 0xCDEF89AB

#define FLASH_SR_BSY_OFF    0
#define FLASH_SR_BSY_MSK    BIT(FLASH_SR_BSY_OFF)

#define FLASH_CR_PG_OFF     0
#define FLASH_CR_PER_OFF    1
#define FLASH_CR_MER_OFF    2
#define FLASH_CR_STRT_OFF   6
#define FLASH_CR_LOCK_OFF   7
#define FLASH_CR_PG_MSK     BIT(FLASH_CR_PG_OFF)
#define FLASH_CR_PER_MSK    BIT(FLASH_CR_PER_OFF)
#define FLASH_CR_LOCK_MSK   BIT(FLASH_CR_LOCK_OFF)
#define FLASH_CR_MER_MSK    BIT(FLASH_CR_MER_OFF)
#define FLASH_CR_STRT_MSK   BIT(FLASH_CR_STRT_OFF)

static struct swd_gpio *stm32f10xx_sg;

void stm32f10xx_reset(void)
{
    _swd_reset(stm32f10xx_sg);
}

void stm32f10xx_setup_swd(void)
{
    _swd_jtag_to_swd(stm32f10xx_sg);
}

static int stm32f10xx_halt_core(void)
{
    u8 ack = 0;

    // set the CTRL.core_reset_ap = 1
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_csw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_CSW_REG & 0xC, 0x8, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d read swd ap_csw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    // enable the auto increment
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_CSW_REG, 0x23000012, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_csw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    // DHCSR.C_DEBUGEN = 1
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DHCSR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_tar failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0xA05F0003, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

      // DEMCR.VC_CORERESET = 1
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DEMCR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_tar failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x1, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    // reset the core
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_AIRCR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x05FA0004, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    // CTRL1.core_reset_ap = 0
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_IDR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_IDR_REG & 0xC, 0x0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    // Select MEM BANK 0
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_MEMAP_BANK_0 & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    return 0;
}

static void stm32f10xx_unhalt_core(void)
{
    u8 ack;

    // DHCSR.C_DEBUGEN = 1
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DHCSR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_tar failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0xA05F0000, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
    }

    // reset the core
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_AIRCR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x05FA0007, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", __FILE__, __func__, __LINE__);
    }
}

u32 stm32f10xx_test_alive(void)
{
    u32 data;

    _swd_read(stm32f10xx_sg, SWD_DP, SWD_READ, SWD_DP_IDCODE_REG, &data, false);

    return data;
}

static void stm32f10xx_gpio_bind(struct swd_gpio *sg)
{
    stm32f10xx_sg = sg;
}

static int stm32f10xx_core_init(void)
{
    u8 ack;
    u32 data;
    int retry = RETRY;

    _swd_jtag_to_swd(stm32f10xx_sg);

    // Read IDCODE to wakeup the device
    ack = _swd_read(stm32f10xx_sg, SWD_DP, SWD_READ, SWD_DP_IDCODE_REG, &data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d read idcode fail\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }
    pr_info("%s: [%s] %d idcode:%08x\n", __FILE__, __func__, __LINE__, data);

    // Set CSYSPWRUPREQ and CDBGPWRUPREQ
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_CTRLSTAT_REG, SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d wirte ctrlstat fail\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }
    pr_info("%s: [%s] %d\n", __FILE__, __func__, __LINE__);

    // wait until the CSYSPWRUPREQ and CDBGPWRUPREQ are set
    do {
        ack = _swd_read(stm32f10xx_sg, SWD_DP, SWD_READ, SWD_DP_CTRLSTAT_REG, &data, true);
        if (ack != SWD_OK) {
            pr_err("%s: [%s] %d read ctrlstat fail\n", __FILE__, __func__, __LINE__);
            return -ENODEV;
        }
        if ((data & (SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK)) == (SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK))
            break;
    } while(retry--);
    pr_info("%s: [%s] %d ctrlstat:%08x\n", __FILE__, __func__, __LINE__, data);

    // select the first AP bank
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, 0x0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select first AP bank fail\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    // Select last AP bank
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_IDR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select last AP bank fail\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_read(stm32f10xx_sg, SWD_AP, SWD_READ, SWD_AP_IDR_REG & 0xC, &data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select last AP bank fail\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }
    pr_info("%s: [%s] %d IDR:%08x\n", __FILE__, __func__, __LINE__, data);

    ack = _swd_read(stm32f10xx_sg, SWD_DP, SWD_READ, SWD_DP_RDBUFF_REG, &data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select last AP bank fail\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }
    pr_info("%s: [%s] %d IDR:%08x\n", __FILE__, __func__, __LINE__, data);

    // select the first AP bank
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, 0x0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select first AP bank fail\n", __FILE__, __func__, __LINE__);
        return -ENODEV;
    }

    return 0;
}

static int stm32f10xx_unlock_flash(void)
{
    u32 data;
    int retry = RETRY;

    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    if (!(data & FLASH_CR_LOCK_MSK))
        return 0;

    pr_info("%s: [%s] %d unlocking flash cur_vla:%08x\n", __FILE__, __func__, __LINE__, data);

    data = FLASH_UNLOCK_MAGIC1;
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_KEYR, sizeof(u32));
    data = FLASH_UNLOCK_MAGIC2;
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_KEYR, sizeof(u32));

    do {
        stm32f10xx_sg->_delay();
        _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

        if (!(data & FLASH_CR_LOCK_MSK))
            return 0;

    } while((retry--) && (data & FLASH_CR_LOCK_MSK));

    return -1;
}

static void stm32f10xx_lock_flash(void)
{
    u32 data = 0;

    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_LOCK_MSK;
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
}

static void stm32f10xx_erase_flash_all(void)
{
    u32 data;
    int retry;

    if(stm32f10xx_unlock_flash()) {
        pr_err("%s [%s] Unable to unlock flash\n", __FILE__, __func__);
        return;
    }

    // set MER = 1
    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_MER_MSK;
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

    // Set STRT = 1
    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_STRT_MSK;
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

    retry = RETRY;
    do {
        stm32f10xx_sg->_delay();
        _swd_ap_read(stm32f10xx_sg, &data, FLASH_SR, sizeof(u32));
    }
    while((retry--) && (data & FLASH_SR_BSY_MSK));

    // Clear MER
    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    data &= (~FLASH_CR_MER_MSK);
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

    stm32f10xx_lock_flash();
}

static void stm32f10xx_erase_flash_page(u32 offset, u32 len)
{
    int i;
    u32 data;
    int retry;
    int page_len;
    u32 base = FLASH_BASE + offset;

    // Unlock flash
    if(stm32f10xx_unlock_flash()) {
        pr_err("%s [%s] Unable to unlock flash\n", __FILE__, __func__);
        return;
    }

    // 1. write FLASH_CR_PER to 1
    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_PER_MSK;
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

    // 2. write address to FAR
    if (len % RAM_PAGE_SIZE)
        page_len = (len / RAM_PAGE_SIZE) + 1;
    else
        page_len = len / RAM_PAGE_SIZE;
    for (i = 0 ; i < page_len ; i++) {
        _swd_ap_write(stm32f10xx_sg, &base, FLASH_AR, sizeof(u32));

        // 3, write FLASH_CR_STRT to 1
        _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
        data |= FLASH_CR_STRT_MSK;
        _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

        // 4. wait until FLASH_SR_BSY to 0
        retry = RETRY;
        do {
            stm32f10xx_sg->_delay();
            _swd_ap_read(stm32f10xx_sg, &data, FLASH_SR, sizeof(u32));
        }while((retry--) && (data & FLASH_SR_BSY_MSK));

        base += RAM_PAGE_SIZE;
    }

    // Restore the original value of FLASH_CR
    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    data &= (~FLASH_CR_PER_MSK);
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

    stm32f10xx_lock_flash();
}

static ssize_t stm32f10xx_program_flash(void *from, u32 offset, u32 len)
{
    int i;
    int retry;
    int err;
    u8 ack;
    u32 data;
    u32 cur_base;
    u32 old_csw;
    u32 *buf = (u32*)from;
    u32 len_to_read = len / sizeof(u32);

    // Unlock flash
    if(stm32f10xx_unlock_flash()) {
        pr_err("%s [%s] Unable to unlock flash\n", __FILE__, __func__);
        return -1;
    }

    // Set the programming bit
    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_PG_MSK;
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_read(stm32f10xx_sg, SWD_AP, SWD_READ, SWD_AP_CSW_REG & 0xC, &old_csw, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d read swd ap_csw failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_read(stm32f10xx_sg, SWD_DP, SWD_READ, SWD_DP_RDBUFF_REG, &old_csw, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d read csw fail\n", __FILE__, __func__, __LINE__);
    }

    data = old_csw & (~0x37); // clear addrinc and size filed
    data |= 0x21; // set the  addrinc to be 0b10, and size to be 0b0001

    // set new AP_CSW
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_CSW_REG & 0xC, data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_csw failed\n", __FILE__, __func__, __LINE__);
    }

    // write data to flash
    _swd_ap_write(stm32f10xx_sg, buf, FLASH_BASE + offset, len);

    // restore the value in AP_CSW
    ack = _swd_send(stm32f10xx_sg, SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", __FILE__, __func__, __LINE__);
    }

    ack = _swd_send(stm32f10xx_sg, SWD_AP, SWD_WRITE, SWD_AP_CSW_REG & 0xC, old_csw, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_csw failed\n", __FILE__, __func__, __LINE__);
    }

    retry = RETRY;
    do{
        stm32f10xx_sg->_delay();
        _swd_ap_read(stm32f10xx_sg, &data, FLASH_SR, sizeof(u32));
    }while(retry-- && (data & FLASH_SR_BSY_MSK));

    _swd_ap_read(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));
    data &= (~FLASH_CR_PG_MSK);
    _swd_ap_write(stm32f10xx_sg, &data, FLASH_CR, sizeof(u32));

    // verify
    err = 0;
    cur_base = FLASH_BASE + offset;
    for (i = 0 ; i < len_to_read ; i++) {
        _swd_ap_read(stm32f10xx_sg, &data, cur_base, sizeof(u32));
        if (data != buf[i])
            err += 4;

        cur_base += sizeof(u32);
    }

    pr_info("%s: [%s] errors:%d\n", __FILE__, __func__, err);

    if (err) {
        stm32f10xx_erase_flash_page(FLASH_BASE + offset, len);
        stm32f10xx_lock_flash();
        return err;
    }

    stm32f10xx_lock_flash();

    return 0;
}

static ssize_t stm32f10xx_write_ram(void* from, u32 offset, u32 len)
{
    int i;
    int err;
    u32 data;
    u32 cur_base;
    u32 *buf = (u32*)from;
    u32 len_to_read = len / sizeof(u32);

    // write data to ram
    if (_swd_ap_write(stm32f10xx_sg, buf, SWD_RAM_BASE + offset, len) < 0)
        return -ENODEV;

    // verify
    err = 0;
    cur_base = SWD_RAM_BASE + offset;
    for (i = 0 ; i < len_to_read ; i++) {
        _swd_ap_read(stm32f10xx_sg, &data, cur_base, sizeof(u32));
        if (data != buf[i])
            err += 4;

        cur_base += sizeof(u32);
    }

    return err;
}

ssize_t stm32f10xx_read(void *to, u32 base, const u32 len)
{
   return _swd_ap_read(stm32f10xx_sg, to, base, len > SWD_BANK_SIZE ? SWD_BANK_SIZE : len);
}

struct rproc_core stm32f10xx_rc = {
    .gpio_bind = stm32f10xx_gpio_bind,
    .core_init = stm32f10xx_core_init,
    .setup_swd = stm32f10xx_setup_swd,
    .reset = stm32f10xx_reset,
    .unhalt_core = stm32f10xx_unhalt_core,
    .halt_core = stm32f10xx_halt_core,
    .test_alive = stm32f10xx_test_alive,
    .erase_flash_all = stm32f10xx_erase_flash_all,
    .erase_flash_page = stm32f10xx_erase_flash_page,
    .program_flash = stm32f10xx_program_flash,
    .write_ram = stm32f10xx_write_ram,
    .read_ram = stm32f10xx_read,
};
