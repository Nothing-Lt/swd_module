#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "swd_drv.h"
#include "rpu_sysfs.h"

#define RPUDEV_NAME "rpu"

extern atomic_t open_lock;

static struct device *rpu_dev;

int rpu_status = RPU_STATUS_UNHALT;

struct swd_device *rpu_swd_dev;

static ssize_t _rpu_xxx_read(char *buf, loff_t off, size_t count)
{
    struct rproc_core *rc = rpu_swd_dev->rc;

    rc->read_ram(buf, off, count);

    return count;
}

static ssize_t flash_write_uni(struct rproc_core *rc, char* buf, u32 offset, u32 count)
{
    int err;
    int pos;
    int len;
    int len_to_write;
    struct core_mem *cm = rc->ci->cm;

    pos = 0;
    len_to_write = count;
    do {
        len = (len_to_write > cm->flash.program_size) ? cm->flash.program_size : len_to_write;
        err = rc->program_flash(cm, &(buf[pos]), offset + pos, len);
        if (err) {
            /* Program error, erase flash */
            rc->erase_flash_page(cm, offset + pos, len);
            continue;
        }

        len_to_write -= len;
        pos += len;
    } while(len_to_write);

    return count;
}

static ssize_t flash_write(struct rproc_core *rc, char* buf, u32 offset, u32 count)
{
    int i;
    int err;
    int pos;
    int len;
    int retry = 10;
    int len_to_write;
    u32 page_size = 0;
    u32 page_offset = 0;
    char *buf_page = NULL;
    struct core_mem *cm = rc->ci->cm;

    // 1. find flash page info
    if (cm->flash.attr) {
        // non-unified page size, find the corresponding page
        for (i = cm->flash.offset;
            (cm->mem_segs[i].start !=0) || (cm->mem_segs[i].size != 0);
            i++) {
            if ((offset >= cm->mem_segs[i].start) && \
                (offset < cm->mem_segs[i].start + cm->mem_segs[i].size)) {
                    page_offset = cm->mem_segs[i].start;
                    page_size = cm->mem_segs[i].size;
                    break;
            }
        }
    } else { // unified page size
        if (cm->mem_segs[cm->flash.offset].size > count) {
            // calculate the page base address and size
            page_offset = offset & ~(cm->mem_segs[cm->flash.offset].size - 1);
            page_size = cm->mem_segs[cm->flash.offset].size;
            i = cm->flash.offset;
        }
    }

    // 2. keep data from flash page in buffer
    if ((page_offset == 0) && (page_size == 0)) {
        pos = 0;
        buf_page = buf;
    } else {
        // get buff to keep data in the page
        buf_page = vmalloc(page_size);
        if (!buf_page)
            return -1;

        // read data from flash
        if (_rpu_xxx_read(buf_page, page_offset,
                            offset - page_offset) < 0) {
            count = -1;
            goto read_flash_err;
        }

        // calculate the pos, copy data to buf_page
        pos = offset - page_offset;
        memcpy(&(buf_page[pos]), buf, count);
    }

    // 3. program the flash
    //    rogram current data first, if there is an error happend,
    //    reprogram this page.
    len_to_write = count;
    do {
        len = (len_to_write > cm->flash.program_size) ? \
	      cm->flash.program_size : len_to_write;

        err = rc->program_flash(cm, &(buf_page[pos]), page_offset + pos, len);
        if (err) {
            /* Program error, erase this page, and reprogram */
            rc->erase_flash_page(cm, page_offset, page_size);
	    if (!retry--){
                count = -1;
                goto read_flash_err;
            }
            pos = 0;
            len_to_write = (offset - page_offset) + count;
            continue;
        }

        len_to_write -= len;
        pos += len;
    } while(len_to_write);

read_flash_err:
    vfree(buf_page);

    return count;
}

static ssize_t rpu_corename_read(struct file *filp, struct kobject *kobj,
        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    struct rproc_core *rc = rpu_swd_dev->rc;

    if (off)
        return 0;

    count = sprintf(buf, "%s\n", rc->core_name);

    return count;
}

static struct bin_attribute rpu_corename_attr = {
    .attr.name = "core_name",
    .attr.mode = 0444,
    .size = 0,
    .read = rpu_corename_read,
};

static ssize_t rpu_meminfo_read(struct file *filp, struct kobject *kobj,
        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    int i;
    struct rproc_core *rc = rpu_swd_dev->rc;
    struct core_mem *cm = rc->ci->cm;

    if (off)
        return 0;

    // get the 
    count = sprintf(buf, "SRAM:\n");
    if (!cm->sram.attr) {
        count += sprintf(buf + count,
            "\t base: 0x%08x\n"
            "\t  len: %u-bytes\n"
            "\t program_size: 0x%x\n",
            cm->sram.base, cm->sram.len, cm->sram.program_size );
    } else {
        count += sprintf(buf + count,
            "\t base: 0x%08x\n"
            "\t program_size: 0x%x\n",
            cm->sram.base, cm->sram.program_size);
        for (i = cm->sram.offset;
            i < cm->sram.offset + cm->sram.len;
            i++) {
            count += sprintf(buf + count,
                        "\t start: 0x%08x - len: %u-bytes\n",
                        cm->mem_segs[i].start, cm->mem_segs[i].size);
        }
    }

    count += sprintf(buf + count, "FLASH:\n");
    if (!cm->flash.attr) {
        count += sprintf(buf + count,
            "\t base: 0x%08x\n"
            "\t  len: %u-bytes\n"
            "\t program_size: 0x%x\n",
            cm->flash.base, cm->flash.len, cm->flash.program_size );
    } else {
        count += sprintf(buf + count,
            "\t base: 0x%08x\n"
            "\t program_size: 0x%x\n",
            cm->flash.base, cm->flash.program_size);
        for (i = cm->flash.offset;
            i < cm->flash.offset + cm->flash.len;
            i++) {
            count += sprintf(buf + count,
                        "\t start: 0x%08x - len: %u-bytes\n",
                        cm->mem_segs[i].start, cm->mem_segs[i].size);
        }
    }

    return count;
}

static struct bin_attribute rpu_meminfo_attr = {
    .attr.name = "core_mem",
    .attr.mode = 0444,
    .size = 0,
    .read = rpu_meminfo_read,
};
static ssize_t rpu_status_read(struct file *filp, struct kobject *kobj,
        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    // allow one process to open it.
    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    if (off) {
        count = 0;
        goto rpu_status_finish;
    }

    if (rpu_status == RPU_STATUS_UNHALT)
        count = sprintf(buf, "%s\n", "unhalt");
    else
        count = sprintf(buf, "%s\n", "halt");

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

rpu_status_finish:
    atomic_inc(&open_lock);

    return count;
}

static struct bin_attribute rpu_status_attr = {
    .attr.name = "status",
    .attr.mode = 0444,
    .size = 0,
    .read = rpu_status_read,
};

static ssize_t rpu_control_write(struct file *filp, struct kobject *kobj,
        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    int ret, val;
    int retry = 10;
    struct rproc_core *rc = rpu_swd_dev->rc;

    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    ret = kstrtoint(buf, count, &val);
    if (ret < 0) {
        count = -1;
        goto rpu_control_finish;
    }

    if (val == RPU_STATUS_UNHALT) {
        rpu_status = RPU_STATUS_UNHALT;
        rc->core_unhalt();
    } else {
        rpu_status = RPU_STATUS_HALT;

        retry = 10;
        do {
            ret = rc->core_init();
        } while(ret && retry--);
        if (!retry) {
                count = -EBUSY;
                goto rpu_control_finish;
	}

        retry = 10;
        do {
           ret = rc->core_halt();
        } while(ret && retry--);
	if (!retry) {
		count = -EBUSY;
		goto rpu_control_finish;
	}
    }

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

rpu_control_finish:
    atomic_inc(&open_lock);

    return count;
}

static struct bin_attribute rpu_control_attr = {
    .attr.name = "control",
    .attr.mode = 0220,
    .size = 0,
    .write = rpu_control_write,
};

static ssize_t rpu_flash_read(struct file *filp, struct kobject *kobj,
        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    struct rproc_core *rc = rpu_swd_dev->rc;
    struct core_mem *cm = rc->ci->cm;

    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    if (rpu_status != RPU_STATUS_HALT)
        goto rpu_status_unhalt;

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    if (_rpu_xxx_read(buf, cm->flash.base + off, count) < 0) {
        count = -1;
        goto rpu_status_unhalt;
    }

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

rpu_status_unhalt:
    atomic_inc(&open_lock);

    return count;
}

static ssize_t rpu_flash_write(struct file *filp, struct kobject *kobj,
        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    struct rproc_core *rc = rpu_swd_dev->rc;
    struct core_mem *cm = rc->ci->cm;

    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    if (rpu_status != RPU_STATUS_HALT)
        goto rpu_status_unhalt;

    if (!cm->flash.attr)
        count = flash_write_uni(rc, buf, off, count);
    else
        count = flash_write(rc, buf, off, count);

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

static ssize_t rpu_ram_read(struct file *filp, struct kobject *kobj,
        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    struct rproc_core *rc = rpu_swd_dev->rc;
    struct core_mem *cm = rc->ci->cm;

    if(!atomic_dec_and_test(&open_lock)){
        atomic_inc(&open_lock);
        return -EBUSY;
    }

    if (rpu_status != RPU_STATUS_HALT)
        goto rpu_status_unhalt;

    pr_info("%s [%s] start\n",RPUDEV_NAME, __func__);

    if (_rpu_xxx_read(buf, cm->sram.base + off, count) < 0) {
        count = -1;
        goto rpu_status_unhalt;
    }

    pr_info("%s [%s] finish\n",RPUDEV_NAME, __func__);

rpu_status_unhalt:
    atomic_inc(&open_lock);

    return count;
}

static ssize_t rpu_ram_write(struct file *filp, struct kobject *kobj,
        struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
    int err;
    int pos;
    int len;
    int len_to_write;
    struct rproc_core *rc = rpu_swd_dev->rc;
    struct core_mem *cm = rc->ci->cm;

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
        len = (len_to_write > cm->sram.program_size) ? cm->sram.program_size : len_to_write;
        err = rc->write_ram(cm, &(buf[pos]), off + pos, len);
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
    &rpu_corename_attr,
    &rpu_meminfo_attr,
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
    rpu_swd_dev = swd_dev;

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
