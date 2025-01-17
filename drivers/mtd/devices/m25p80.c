/*
 * MTD SPI driver for ST M25Pxx (and similar) serial flash chips
 *
 * Author: Mike Lavender, mike@steroidmicros.com
 *
 * Copyright (c) 2005, Intec Automation Inc.
 *
 * Some parts are based on lart.c by Abraham Van Der Merwe
 *
 * Cleaned up and generalized based on mtd_dataflash.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/mtd/spi-nor.h>
#include <linux/debugfs.h>
#include <linux/reboot.h>
#include <linux/mtd/spi-nor.h>


int spi_nor_wait_till_ready(struct spi_nor *nor);
int write_ear(struct spi_nor *nor, u32 addr);

#define	MAX_CMD_SIZE		6
#define M25P80_DEBUG_DUMP_WRITE_REG           0x00000001
#define M25P80_DEBUG_DUMP_READ_REG            0x00000002

#define MT25_READ_NONVOLATILE_CONFIG_REG        0xb5
#define MT25_READ_VOLATILE_CONFIG_REG           0x85

struct m25p {
	struct spi_device	*spi;
	struct spi_nor		spi_nor;
	u8			command[MAX_CMD_SIZE];
};



static struct spi_nor *spi_nor;

static struct dentry* dir = NULL;
static uint32_t debug_flags = 0;
static uint32_t protect_blocks = SR_BP3;
static uint8_t  sr = 0xff;



static int flash_reset_handle(struct notifier_block *this, unsigned long mode, void *cmd)
{
    write_ear(spi_nor, 0);
    printk(KERN_INFO "Reset flash EAR\n");
    return NOTIFY_DONE;
}

static struct notifier_block flash_reset_handler = {
         .notifier_call = flash_reset_handle,
};



static int m25p80_read_reg(struct spi_nor *nor, u8 code, u8 *val, int len)
{
	struct m25p *flash = nor->priv;
	struct spi_device *spi = flash->spi;
	int ret;

	ret = spi_write_then_read(spi, &code, 1, val, len);
	if (ret < 0)
		dev_err(&spi->dev, "error %d reading %x\n", ret, code);

    if (code == SPINOR_OP_RDSR) {
        sr = val[0];
    }


    if (debug_flags & M25P80_DEBUG_DUMP_READ_REG)
    {
        printk(KERN_INFO "rcode %02x val %08x len %d\n", code, (val != NULL)? *((u32*) val):0, len);
    }

	return ret;
}

static void m25p_addr2cmd(struct spi_nor *nor, unsigned int addr, u8 *cmd)
{
	/* opcode is in cmd[0] */
	cmd[1] = addr >> (nor->addr_width * 8 -  8);
	cmd[2] = addr >> (nor->addr_width * 8 - 16);
	cmd[3] = addr >> (nor->addr_width * 8 - 24);
	cmd[4] = addr >> (nor->addr_width * 8 - 32);
}

static int m25p_cmdsz(struct spi_nor *nor)
{
	return 1 + nor->addr_width;
}

static int m25p80_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct m25p *flash = nor->priv;
	struct spi_device *spi = flash->spi;

    if (opcode == SPINOR_OP_WRSR) {

        buf[0] = (buf[0] & ~(SR_BP3 | SR_BP_BIT_MASK)) | (SR_TB | protect_blocks);
        if (sr != buf[0])
        {
            sr = buf[0];
            //printk(KERN_INFO "++++ SR%02x\n", sr);
        }
    }

    if (debug_flags & M25P80_DEBUG_DUMP_WRITE_REG)
    {

        printk(KERN_INFO "wcode %02x val %08x len %d\n", opcode, (buf != NULL)? *((u32*) buf):0, len);
    }


	flash->command[0] = opcode;
	if (buf)
		memcpy(&flash->command[1], buf, len);

	return spi_write(spi, flash->command, len + 1);
}

static ssize_t m25p80_write(struct spi_nor *nor, loff_t to, size_t len,
			    const u_char *buf)
{
	struct m25p *flash = nor->priv;
	struct spi_device *spi = flash->spi;
	struct spi_transfer t[2] = {};
	struct spi_message m;
	int cmd_sz = m25p_cmdsz(nor);
	ssize_t ret;

	spi_message_init(&m);

	if (nor->program_opcode == SPINOR_OP_AAI_WP && nor->sst_write_second)
		cmd_sz = 1;

	flash->command[0] = nor->program_opcode;
	m25p_addr2cmd(nor, to, flash->command);

	t[0].tx_buf = flash->command;
	t[0].len = cmd_sz;
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	ret = spi_sync(spi, &m);
	if (ret)
		return ret;

	ret = m.actual_length - cmd_sz;
	if (ret < 0)
		return -EIO;
	return ret;
}

static inline unsigned int m25p80_rx_nbits(struct spi_nor *nor)
{
	switch (nor->flash_read) {
	case SPI_NOR_DUAL:
		return 2;
	case SPI_NOR_QUAD:
		return 4;
	default:
		return 0;
	}
}

/*
 * Read an address range from the nor chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static ssize_t m25p80_read(struct spi_nor *nor, loff_t from, size_t len,
			   u_char *buf)
{
	struct m25p *flash = nor->priv;
	struct spi_device *spi = flash->spi;
	struct spi_transfer t[2];
	struct spi_message m;
	unsigned int dummy = nor->read_dummy;
	ssize_t ret;

	/* convert the dummy cycles to the number of bytes */
	dummy /= 8;

	if (spi_flash_read_supported(spi)) {
		struct spi_flash_read_message msg;

		memset(&msg, 0, sizeof(msg));

		msg.buf = buf;
		msg.from = from;
		msg.len = len;
		msg.read_opcode = nor->read_opcode;
		msg.addr_width = nor->addr_width;
		msg.dummy_bytes = dummy;
		/* TODO: Support other combinations */
		msg.opcode_nbits = SPI_NBITS_SINGLE;
		msg.addr_nbits = SPI_NBITS_SINGLE;
		msg.data_nbits = m25p80_rx_nbits(nor);

		ret = spi_flash_read(spi, &msg);
		if (ret < 0)
			return ret;
		return msg.retlen;
	}

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	flash->command[0] = nor->read_opcode;
	m25p_addr2cmd(nor, from, flash->command);

	t[0].tx_buf = flash->command;
	t[0].len = m25p_cmdsz(nor) + dummy;
	t[0].dummy = nor->read_dummy;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].rx_nbits = m25p80_rx_nbits(nor);
	t[1].len = min(len, spi_max_transfer_size(spi));
	spi_message_add_tail(&t[1], &m);

	ret = spi_sync(spi, &m);
	if (ret)
		return ret;

	ret = m.actual_length - m25p_cmdsz(nor) - dummy;
	if (ret < 0)
		return -EIO;
	return ret;
}

/*
 * board specific setup should have ensured the SPI clock used here
 * matches what the READ command supports, at least until this driver
 * understands FAST_READ (for clocks over 25 MHz).
 */
static int m25p_probe(struct spi_device *spi)
{
	struct flash_platform_data	*data;
	struct m25p *flash;
	struct spi_nor *nor;
	enum read_mode mode = SPI_NOR_NORMAL;
	char *flash_name;
	int ret;
    struct dentry *junk;
    u8 val[8];

    dir = debugfs_create_dir("m25p80", 0);
    if (!dir) {
      printk(KERN_INFO "debugfs: cannot create /sys/kernel/debug/m25p80\n");
    }


    junk = debugfs_create_u32("debug", 0777, dir, &debug_flags);
    if (!junk) {
      printk(KERN_ALERT "debugfs: failed to create /sys/kernel/debug/m25p80/debug\n");
    }

    junk = debugfs_create_u32("protect", 0777, dir, &protect_blocks);
    if (!junk) {
      printk(KERN_ALERT "debugfs: failed to create /sys/kernel/debug/m25p80/protect\n");
    }

    junk = debugfs_create_u8("sr", 0777, dir, &sr);
    if (!junk) {
      printk(KERN_ALERT "debugfs: failed to create /sys/kernel/debug/m25p80/sr\n");
    }

	data = dev_get_platdata(&spi->dev);

	flash = devm_kzalloc(&spi->dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;

	nor = &flash->spi_nor;

	/* install the hooks */
	nor->read = m25p80_read;
	nor->write = m25p80_write;
	nor->write_reg = m25p80_write_reg;
	nor->read_reg = m25p80_read_reg;

	nor->dev = &spi->dev;
	spi_nor_set_flash_node(nor, spi->dev.of_node);
	nor->priv = flash;

	spi_set_drvdata(spi, flash);
	flash->spi = spi;
	nor->spi = spi;

	if (spi->mode & SPI_RX_QUAD)
		mode = SPI_NOR_QUAD;
	else if (spi->mode & SPI_RX_DUAL)
		mode = SPI_NOR_DUAL;

	if (data && data->name)
		nor->mtd.name = data->name;

	/* For some (historical?) reason many platforms provide two different
	 * names in flash_platform_data: "name" and "type". Quite often name is
	 * set to "m25p80" and then "type" provides a real chip name.
	 * If that's the case, respect "type" and ignore a "name".
	 */
	if (data && data->type)
		flash_name = data->type;
	else if (!strcmp(spi->modalias, "spi-nor"))
		flash_name = NULL; /* auto-detect */
	else
		flash_name = spi->modalias;

	ret = spi_nor_scan(nor, flash_name, mode);
	if (ret)
		return ret;

    spi_nor = nor;

    // Dumping non volatile configuration register
    m25p80_read_reg(nor, MT25_READ_NONVOLATILE_CONFIG_REG, val, 2);
    printk(KERN_INFO "NONVOLATILE CONFIG REG %02x%02x\n", val[0], val[1]);
    
    m25p80_write_reg(nor, SPINOR_OP_WREN, NULL, 0);
    val[0] = 0;
    m25p80_write_reg(nor, SPINOR_OP_WRSR, val, 1);
    spi_nor_wait_till_ready(nor);

    m25p80_read_reg(nor, SPINOR_OP_RDSR, val, 1);


    register_reboot_notifier(&flash_reset_handler);

	return mtd_device_register(&nor->mtd, data ? data->parts : NULL,
				   data ? data->nr_parts : 0);
}


static int m25p_remove(struct spi_device *spi)
{
	struct m25p	*flash = spi_get_drvdata(spi);

	/* Clean up MTD stuff. */
	return mtd_device_unregister(&flash->spi_nor.mtd);
}

/*
 * Do NOT add to this array without reading the following:
 *
 * Historically, many flash devices are bound to this driver by their name. But
 * since most of these flash are compatible to some extent, and their
 * differences can often be differentiated by the JEDEC read-ID command, we
 * encourage new users to add support to the spi-nor library, and simply bind
 * against a generic string here (e.g., "jedec,spi-nor").
 *
 * Many flash names are kept here in this list (as well as in spi-nor.c) to
 * keep them available as module aliases for existing platforms.
 */
static const struct spi_device_id m25p_ids[] = {
	/*
	 * Allow non-DT platform devices to bind to the "spi-nor" modalias, and
	 * hack around the fact that the SPI core does not provide uevent
	 * matching for .of_match_table
	 */
	{"spi-nor"},

	/*
	 * Entries not used in DTs that should be safe to drop after replacing
	 * them with "spi-nor" in platform data.
	 */
	{"s25sl064a"},	{"w25x16"},	{"m25p10"},	{"m25px64"},

	/*
	 * Entries that were used in DTs without "jedec,spi-nor" fallback and
	 * should be kept for backward compatibility.
	 */
	{"at25df321a"},	{"at25df641"},	{"at26df081a"},
	{"mr25h256"},
	{"mx25l4005a"},	{"mx25l1606e"},	{"mx25l6405d"},	{"mx25l12805d"},
	{"mx25l25635e"},{"mx66l51235l"},
	{"n25q064"},	{"n25q128a11"},	{"n25q128a13"},	{"n25q512a"},
	{"s25fl256s1"},	{"s25fl512s"},	{"s25sl12801"},	{"s25fl008k"},
	{"s25fl064k"},
	{"sst25vf040b"},{"sst25vf016b"},{"sst25vf032b"},{"sst25wf040"},
	{"m25p40"},	{"m25p80"},	{"m25p16"},	{"m25p32"},
	{"m25p64"},	{"m25p128"},
	{"w25x80"},	{"w25x32"},	{"w25q32"},	{"w25q32dw"},
	{"w25q80bl"},	{"w25q128"},	{"w25q256"},

	/* Flashes that can't be detected using JEDEC */
	{"m25p05-nonjedec"},	{"m25p10-nonjedec"},	{"m25p20-nonjedec"},
	{"m25p40-nonjedec"},	{"m25p80-nonjedec"},	{"m25p16-nonjedec"},
	{"m25p32-nonjedec"},	{"m25p64-nonjedec"},	{"m25p128-nonjedec"},

	{ },
};
MODULE_DEVICE_TABLE(spi, m25p_ids);

static const struct of_device_id m25p_of_table[] = {
	/*
	 * Generic compatibility for SPI NOR that can be identified by the
	 * JEDEC READ ID opcode (0x9F). Use this, if possible.
	 */
	{ .compatible = "jedec,spi-nor" },
	{}
};
MODULE_DEVICE_TABLE(of, m25p_of_table);

static struct spi_driver m25p80_driver = {
	.driver = {
		.name	= "m25p80",
		.of_match_table = m25p_of_table,
	},
	.id_table	= m25p_ids,
	.probe	= m25p_probe,
	.remove	= m25p_remove,

	/* REVISIT: many of these chips have deep power-down modes, which
	 * should clearly be entered on suspend() to minimize power use.
	 * And also when they're otherwise idle...
	 */
};

module_spi_driver(m25p80_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("MTD SPI driver for ST M25Pxx flash chips");
