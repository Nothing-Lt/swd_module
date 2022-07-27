#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/fs.h>

#include "../include/swd_module.h"

#define SWDDEV_NAME "swd" 

#define SWD_RESET_LEN 50
#define SWD_DELAY   (1000000000UL / 2000)
#define RETRY       600
#define START       1
#define STOP        0
#define PARK        1

static struct swd_dev
{
    struct cdev cdev;
    u32 ope_base;
} swd_dev;
static struct class *cls;
static struct device *dev;
static int swd_major = 0;

static spinlock_t __lock;
static int _swclk_pin = 66;
static int _swdio_pin = 69;

enum SWDIO_DIR {
    SWD_OUT = 0,
    SWD_IN
};

enum SWD_ACK {
    SWD_OK      = 1,
    SWD_WAIT    = 2,
    SWD_FAULT   = 4
};

enum SWD_RW {
    SWD_WRITE   = 0,
    SWD_READ    = 1
};

enum SWD_APNDP {
    SWD_DP  = 0,
    SWD_AP  = 1
};

enum SWD_REGS {
    // DP regs
    SWD_DP_IDCODE_REG   = 0x0,
    SWD_DP_ABORT_REG    = 0x0,
    SWD_DP_CTRLSTAT_REG = 0x4,
    SWD_DP_RESEND_REG   = 0x8,
    SWD_DP_SELECT_REG   = 0x8,
    SWD_DP_RDBUFF_REG   = 0xC,

    // AP regs       
    SWD_AP_CSW_REG  = 0x0,
    SWD_AP_TAR_REG  = 0x4,
    SWD_AP_DRW_REG  = 0xC,
    SWD_AP_IDR_REG  = 0xFC,

    // debug register (AHB address)
    SWD_DDFSR_REG   = 0xE000ED30,   // Debug Fault StatusRegister
    SWD_DHCSR_REG   = 0xE000EDF0,   // Debug Halting Control and Status Register
    SWD_DCRSR_REG   = 0xE000EDF4,   // Debug Core Register Selector Register
    SWD_DCRDR_REG   = 0xE000EDF8,   // Debug Core Register Data Register
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

#define SWD_FLASH_BASE  0x08000000
#define SWD_RAM_BASE    0x20000000
#define FLASH_PAGE_SIZE 0x400

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

#define SWCLK_SET(v) gpio_direction_output(_swclk_pin, v)
#define SWDIO_SET(v) gpio_direction_output(_swdio_pin, v)
#define SWDIO_SET_DIR(dir)  {if (dir) {gpio_direction_input(_swdio_pin);} \
                            else {gpio_direction_output(_swdio_pin, 1);}}
#define SWDIO_GET(v) gpio_get_value(_swdio_pin)

static inline void _delay(unsigned long long time_out)
{
    for (; time_out ; time_out--) {
    }
}

static inline void _swd_send_bit(char c)
{
    SWDIO_SET(c);
    SWCLK_SET(1);
    _delay(SWD_DELAY);
    SWCLK_SET(0);
    _delay(SWD_DELAY);
}

static inline int _swd_read_bit(void)
{
    int ret;

    ret = SWDIO_GET();
    SWCLK_SET(1);
    _delay(SWD_DELAY);
    SWCLK_SET(0);
    _delay(SWD_DELAY);

    return ret;
}

static inline void _swd_send_cmd(u8 cmd)
{
    int i;

    spin_lock_irq(&__lock);
    for (i = 0 ; i < 8 ; i++) {
        _swd_send_bit(cmd & 1);
        cmd = cmd >> 1;
    }
    spin_unlock_irq(&__lock);
}

static inline void _swd_send_data(u32 data)
{
    int i;
    u8 parity = 0;

    spin_lock_irq(&__lock);
    for (i = 0 ; i < 32 ; i++) {
        _swd_send_bit(data & 1);
        parity = parity ^ (data & 1);
        data = data >> 1;
    }
    _swd_send_bit(parity);
    spin_unlock_irq(&__lock);
}

static inline u32 _swd_read_data(void)
{
    int i;
    u32 data = 0;
    u8 parity = 0;

    spin_lock_irq(&__lock);
    for (i = 0 ; i < 32 ; i++) {
        data |= _swd_read_bit() << i;
    }
    parity = _swd_read_bit();
    spin_unlock_irq(&__lock);

    return data;
}

static inline void _swd_trn(void)
{
    SWCLK_SET(1);
    _delay(SWD_DELAY);
    SWCLK_SET(0);
    _delay(SWD_DELAY);
}

static inline void _swd_reset(void)
{
    int i;

    spin_lock_irq(&__lock);

    SWDIO_SET(1);
    for (i = 0 ; i < SWD_RESET_LEN ; i++) {
        SWCLK_SET(1);
        _delay(SWD_DELAY);
        SWCLK_SET(0);
        _delay(SWD_DELAY);
    }

    SWDIO_SET(0);
    for (i = 0 ; i < 2 ; i++) {
        SWCLK_SET(1);
        _delay(SWD_DELAY);
        SWCLK_SET(0);
        _delay(SWD_DELAY);
    }

    spin_unlock_irq(&__lock);
}

static void _swd_jtag_to_swd(void)
{
    _swd_send_cmd(JTAG_TO_SWD & 0xFF);
    _swd_send_cmd((JTAG_TO_SWD >> 8) & 0xFF);
    _swd_reset();
}

static void inline __wait_handling(void);
static void inline __fault_err_handling(void);
static void _swd_fault_handling(u8 ack);

static u8 _swd_send(u8 APnDP, u8 RnW, u8 A, u32 data, bool handle_err)
{
    u8 cmd = 0;
    u8 A_2 = 0;
    u8 A_3 = 0;
    u8 ack = 0;
    u8 parity = 0;
    int retry = RETRY;

    // 1. start
    // 7. Park
    cmd = (PARK << 7) | START;

    // 2. APnDP
    // 3. RnW    
    cmd = cmd | (APnDP << 1) | (RnW << 2);

    // 4. A[2:3]
    A_2 = ((A >> 2) & 1);
    A_3 = ((A >> 3) & 1);
    cmd = cmd | (A_2 << 3) | (A_3 << 4);

    // 5. Parity
    parity = APnDP ^ RnW ^ A_2 ^ A_3;
    cmd = cmd | (parity << 5);

    // 6. Stop
    do {
        _swd_send_cmd(cmd);

        _swd_trn();

        SWDIO_SET_DIR(SWD_IN);
        ack = _swd_read_bit();
        ack = (_swd_read_bit() << 1) | ack;
        ack = (_swd_read_bit() << 2) | ack;

        _swd_trn();
    } while((ack == SWD_WAIT) && retry--);

    if ((ack != SWD_OK) && handle_err) {
        _swd_fault_handling(ack);
        return ack;
    }

    SWDIO_SET_DIR(SWD_OUT);
    _swd_send_data(data);

    return ack;
}

static u8 _swd_read(u8 APnDP, u8 RnW, u8 A, u32 *data, bool handle_err)
{
    u8 cmd = 0;
    u8 A_2 = 0;
    u8 A_3 = 0;
    u8 ack = 0;
    u8 parity = 0;
    int retry = RETRY;

    // 1. start
    // 7. Park
    cmd = (PARK << 7) | START;

    // 2. APnDP
    // 3. RnW    
    cmd = cmd | (APnDP << 1) | (RnW << 2);

    // 4. A[2:3]
    A_2 = ((A >> 2) & 1);
    A_3 = ((A >> 3) & 1);
    cmd = cmd | (A_2 << 3) | (A_3 << 4);

    // 5. Parity
    parity = APnDP ^ RnW ^ A_2 ^ A_3;
    cmd = cmd | (parity << 5);

    // 6. Stop
    do {
        _swd_send_cmd(cmd);

        _swd_trn();

        SWDIO_SET_DIR(SWD_IN);

        ack = _swd_read_bit();
        ack = (_swd_read_bit() << 1) | ack;
        ack = (_swd_read_bit() << 2) | ack;

        if (ack == SWD_WAIT)
             _swd_trn();
    } while((ack == SWD_WAIT) && retry--);

    if ((ack != SWD_OK) && handle_err){
        _swd_fault_handling(ack);
        return ack;
    }

    *data = _swd_read_data();

    _swd_trn();

    SWDIO_SET_DIR(SWD_OUT);

    return ack;
}
static void inline __wait_handling(void)
{
    u32 abort_val = SWD_DABABORT_MSK;

    // set the DAPABORT in abort reg, abort data transmition
    _swd_send(SWD_DP, SWD_READ, SWD_DP_ABORT_REG, abort_val, false);
}

static void inline __fault_err_handling(void)
{
    u32 ctrlstat_val = 0;
    u32 abort_val = 0;

    // read the ctrlstat reg and set the abort reg
    _swd_read(SWD_DP, SWD_READ, SWD_DP_CTRLSTAT_REG, &ctrlstat_val, false);
    pr_err("%s: [%s] %d ctrlstat_val:%08x\n", SWDDEV_NAME, __func__, __LINE__, ctrlstat_val);

    if (ctrlstat_val & SWD_STICKYERR_MSK)
        abort_val |= SWD_STKERRCLR_MSK;

    if (ctrlstat_val & SWD_WDATAERR_MSK) 
        abort_val |= SWD_WDERRCLR_MSK;

    if (ctrlstat_val & SWD_STICKYORUN_MSK) 
        abort_val |= SWD_ORUNERRCLR_MSK;

    pr_info("%s: [%s] %d abort_val:%08x\n", SWDDEV_NAME, __func__, __LINE__, abort_val);
    _swd_send(SWD_DP, SWD_WRITE, SWD_DP_ABORT_REG, abort_val, false);

    // after clear the wdataerr bit, reset the line and read the idcode
    if (ctrlstat_val & SWD_WDATAERR_MSK) {     
        _swd_jtag_to_swd();
        _swd_read(SWD_DP, SWD_READ, SWD_DP_IDCODE_REG, &abort_val, false);
    }
}

static void _swd_fault_handling(u8 ack)
{
    switch (ack) {
    case SWD_WAIT:
        pr_err("%s: [%s] %d wait\n", SWDDEV_NAME, __func__, __LINE__);
        __wait_handling();
    break;
    case SWD_FAULT: // for fault 
    default: // for protocol error
        pr_err("%s: [%s] %d fault ack:%d\n", SWDDEV_NAME, __func__, __LINE__, ack);
        __fault_err_handling();
    }
}

static ssize_t _swd_ap_read(void* to, u32 base, const ssize_t len)
{
    int i;
    u8 ack = 0;
    u32 data = 0;
    u32 *buf = to;
    ssize_t len_to_read = len / 4;

    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select first AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return 0;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, base, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select first AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return 0;
    }

    ack = _swd_read(SWD_AP, SWD_READ, SWD_AP_DRW_REG & 0xC, &data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d ack:%d\n", SWDDEV_NAME, __func__, __LINE__, ack);
        return 0;
    }

    for (i = 0 ; i < len_to_read-1 ; i++) {
        ack = _swd_read(SWD_AP, SWD_READ, SWD_AP_DRW_REG & 0xC, &data, true);
        if (ack != SWD_OK) {
            pr_err("%s: [%s] %d ack:%d\n", SWDDEV_NAME, __func__, __LINE__, ack);
            break;
        }
        pr_err("%s: [%s] %d idx:%d value: %08x\n", SWDDEV_NAME, __func__, __LINE__, i, data);
        buf[i] = data;
    }

    _swd_read(SWD_DP, SWD_READ, SWD_DP_RDBUFF_REG, &data, true);
    buf[i] = data;
    pr_err("%s: [%s] %d value: %08x\n", SWDDEV_NAME, __func__, __LINE__, data);
    
    return (++i) * 4;
}

static ssize_t _swd_ap_write(void *from, u32 base, u32 len)
{
    int i;
    u8 ack = 0;
    u32 *buf = from;
    ssize_t len_to_write = len / sizeof(u32);

    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select first AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return 0;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, base, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select first AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return 0;
    }

    for (i = 0 ; i < len_to_write ; i++) {
        pr_err("%s: [%s] %d idx:%d value: %08x\n", SWDDEV_NAME, __func__, __LINE__, i, buf[i]);
        ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, buf[i], true);
        if (ack != SWD_OK) {
            pr_err("%s: [%s] %d ack:%d\n", SWDDEV_NAME, __func__, __LINE__, ack);
            break;
        }
    }

    pr_err("%s: [%s] %d write: %d\n", SWDDEV_NAME, __func__, __LINE__, i);
    
    return (++i) * 4;
}

static int _swd_halt_core(void)
{
    u8 ack = 0;

    // set the CTRL.core_reset_ap = 1
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_csw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_CSW_REG & 0xC, 0x8, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d read swd ap_csw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }
 
    // enable the auto increment
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_CSW_REG, 0x23000012, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_csw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    // DHCSR.C_DEBUGEN = 1
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DHCSR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_tar failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0xA05F0001, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

      // DEMCR.VC_CORERESET = 1
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DEMCR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_tar failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }
    
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x1, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    // reset the core
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_AIRCR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0xFA050004, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }
 
    // CTRL1.core_reset_ap = 0
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_IDR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_IDR_REG & 0xC, 0x0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }
 
    // Select MEM BANK 0
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_MEMAP_BANK_0 & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    return 0;
}

static int _swd_init(void)
{
    u8 ack;
    u32 data;
    int retry = RETRY;

    _swd_jtag_to_swd();
    
    // Read IDCODE to wakeup the device
    ack = _swd_read(SWD_DP, SWD_READ, SWD_DP_IDCODE_REG, &data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d read idcode fail\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }
    pr_info("%s: [%s] %d idcode:%08x\n", SWDDEV_NAME, __func__, __LINE__, data);

    // Set CSYSPWRUPREQ and CDBGPWRUPREQ
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_CTRLSTAT_REG, SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d wirte ctrlstat fail\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }
    pr_info("%s: [%s] %d\n", SWDDEV_NAME, __func__, __LINE__);

    // wait until the CSYSPWRUPREQ and CDBGPWRUPREQ are set
    do {
        ack = _swd_read(SWD_DP, SWD_READ, SWD_DP_CTRLSTAT_REG, &data, true);
        if (ack != SWD_OK) {
            pr_err("%s: [%s] %d read ctrlstat fail\n", SWDDEV_NAME, __func__, __LINE__);
            return -ENODEV;
        }
        if ((data & (SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK)) == (SWD_CSYSPWRUPREQ_MSK | SWD_CDBGPWRUPREQ_MSK))
            break;
    } while(retry--);
    pr_info("%s: [%s] %d ctrlstat:%08x\n", SWDDEV_NAME, __func__, __LINE__, data);

    // select the first AP bank
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, 0x0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select first AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    // Select last AP bank
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_IDR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select last AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    ack = _swd_read(SWD_AP, SWD_READ, SWD_AP_IDR_REG & 0xC, &data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select last AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }
    pr_info("%s: [%s] %d IDR:%08x\n", SWDDEV_NAME, __func__, __LINE__, data);

    ack = _swd_read(SWD_DP, SWD_READ, SWD_DP_RDBUFF_REG, &data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select last AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }
    pr_info("%s: [%s] %d IDR:%08x\n", SWDDEV_NAME, __func__, __LINE__, data);

    // select the first AP bank
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, 0x0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d select first AP bank fail\n", SWDDEV_NAME, __func__, __LINE__);
        return -ENODEV;
    }

    return 0;
}

static int _swd_unlock_flash(void)
{
    int ret = -1;
    int retry = RETRY;
    u32 data;

    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    if (!(data & FLASH_CR_LOCK_MSK))
        return 0;

    pr_info("%s: [%s] %d unlocking flash cur_vla:%08x\n", SWDDEV_NAME, __func__, __LINE__, data);

    data = FLASH_UNLOCK_MAGIC1;
    _swd_ap_write(&data, FLASH_KEYR, sizeof(u32));
    data = FLASH_UNLOCK_MAGIC2;
    _swd_ap_write(&data, FLASH_KEYR, sizeof(u32));

    do {
        _delay(SWD_DELAY);
        _swd_ap_read(&data, FLASH_CR, sizeof(u32));

        if (!(data & FLASH_CR_LOCK_MSK)) {
            ret = 0;
            break;
        }
    } while(retry--);

    return ret;
}

static void _swd_lock_flash(void)
{
    u32 data = 0;

    _swd_ap_write(&data, FLASH_CR, sizeof(u32));
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_LOCK_MSK;
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));
}

static void _swd_erase_flash_all(void)
{
    u32 data;

    if(_swd_unlock_flash()) {
        pr_err("%s [%s] Unable to unlock flash\n", SWDDEV_NAME, __func__);
        return;
    }

    // set MER = 1
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_MER_MSK;
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));

    // Set STRT = 1
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_STRT_MSK;
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));

    do {
        _delay(SWD_DELAY);
        _swd_ap_read(&data, FLASH_SR, sizeof(u32));
    }
    while(data & FLASH_SR_BSY_MSK);

    // Clear MER
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data &= (~FLASH_CR_MER_MSK);
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));

    _swd_lock_flash();
}

static void _swd_erase_flash_page(u32 base, u32 len)
{
    int i;
    u32 data;
    int retry;

    // 1. write FLASH_CR_PER to 1
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_PER_MSK;
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));
    
    // 2. write address to FAR
    for (i = 0 ; i < (len / FLASH_PAGE_SIZE) ; i++) {
        _swd_ap_write(&base, FLASH_AR, sizeof(u32));

        // 3, write FLASH_CR_STRT to 1
        _swd_ap_read(&data, FLASH_CR, sizeof(u32));
        data |= FLASH_CR_STRT_MSK;
        _swd_ap_write(&data, FLASH_CR, sizeof(u32));
    
        // 4. wait until FLASH_SR_BSY to 0    
        retry = RETRY;
        do {
            _delay(SWD_DELAY);
            _swd_ap_read(&data, FLASH_SR, sizeof(u32));
        }while((retry--) && (data & FLASH_SR_BSY_MSK));

        base += FLASH_PAGE_SIZE;
    }

    // Restore the original value of FLASH_CR
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data &= (~FLASH_CR_PER_MSK);
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));
}

static int _swd_program_flash(void *from, u32 base, u32 len)
{
    int i;
    int retry;
    u8 ack;
    u32 data;
    u32 cur_base;
    u32 old_csw;
    u32 *buf = (u32*)from;
    u32 len_to_read = len / sizeof(u32);

    // Unlock flash
    if(_swd_unlock_flash()) {
        pr_err("%s [%s] Unable to unlock flash\n", SWDDEV_NAME, __func__);
        return -1;
    }

    // erase the corresponding page
    _swd_erase_flash_page(base, len);

    // Set the programming bit
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_PG_MSK;
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));

    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_read(SWD_AP, SWD_READ, SWD_AP_CSW_REG & 0xC, &old_csw, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d read swd ap_csw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_read(SWD_DP, SWD_READ, SWD_DP_RDBUFF_REG, &old_csw, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d read csw fail\n", SWDDEV_NAME, __func__, __LINE__);
    }

    data = old_csw & (~0x37); // clear addrinc and size filed 
    data |= 0x21; // set the  addrinc to be 0b10, and size to be 0b0001

    // set new AP_CSW 
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_CSW_REG & 0xC, data, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_csw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    // write data to flash
    _swd_ap_write(buf, base, len);

    // restore the value in AP_CSW
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_CSW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_CSW_REG & 0xC, old_csw, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_csw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    retry = RETRY;
    do{
        _delay(SWD_DELAY);
        _swd_ap_read(&data, FLASH_SR, sizeof(u32));
    }while(retry-- && (data & FLASH_SR_BSY_MSK));

    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data &= (~FLASH_CR_PG_MSK);
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));

    // verify
    cur_base = base;
    for (i = 0 ; i < len_to_read ; i++) {
        _swd_ap_read(&data, cur_base, sizeof(u32));
        if (data != buf[i]) {
            pr_info("%s: [%s] buf:%08x data:%08x\n", SWDDEV_NAME, __func__, buf[i], data);
            break;
        }
        cur_base += sizeof(u32);
    }

    if (i < len_to_read) {
        _swd_erase_flash_page(base, len);
        _swd_lock_flash();
        return -1;
    }

    _swd_lock_flash();

    return 0;
}

static int swd_open(struct inode *inode, struct file* filp)
{
    int ret;

    pr_info("%s: [%s] %d open start\n", SWDDEV_NAME, __func__, __LINE__);

    ret = _swd_init();
    if (ret) {
        pr_err("%s: [%s] %d error with _swd_init\n", SWDDEV_NAME, __func__, __LINE__);
        return ret;
    }

    _swd_halt_core();

    swd_dev.ope_base = SWD_FLASH_BASE;
    filp->private_data = &swd_dev;

    pr_info("%s: [%s] %d open finished\n", SWDDEV_NAME, __func__, __LINE__);

    return 0;
}

static int swd_release(struct inode *inode, struct file* filp)
{
    pr_info("%s: [%s] %d release start\n", SWDDEV_NAME, __func__, __LINE__);

    pr_info("%s: [%s] %d release finished\n", SWDDEV_NAME, __func__, __LINE__);

    return 0;
}

// able to read from flash (start from 0x08000000)
static ssize_t swd_read(struct file *filp, char *user_buf, size_t len, loff_t *off)
{
    int ret;
    u32 base;
    char *buf;
    unsigned long len_to_cpy;
    unsigned long read_len;
    struct swd_dev *dev = (struct swd_dev*)(filp->private_data);

    pr_info("%s: [%s] %d read start\n", SWDDEV_NAME, __func__, __LINE__);

    buf = kmalloc(len, GFP_KERNEL);
    if (!buf) {
        pr_err("%s: [%s] %d NULL from kmalloc\n", SWDDEV_NAME, __func__, __LINE__);
        return 0;
    }

    len_to_cpy = 0;
    base = dev->ope_base;
    do {
        read_len = _swd_ap_read(buf + len_to_cpy, base, len > SWD_BANK_SIZE ? SWD_BANK_SIZE : len);
        len_to_cpy += read_len;
        base += read_len;
        len -= read_len;
    } while(len/4);

    ret = copy_to_user(user_buf, buf, len_to_cpy);
    if (ret)
        return ret;

    kfree(buf);

    pr_info("%s: [%s] %d read finished\n", SWDDEV_NAME, __func__, __LINE__);

    *off += len_to_cpy;
    return len_to_cpy;
}

//  1. reset line
//  2. read dp reg
//  3. write dp reg
//  4. halt core
//  5. test alive
//  6. set base
//  7. download to sram
//  8. download to flash
//  9. erase flash
//  10. verify
static long swd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long ret = 0;
    char *buf = NULL;
    struct swd_parameters params;
    struct swd_dev *dev = (struct swd_dev*)(filp->private_data);

    pr_info("%s: [%s] %d ioctl start\n", SWDDEV_NAME, __func__, __LINE__);

    switch(cmd) {
    case SWDDEV_IOC_RSTLN:
        _swd_jtag_to_swd();
        break;
    case SWDDEV_IOC_RDDPREG:
        if(copy_from_user(&params, (void*)arg, sizeof(struct swd_parameters)))
            return -EFAULT;
        _swd_read(SWD_DP, SWD_READ, params.arg[0], (u32*)&(params.ret), true);
        if(copy_to_user((void*)arg, &params, sizeof(struct swd_parameters)))
            return -EFAULT;
        break;
    case SWDDEV_IOC_WRDPREG:
        if(copy_from_user(&params, (void*)arg, sizeof(struct swd_parameters)))
            return -EFAULT;
        _swd_send(SWD_DP, SWD_WRITE, params.arg[0], params.arg[1], true);
        break;
    case SWDDEV_IOC_HLTCORE:
        _swd_jtag_to_swd();
        _swd_halt_core();
        break;
    case SWDDEV_IOC_TSTALIVE:
        _swd_jtag_to_swd();
        _swd_read(SWD_DP, SWD_READ, SWD_DP_IDCODE_REG, (u32*)&(params.ret), false);
        if(copy_to_user((void*)arg, &params, sizeof(struct swd_parameters)))
            return -EFAULT;
        break;
    case SWDDEV_IOC_SETBASE:
        if(copy_from_user(&params, (void*)arg, sizeof(struct swd_parameters)))
            return -EFAULT;
        dev->ope_base = (u32)params.arg[0];
        break;
    case SWDDEV_IOC_DWNLDSRAM:
        if(copy_from_user(&params, (void*)arg, sizeof(struct swd_parameters)))
            return -EFAULT;
        buf = kmalloc(params.arg[2], GFP_KERNEL);
        if (!buf)
            return -ENOMEM;
        if(copy_from_user(buf, (void*)(params.arg[0]), params.arg[2])){
            kfree(buf);
            return -EFAULT;
        }
        _swd_ap_write(buf, params.arg[1], params.arg[2]);
        kfree(buf);
        break;
    case SWDDEV_IOC_DWNLDFLSH:
        if(copy_from_user(&params, (void*)arg, sizeof(struct swd_parameters)))
            return -EFAULT;
        buf = kmalloc(params.arg[2], GFP_KERNEL);
        if (!buf)
            return -ENOMEM;
        if(copy_from_user(buf, (void*)(params.arg[0]), params.arg[2])){
            kfree(buf);
            return -EFAULT;
        }
        ret = _swd_program_flash(buf, params.arg[1], params.arg[2]);
        kfree(buf);
        break;
    case SWDDEV_IOC_ERSFLSH:
        _swd_erase_flash_all();
        break;
    case SWDDEV_IOC_VRFY:
        break;
    default:
        pr_err("%s [%s] %d unknown cmd %08x\n", SWDDEV_NAME, __func__, __LINE__, cmd);
    }

    return ret;
}

static struct file_operations fops = {
    .open       = swd_open,
    .release    = swd_release,
    .read       = swd_read,
    .unlocked_ioctl = swd_ioctl
};

static int __init swd_init(void)
{
    dev_t devid;
    int ret;

    pr_info("%s: [%s] %d probe start\n", SWDDEV_NAME, __func__, __LINE__);

    cdev_init(&swd_dev.cdev, &fops);
    swd_dev.cdev.owner = THIS_MODULE;
    
    ret = gpio_request(_swclk_pin, NULL);
    if (ret < 0)
        goto swclk_pin_request_fail;

    ret = gpio_request(_swdio_pin, NULL);
    if (ret < 0)
        goto swdio_pin_request_fail;

    if (swd_major) {
        ret = register_chrdev_region(MKDEV(swd_major, 0), 1, SWDDEV_NAME);
    } else {
        ret = alloc_chrdev_region(&devid, 0, 1, SWDDEV_NAME);
        swd_major = MAJOR(devid);
    }
    if (ret < 0)
        goto chrdev_region_fail;

    ret = cdev_add(&swd_dev.cdev, MKDEV(swd_major, 0), 1);
    if (ret != 0)
        goto cdev_add_fail;

    cls = class_create(THIS_MODULE, SWDDEV_NAME);
    if (!cls)
        goto class_create_fail;

    dev = device_create(cls, NULL, MKDEV(swd_major,0), NULL, SWDDEV_NAME);
    if (!dev)
        goto device_create_fail;

    spin_lock_init(&__lock);
    pr_info("%s: [%s] %d probe finished\n", SWDDEV_NAME, __func__, __LINE__);
    return 0;

device_create_fail:
    class_destroy(cls);

class_create_fail:
    cdev_del(&swd_dev.cdev);

cdev_add_fail:
    unregister_chrdev_region(MKDEV(swd_major, 0), 1);

chrdev_region_fail:
    gpio_free(_swdio_pin);

swdio_pin_request_fail:
    gpio_free(_swclk_pin);

swclk_pin_request_fail:
    return ret;
}

static void __exit swd_exit(void)
{
    pr_info("%s: [%s] %d exit start\n", SWDDEV_NAME, __func__, __LINE__);
    
    gpio_free(_swdio_pin);
    gpio_free(_swclk_pin);
    device_destroy(cls, MKDEV(swd_major, 0));
    class_destroy(cls);
    cdev_del(&swd_dev.cdev);
    unregister_chrdev_region(MKDEV(swd_major, 0), 1);

    pr_info("%s: [%s] %d exit finished\n", SWDDEV_NAME, __func__, __LINE__);
}

module_init(swd_init);
module_exit(swd_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LINAQIN");
MODULE_DESCRIPTION("A Simple Arm Serial Debug(host) port driver");
