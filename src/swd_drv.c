#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/fs.h>

#include "rproc_core.h"
#include "swd_gpio/swd_gpio.h"
#include "../include/swd_module.h"

#define SWDDEV_NAME "swd"

#define SWD_DELAY   (1000000000UL / 2000)

static struct swd_dev
{
    struct cdev cdev;
    struct rproc_core *rc;
} swd_dev;
static struct class *cls;
static struct device *dev;
static int swd_major = 0;

static spinlock_t __lock;
static atomic_t open_lock = ATOMIC_INIT(1);
static int _swclk_pin = 27;
static int _swdio_pin = 17;

extern struct rproc_core stm32f10xx_rc;
extern struct rproc_core stm32f411xx_rc;

static inline void _delay(unsigned long long time_out)
{
    for (; time_out ; time_out--) {
    }
}

static inline void delay (void) {
    _delay(SWD_DELAY);
}

static inline void SWCLK_SET (int v) {
    gpio_direction_output(_swclk_pin, v);
}

static inline void SWDIO_SET (int v) {
    gpio_direction_output(_swdio_pin, v);
}

static inline void SWDIO_DIR_IN (void) {
    gpio_direction_input(_swdio_pin);
}

static inline void SWDIO_DIR_OUT (void) {
    gpio_direction_output(_swdio_pin, 1);
}

static inline int SWDIO_GET (void) {
    return gpio_get_value(_swdio_pin);
}

static inline void signal_begin (void) {
    spin_lock_irq(&__lock);
}

static inline void signal_end (void) {
    spin_unlock_irq(&__lock);
}

// setup the gpio level things
struct swd_gpio stm32f10xx_sg = {
    .signal_begin = signal_begin,
    .signal_end = signal_end,
    .SWCLK_SET = SWCLK_SET,
    .SWDIO_DIR_IN = SWDIO_DIR_IN,
    .SWDIO_DIR_OUT = SWDIO_DIR_OUT,
    .SWDIO_SET = SWDIO_SET,
    .SWDIO_GET = SWDIO_GET,
    ._delay = delay
};

// setup the gpio level things
struct swd_gpio stm32f411xx_sg = {
    .signal_begin = signal_begin,
    .signal_end = signal_end,
    .SWCLK_SET = SWCLK_SET,
    .SWDIO_DIR_IN = SWDIO_DIR_IN,
    .SWDIO_DIR_OUT = SWDIO_DIR_OUT,
    .SWDIO_SET = SWDIO_SET,
    .SWDIO_GET = SWDIO_GET,
    ._delay = delay
};

static int swd_open(struct inode *inode, struct file* filp)
{
    int ret;
    struct swd_dev *sd = container_of(inode->i_cdev, struct swd_dev, cdev);
    struct rproc_core *rc = sd->rc;

    pr_info("%s: [%s] %d open start\n", SWDDEV_NAME, __func__, __LINE__);

    // allow one process to open it.
    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    ret = rc->core_init();
    if (ret) {
        pr_err("%s: [%s] %d error with _swd_init\n", SWDDEV_NAME, __func__, __LINE__);
        goto swd_init_fail;
    }

    rc->halt_core();

    filp->f_pos = SWD_FLASH_BASE;
    filp->private_data = &swd_dev;

    pr_info("%s: [%s] %d open finished\n", SWDDEV_NAME, __func__, __LINE__);

    return 0;

swd_init_fail:
    atomic_inc(&open_lock);
    return ret;
}

static int swd_release(struct inode *inode, struct file* filp)
{
    struct swd_dev *sd = (struct swd_dev*)filp->private_data;
    struct rproc_core *rc = sd->rc;

    pr_info("%s: [%s] %d release start\n", SWDDEV_NAME, __func__, __LINE__);

    rc->reset();
    atomic_inc(&open_lock);

    pr_info("%s: [%s] %d release finished\n", SWDDEV_NAME, __func__, __LINE__);

    return 0;
}

loff_t swd_llseek(struct file *filp, loff_t off, int whence)
{
    loff_t newpos = -1;

    switch(whence) {
      case 0: /* SEEK_SET */
        newpos = off;
        break;

      case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

      default: /* SEEK_END and others */
        return -EINVAL;
    }

    if (newpos < 0) 
        return -EINVAL;

    filp->f_pos = newpos;
    return newpos;
}

static ssize_t swd_read(struct file *filp, char *user_buf, size_t len, loff_t *off)
{
    int ret;
    u32 base;
    char *buf;
    ssize_t len_to_cpy;
    ssize_t read_len;
    struct swd_dev *sd = (struct swd_dev*)filp->private_data;
    struct rproc_core *rc = sd->rc;

    pr_info("%s: [%s] %d read start\n", SWDDEV_NAME, __func__, __LINE__);

    buf = kmalloc(len, GFP_KERNEL);
    if (!buf) {
        pr_err("%s: [%s] %d NULL from kmalloc\n", SWDDEV_NAME, __func__, __LINE__);
        return 0;
    }

    len_to_cpy = 0;
    base = filp->f_pos;
    do {
        read_len = rc->read_ram(buf + len_to_cpy, base, len);
        if (read_len < 0) {
            len_to_cpy = -1;
            goto swd_ap_read_fault;
        }

        len_to_cpy += read_len;
        base += read_len;
        len -= read_len;
    } while(len/4);

    ret = copy_to_user(user_buf, buf, len_to_cpy);
    if (ret)
        return ret;

    *off += len_to_cpy;

swd_ap_read_fault:
    kfree(buf);

    pr_info("%s: [%s] %d read finished\n", SWDDEV_NAME, __func__, __LINE__);

    return len_to_cpy;
}

//  0. reset line
//  1. read dp reg
//  2. write dp reg
//  3. halt core
//  4. unhalt core
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
    struct swd_dev *sd = (struct swd_dev*)filp->private_data;
    struct rproc_core *rc = sd->rc;

    pr_info("%s: [%s] %d ioctl start\n", SWDDEV_NAME, __func__, __LINE__);

    switch(cmd) {
    case SWDDEV_IOC_RSTLN:
        rc->setup_swd();
        break;
    case SWDDEV_IOC_HLTCORE:
        rc->setup_swd();
        rc->halt_core();
        break;
    case SWDDEV_IOC_UNHLTCORE:
        rc->unhalt_core();
        rc->reset();
        break;
    case SWDDEV_IOC_TSTALIVE:
        rc->setup_swd();
        params.ret = rc->test_alive();
        if(copy_to_user((void*)arg, &params, sizeof(struct swd_parameters)))
            return -EFAULT;
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
        ret = rc->write_ram(buf, params.arg[1], params.arg[2]);
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
        ret = rc->program_flash(buf, params.arg[1], params.arg[2]);
        kfree(buf);
        break;
    case SWDDEV_IOC_ERSFLSH:
        rc->erase_flash_all();
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
    .llseek     = swd_llseek,
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

    // bind with rproc_core
    // stm32f10xx_rc.gpio_bind(&stm32f10xx_sg);
    // swd_dev.rc = &stm32f10xx_rc;
    // for stm32f411
    stm32f411xx_rc.gpio_bind(&stm32f411xx_sg);
    swd_dev.rc = &stm32f411xx_rc;

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
