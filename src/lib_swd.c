#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>

#include "swd.h"
#include "lib_swd.h"
#include "rpu_sysfs.h"
#include "../include/swd_module.h"

#define SWD_RESET_LEN 50
#define SWD_DELAY   (1000000000UL / 2000)
#define RETRY       600
#define START       1
#define STOP        0
#define PARK        1

spinlock_t __lock;
int _swclk_pin = 66;
int _swdio_pin = 69;

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

void _swd_reset(void)
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

void _swd_jtag_to_swd(void)
{
    _swd_send_cmd(JTAG_TO_SWD & 0xFF);
    _swd_send_cmd((JTAG_TO_SWD >> 8) & 0xFF);
    _swd_reset();
}

static void inline __wait_handling(void);
static void inline __fault_err_handling(void);
static void _swd_fault_handling(u8 ack);

u8 _swd_send(u8 APnDP, u8 RnW, u8 A, u32 data, bool handle_err)
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

u8 _swd_read(u8 APnDP, u8 RnW, u8 A, u32 *data, bool handle_err)
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

ssize_t _swd_ap_read(void* to, u32 base, const ssize_t len)
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
        buf[i] = data;
    }

    _swd_read(SWD_DP, SWD_READ, SWD_DP_RDBUFF_REG, &data, true);
    buf[i] = data;
  
    return (++i) * 4;
}

ssize_t _swd_ap_write(void *from, u32 base, u32 len)
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
        ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, buf[i], true);
        if (ack != SWD_OK) {
            pr_err("%s: [%s] %d ack:%d\n", SWDDEV_NAME, __func__, __LINE__, ack);
            break;
        }
    }

    return (++i) * 4;
}

int _swd_halt_core(void)
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

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0xA05F0003, true);
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

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x05FA0004, true);
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

void _swd_unhalt_core(void)
{
    u8 ack;

    // DHCSR.C_DEBUGEN = 1
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd select failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_DHCSR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_tar failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0xA05F0000, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    // reset the core
    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_TAR_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_TAR_REG & 0xC, SWD_AIRCR_REG, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_send(SWD_DP, SWD_WRITE, SWD_DP_SELECT_REG, SWD_AP_DRW_REG & 0xF0, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }

    ack = _swd_send(SWD_AP, SWD_WRITE, SWD_AP_DRW_REG & 0xC, 0x05FA0007, true);
    if (ack != SWD_OK) {
        pr_err("%s: [%s] %d write swd ap_drw failed\n", SWDDEV_NAME, __func__, __LINE__);
    }
}

int _swd_init(void)
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

void _swd_erase_flash_all(void)
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
    int page_len;

    // 1. write FLASH_CR_PER to 1
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data |= FLASH_CR_PER_MSK;
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));
    
    // 2. write address to FAR
    if (len % RAM_PAGE_SIZE)
        page_len = (len / RAM_PAGE_SIZE) + 1;
    else 
        page_len = len / RAM_PAGE_SIZE;
    for (i = 0 ; i < page_len ; i++) {
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

        base += RAM_PAGE_SIZE;
    }

    // Restore the original value of FLASH_CR
    _swd_ap_read(&data, FLASH_CR, sizeof(u32));
    data &= (~FLASH_CR_PER_MSK);
    _swd_ap_write(&data, FLASH_CR, sizeof(u32));
}

int _swd_program_flash(void *from, u32 base, u32 len)
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
    err = 0;
    cur_base = base;
    for (i = 0 ; i < len_to_read ; i++) {
        _swd_ap_read(&data, cur_base, sizeof(u32));
        if (data != buf[i]) 
            err += 4;

        cur_base += sizeof(u32);
    }

    pr_info("%s: [%s] errors:%d\n", SWDDEV_NAME, __func__, err);

    if (err) {
        _swd_erase_flash_page(base, len);
        _swd_lock_flash();
        return err;
    }

    _swd_lock_flash();

    return 0;
}

int _swd_write_ram(void* from, u32 base, u32 len)
{
    int i;
    int err;
    u32 data;
    u32 cur_base;
    u32 *buf = (u32*)from;
    u32 len_to_read = len / sizeof(u32);

    // write data to flash
    _swd_ap_write(buf, base, len);
        
    // verify
    err = 0;
    cur_base = base;
    for (i = 0 ; i < len_to_read ; i++) {
        _swd_ap_read(&data, cur_base, sizeof(u32));
        if (data != buf[i]) 
            err += 4;

        cur_base += sizeof(u32);
    }

    return err;
}