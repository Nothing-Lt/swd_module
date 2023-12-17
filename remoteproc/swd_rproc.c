
// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/of.h>
#include <linux/firmware.h>
#include <linux/remoteproc.h>
#include <linux/elf.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include "remoteproc_internal.h"
#include "remoteproc_elf_helpers.h"

#include "swd/swd_core.h"

struct swd_gpio_rproc {
    struct rproc_core *rc;
};

extern struct rproc_core stm32f103c8t6_rc;
extern struct rproc_core stm32f411ceu6_rc;

/* gpio bind definition */
static spinlock_t __lock;
static struct gpio_desc *_swclk;
static struct gpio_desc *_swdio;

#define SWD_DELAY   (1000000000UL / 2000)

static inline void _delay(unsigned long long time_out)
{
    for (; time_out ; time_out--);
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

/* flash write functions */
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
        ;
        if (rc->read_ram(buf_page, page_offset, offset - page_offset) < 0) {
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

/* rproc functions */
/* rproc_core: init, halt core */
static int swd_rproc_prepare(struct rproc *rproc)
{
    int ret;
    struct swd_gpio_rproc *sgr = rproc->priv;
    struct rproc_core *rc = sgr->rc;

    ret = rc->core_init();
    if (ret)
        goto swd_init_fail;

    rc->core_halt();

    return 0;

swd_init_fail:
    return ret;
}

static int swd_elf_load_segments(struct rproc *rproc, const struct firmware *fw)
{
    uint32_t count, size = fw->size;
    const uint8_t *buff = fw->data;
    struct swd_gpio_rproc *sgr = rproc->priv;
    struct rproc_core *rc = sgr->rc;

    // add flash downloading function call here.
    // 1. get fw data
    // 2. get download address and size
    // 3. handle the flash downloading error.
    if (!rc->ci->cm->flash.attr)
        count = flash_write_uni(rc, buff, 0, size);
    else
        count = flash_write(rc, buff, 0, size);

    return 0;
}

static int swd_rproc_start(struct rproc *rproc)
{
    struct swd_gpio_rproc *sgr = rproc->priv;
    struct rproc_core *rc = sgr->rc;

    rc->core_unhalt();

	return 0;
}

static int swd_rproc_stop(struct rproc *rproc)
{
    int ret;
    struct swd_gpio_rproc *sgr = rproc->priv;
    struct rproc_core *rc = sgr->rc;

    ret = rc->core_init();
    if (ret)
        goto swd_init_fail;

    rc->core_halt();

	return 0;

swd_init_fail:
    return ret;
}

static struct rproc_ops swd_rproc_ops = {
    .prepare    = swd_rproc_prepare,
	.start		= swd_rproc_start,
	.stop		= swd_rproc_stop,
	.load		= swd_elf_load_segments,
};

struct rproc_core *cores[] = {
    &stm32f103c8t6_rc,
    &stm32f411ceu6_rc
};

static const struct of_device_id swd_rproc_match[] = {
	{ .compatible = "rproc,swd-gpio" },
	{},
};
MODULE_DEVICE_TABLE(of, swd_rproc_match);

static int swd_rproc_probe(struct platform_device *pdev)
{
    int i, ret;
    struct rproc *rproc;
	struct swd_gpio_rproc *sgr;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
    const char *core_name;

    rproc = rproc_alloc(dev, np->name, &swd_rproc_ops, NULL,
			sizeof(struct swd_gpio_rproc));
	if (!rproc)
		return -ENOMEM;

    sgr = rproc->priv;

    _swclk = devm_gpiod_get(dev, "swclk", GPIOD_OUT_LOW);
    if (IS_ERR(_swclk)) {
        dev_err(&rproc->dev, "Err with get swclk\n");
        ret = PTR_ERR(_swclk);
        goto swclk_request_fail;
    }

    _swdio = devm_gpiod_get(dev, "swdio", GPIOD_OUT_LOW);
    if (IS_ERR(_swdio)) {
        dev_err(&rproc->dev, "Err with get swdio\n");
        ret = PTR_ERR(_swdio);
        goto swdio_request_fail;
    }

    // find the matching core
    sgr->rc = cores[0];
    if (!of_property_read_string(dev->of_node, "core", &core_name)) {
        for (i = 0 ; i < sizeof(cores) / sizeof(struct rproc_core*) ; i++) {
            if (!strcmp(cores[i]->core_name, core_name)) {
                sgr->rc = cores[i];
                break;
            }
        }
    } else
        dev_err(&rproc->dev, "Err with get core\n");

    sgr->rc->gpio_bind(&sg);

	ret = rproc_add(rproc);
	if (ret)
		goto rproc_add_fail;

    dev_info(&rproc->dev, "probed with core:%s\n", sgr->rc->core_name);

    return 0;

rproc_add_fail:
    gpiod_put(_swdio);
swdio_request_fail:
    gpiod_put(_swclk);
swclk_request_fail:
    rproc_free(rproc);

    return ret;
}

static int swd_rproc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver swd_rproc_driver = {
	.probe = swd_rproc_probe,
	.remove = swd_rproc_remove,
	.driver = {
		.name = "rproc-swd-gpio",
		.of_match_table = of_match_ptr(swd_rproc_match),
	},
};
module_platform_driver(swd_rproc_driver);

MODULE_DESCRIPTION("SWD GPIO remoteproc driver");
MODULE_AUTHOR("LI NAQIN");
MODULE_LICENSE("GPL v2");