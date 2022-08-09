#ifndef LIB_SWD_H
#define LIB_SWD_H

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

void _swd_reset(void);

void _swd_jtag_to_swd(void);

int _swd_init(void);

int _swd_halt_core(void);

void _swd_unhalt_core(void);

u8 _swd_send(u8 APnDP, u8 RnW, u8 A, u32 data, bool handle_err);

u8 _swd_read(u8 APnDP, u8 RnW, u8 A, u32 *data, bool handle_err);

ssize_t _swd_ap_read(void* to, u32 base, const ssize_t len);

ssize_t _swd_ap_write(void *from, u32 base, u32 len);

void _swd_erase_flash_all(void);

int _swd_program_flash(void *from, u32 base, u32 len);

#endif