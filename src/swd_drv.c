#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/platform_device.h>

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
static struct device *device;
static int swd_major = 0;

static spinlock_t __lock;
static atomic_t open_lock = ATOMIC_INIT(1);
static struct gpio_desc *_swclk;
static struct gpio_desc *_swdio;

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
    gpiod_direction_output(_swclk, v);
}

static inline void SWDIO_SET (int v) {
    gpiod_direction_output(_swdio, v);
}

static inline void SWDIO_DIR_IN (void) {
    gpiod_direction_input(_swdio);
}

static inline void SWDIO_DIR_OUT (void) {
    gpiod_direction_output(_swdio, 1);
}

static inline int SWDIO_GET (void) {
    return gpiod_get_value(_swdio);
}

static inline void signal_begin (void) {
    spin_lock_irq(&__lock);
}

static inline void signal_end (void) {
    spin_unlock_irq(&__lock);
}

// setup the gpio level things
struct swd_gpio sg = {
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

    filp->f_pos = rc->ci->cm->flash.base;
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
//  1. halt core
//  2. unhalt core
//  2. test alive
//  4. download to sram
//  5. download to flash
//  6. erase flash
//  7. erase flash by page
//  8. verify
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
    case SWDDEV_IOC_ERSFLSH_PG:
        if (copy_from_user(&params, (void*)arg, sizeof(struct swd_parameters)))
            return -EFAULT;
        rc->erase_flash_page(params.arg[0], params.arg[1]);
        break;
    case SWDDEV_IOC_MEMINFO_GET:
        if (copy_from_user(&params, (void*)arg, sizeof(struct swd_parameters)))
            return -EFAULT;
        if (copy_to_user((void*)params.arg[0], rc->ci->cm, rc->ci->cm_size))
            return -EFAULT;
        pr_err("[%s] %d size:%d\n", __FILE__, __LINE__, rc->ci->cm_size);
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

struct core_name_pair {
    char *name;
    struct rproc_core *rc;
} core_name_pairs[] = {
    {
        .name = "stm32f10xx",
        .rc = &stm32f10xx_rc
    }, 
    {
        .name = "stm32f411xx",
        .rc = &stm32f411xx_rc
    }
};

static int swd_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    const char *core_name;
    dev_t devid;
    int ret;
    int i;

    pr_info("%s: [%s] %d start\n", SWDDEV_NAME, __func__, __LINE__);

    _swclk = devm_gpiod_get(dev, "swclk", GPIOD_OUT_LOW);
    if (IS_ERR(_swclk)) {
        pr_err("%s [%s] %d Err with get swclk\n", SWDDEV_NAME, __func__, __LINE__);
        ret = PTR_ERR(_swclk);
        goto swclk_request_fail;
    }

    _swdio = devm_gpiod_get(dev, "swdio", GPIOD_OUT_LOW);
    if (IS_ERR(_swdio)) {
        pr_err("%s [%s] %d Err with get swdio\n", SWDDEV_NAME, __func__, __LINE__);
        ret = PTR_ERR(_swdio);
        goto swdio_request_fail;
    }

    // find the matching core
    if (of_property_read_string(dev->of_node, "core", &core_name)) {
        pr_err("%s [%s] %d Err with get core\n", SWDDEV_NAME, __func__, __LINE__);
        stm32f10xx_rc.gpio_bind(&sg);
        swd_dev.rc = &stm32f10xx_rc;
    }
    for (i = 0 ; 
        i < sizeof(core_name_pairs)/sizeof(struct core_name_pair) ; 
        i++) {
        if (!strcmp(core_name_pairs[i].name, core_name)){
            swd_dev.rc = core_name_pairs[i].rc;
            break;
        }
    }
    swd_dev.rc->gpio_bind(&sg);

    cdev_init(&swd_dev.cdev, &fops);
    swd_dev.cdev.owner = THIS_MODULE;

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

    device = device_create(cls, NULL, MKDEV(swd_major,0), NULL, SWDDEV_NAME);
    if (!device)
        goto device_create_fail;

    spin_lock_init(&__lock);
    pr_info("%s: [%s] %d finished\n", SWDDEV_NAME, __func__, __LINE__);
    return 0;

device_create_fail:
    class_destroy(cls);

class_create_fail:
    cdev_del(&swd_dev.cdev);

cdev_add_fail:
    unregister_chrdev_region(MKDEV(swd_major, 0), 1);

chrdev_region_fail:
    gpiod_put(_swdio);

swdio_request_fail:
    gpiod_put(_swclk);

swclk_request_fail:
    return ret;
}

static int swd_remove(struct platform_device *pdev)
{
    pr_info("%s: [%s] %d start\n", SWDDEV_NAME, __func__, __LINE__);
    
    gpiod_put(_swdio);
    gpiod_put(_swclk);
    device_destroy(cls, MKDEV(swd_major, 0));
    class_destroy(cls);
    cdev_del(&swd_dev.cdev);
    unregister_chrdev_region(MKDEV(swd_major, 0), 1);

    pr_info("%s: [%s] %d finished\n", SWDDEV_NAME, __func__, __LINE__);

    return 0;
}

static struct of_device_id swd_driver_ids[] = {
    {.compatible = "swd,remoteproc"},
    {}
};
MODULE_DEVICE_TABLE(of, swd_driver_ids);

static struct platform_driver swd_driver = {
    .probe = swd_probe,
    .remove = swd_remove,
    .driver = {
        .name = "swd",
        .of_match_table = swd_driver_ids,
    },
};

static int __init swd_init(void)
{
    pr_info("%s: [%s] %d start\n", SWDDEV_NAME, __func__, __LINE__);

    if(platform_driver_register(&swd_driver)) {
        pr_err("%s: [%s] %d Err with platform_driver_register\n", SWDDEV_NAME, __func__, __LINE__);
        return -1;
    }

    pr_info("%s: [%s] %d finished\n", SWDDEV_NAME, __func__, __LINE__);

    return 0;
}

static void __exit swd_exit(void)
{
    pr_info("%s: [%s] %d start\n", SWDDEV_NAME, __func__, __LINE__);

    platform_driver_unregister(&swd_driver);

    pr_info("%s: [%s] %d finished\n", SWDDEV_NAME, __func__, __LINE__);

}

module_init(swd_init);
module_exit(swd_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LINAQIN");
MODULE_DESCRIPTION("A Simple Arm Serial Debug(host) port driver");
