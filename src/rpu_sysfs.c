#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>

#include "swd.h"
#include "lib_swd.h"
#include "rpu_sysfs.h"

#define RPUDEV_NAME "rpu"

extern atomic_t open_lock;

static struct device *rpu_dev;

int rpu_status = RPU_STATUS_UNHALT;

static ssize_t _rpu_xxx_read(char *buf, loff_t off, size_t count)
{
    u32 pos;
    ssize_t len;
    ssize_t len_to_read;

    pos = 0;
    len_to_read = count;
    do {
        len = _swd_ap_read(&(buf[pos]), off + pos, 
                        (len_to_read > SWD_BANK_SIZE) ? SWD_BANK_SIZE : len_to_read);
        if (len < 0)
            return -1;

        pos += len;
        len_to_read -= len;
    } while(len_to_read);

    return count;
}

static ssize_t rpu_status_read(struct file *filp, struct kobject *kobj,struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    // allow one process to open it.
    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    if (off >= count)
        return 0;

    if (rpu_status == RPU_STATUS_UNHALT)
        count = sprintf(buf, "%s\n", "unhalt");
    else
        count = sprintf(buf, "%s\n", "halt");

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

    atomic_inc(&open_lock);

    return count;
}

static struct bin_attribute rpu_status_attr = {
    .attr.name = "status",
    .attr.mode = 0444,
    .size = 0,
    .read = rpu_status_read,
};

static ssize_t rpu_control_write(struct file *filp, struct kobject *kobj,struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    int ret, val;

    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    ret = kstrtoint(buf, count, &val);
    if (ret < 0)
        return -1;

    if (val == RPU_STATUS_UNHALT) {
        rpu_status = RPU_STATUS_UNHALT;
        _swd_unhalt_core();
    } else {
        rpu_status = RPU_STATUS_HALT;
        _swd_init();
        _swd_halt_core();
    }

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

    atomic_inc(&open_lock);

    return count;
}

static struct bin_attribute rpu_control_attr = {
    .attr.name = "control",
    .attr.mode = 0220,
    .size = 0,
    .write = rpu_control_write,
};

static ssize_t rpu_flash_read(struct file *filp, struct kobject *kobj,struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    if (rpu_status != RPU_STATUS_HALT)
        goto rpu_status_unhalt;

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    if (_rpu_xxx_read(buf, SWD_FLASH_BASE + off, count) < 0){
        count = -1;
        goto rpu_status_unhalt;
    }

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

rpu_status_unhalt:
    atomic_inc(&open_lock);

    return count;
}

static ssize_t rpu_flash_write(struct file *filp, struct kobject *kobj, struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    int err;
    u32 pos;
    ssize_t len;
    ssize_t len_to_write;

    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    if (rpu_status != RPU_STATUS_HALT)
        goto rpu_status_unhalt;

    pos = 0;
    len_to_write = count;
    do {
        len = (len_to_write > FLASH_PAGE_SIZE) ? FLASH_PAGE_SIZE : len_to_write;
        err = _swd_program_flash(&(buf[pos]), SWD_FLASH_BASE + off + pos, len);
        if (err) {
            count = -1;
            goto rpu_status_unhalt;
        }

        len_to_write -= len;
        pos += len;
    } while(len_to_write);

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

rpu_status_unhalt:
    atomic_inc(&open_lock);

    return count;
}

static struct bin_attribute rpu_flash_attr = {
    .attr.name = "flash",
    .attr.mode = 0664,
    .size = 0,
    .read = rpu_flash_read,
    .write = rpu_flash_write,
};

static ssize_t rpu_ram_read(struct file *filp, struct kobject *kobj,struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    if (rpu_status != RPU_STATUS_HALT)
        goto rpu_status_unhalt;

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    if (_rpu_xxx_read(buf, SWD_RAM_BASE + off, count) < 0) {
        count = -1;
        goto rpu_status_unhalt;
    }

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

rpu_status_unhalt:
    atomic_inc(&open_lock);

    return count;
}

static ssize_t rpu_ram_write(struct file *filp, struct kobject *kobj, struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    int err;
    u32 pos;
    ssize_t len;
    ssize_t len_to_write;

    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    if (rpu_status != RPU_STATUS_HALT)
        goto rpu_status_unhalt;

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    pos = 0;
    len_to_write = count;
    do {
        len = (len_to_write > RAM_PAGE_SIZE) ? RAM_PAGE_SIZE : len_to_write;
        err = _swd_write_ram(&(buf[pos]), SWD_RAM_BASE + off + pos, len);
        if (err) {
            count = -1;
            goto rpu_status_unhalt;
        }

        len_to_write -= len;
        pos += len;
    } while(len_to_write);

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

rpu_status_unhalt:
    atomic_inc(&open_lock);

    return count;
}

static struct bin_attribute rpu_ram_attr = {
    .attr.name = "ram",
    .attr.mode = 0664,
    .size = 0,
    .read = rpu_ram_read,
    .write = rpu_ram_write,
};

static struct bin_attribute *rpu_bin_attrs[] = {
    &rpu_status_attr,
    &rpu_control_attr,
    &rpu_ram_attr,
    &rpu_flash_attr,
    NULL
};

static const struct attribute_group rpu_dev_group = {
    .bin_attrs = rpu_bin_attrs,
};

static const struct attribute_group *rpu_dev_groups[] = {
    &rpu_dev_group,
    NULL
};

static struct device_type rpu_sysfs = {
    .name = "rpu_sysfs",
};

static void rpu_sysfs_release(struct device *dev)
{
    // kfree(dev);
}

int rpu_sysfs_init(struct swd_device *swd_dev)
{
    int ret;

    pr_info("%s: [%s] start\n", RPUDEV_NAME, __func__);

    rpu_dev = kzalloc(sizeof(struct device), GFP_KERNEL);
    if (!rpu_dev) {
        pr_err("%s: [%s] Err with memory alloc\n", RPUDEV_NAME, __func__);
        return -ENOMEM;
    }

    device_initialize(rpu_dev);
    rpu_dev->class = swd_dev->cls;
    rpu_dev->type = &rpu_sysfs;
    rpu_dev->parent = swd_dev->dev;
    rpu_dev->groups = rpu_dev_groups;
    rpu_dev->release = rpu_sysfs_release;

    ret = dev_set_name(rpu_dev, RPUDEV_NAME);
    if (ret)
        goto dev_set_name_fail;

    ret = device_add(rpu_dev);
    if (ret)
        goto  device_add_fail;

    pr_info("%s: [%s] finish\n", RPUDEV_NAME, __func__);
    return 0;

device_add_fail:
dev_set_name_fail:
    kfree(rpu_dev);
    return ret;
}

void rpu_sysfs_exit(struct swd_device *swd_dev)
{
    pr_info("%s: [%s] start\n", RPUDEV_NAME, __func__);

    // kfree(rpu_dev);

    pr_info("%s: [%s] finished\n", RPUDEV_NAME, __func__);
}
