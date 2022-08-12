#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>

#include "swd.h"
#include "lib_swd.h"
#include "rpu_sysfs.h"
#include "../include/swd_module.h"

static struct swd_dev
{
    struct cdev cdev;
    u32 ope_base;
} swd_dev;
struct class *cls;
struct device *dev;
static int swd_major = 0;

extern spinlock_t __lock;
extern int _swclk_pin;
extern int _swdio_pin;

extern int rpu_status;

atomic_t open_lock = ATOMIC_INIT(1);

static int swd_open(struct inode *inode, struct file* filp)
{
    int ret;

    pr_info("%s: [%s] %d open start\n", SWDDEV_NAME, __func__, __LINE__);

    // allow one process to open it.
    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    ret = _swd_init();
    if (ret) {
        pr_err("%s: [%s] %d error with _swd_init\n", SWDDEV_NAME, __func__, __LINE__);
        return ret;
    }

    _swd_halt_core();
    rpu_status = RPU_STATUS_HALT;

    swd_dev.ope_base = SWD_FLASH_BASE;
    filp->private_data = &swd_dev;

    pr_info("%s: [%s] %d open finished\n", SWDDEV_NAME, __func__, __LINE__);

    return 0;
}

static int swd_release(struct inode *inode, struct file* filp)
{
    pr_info("%s: [%s] %d release start\n", SWDDEV_NAME, __func__, __LINE__);

    atomic_inc(&open_lock);
    
    pr_info("%s: [%s] %d release finished\n", SWDDEV_NAME, __func__, __LINE__);

    return 0;
}

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
        rpu_status = RPU_STATUS_HALT;
        break;
    case SWDDEV_IOC_UNHLTCORE:
        _swd_unhalt_core();
        rpu_status = RPU_STATUS_UNHALT;
        _swd_reset();
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
        ret = _swd_write_ram(buf, params.arg[1], params.arg[2]);
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

    ret = rpu_sysfs_init();
    if (ret)
        goto rpu_sysfs_init_fail;

    spin_lock_init(&__lock);
    pr_info("%s: [%s] %d probe finished\n", SWDDEV_NAME, __func__, __LINE__);
    return 0;

rpu_sysfs_init_fail:
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
    
    rpu_sysfs_exit();
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
