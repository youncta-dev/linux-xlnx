/*
 * Youncta MacLite Linux driver for the Youncta Ethernet MAC Lite device.
 *
 * This is a new flat driver which is based on the original Mac_lite
 * driver from John Williams <john.williams@Youncta.com>.
 *
 * 2007 - 2013 (c) Youncta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/interrupt.h>

#define DRIVER_NAME "youncta_temac"


struct temac_local {


	void __iomem *base_addr;

	spinlock_t reset_lock;

};

static struct temac_local tl;

/**
 * temac_of_probe - Probe method for the Maclite device.
 * @ofdev:	Pointer to OF device structure
 * @match:	Pointer to the structure used for matching a device
 *
 * This function probes for the Maclite device in the device tree.
 * It initializes the driver data structure and the hardware, sets the MAC
 * address and registers the network device.
 * It also registers a mii_bus for the Maclite device, if MDIO is included
 * in the device.
 *
 * Return:	0, if the driver is bound to the Maclite device, or
 *		a negative error if there is failure.
 */
static int temac_of_probe(struct platform_device *ofdev)
{
	struct resource *res;

	struct temac_local *lp = &tl;
	struct device *dev = &ofdev->dev;
    void __iomem *addr; 

	int rc = 0;

	dev_info(dev, "Device Tree Probing\n");


	res = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	lp->base_addr = devm_ioremap_resource(&ofdev->dev, res);
	if (IS_ERR(lp->base_addr)) {
		rc = PTR_ERR(lp->base_addr);
		goto error;
	}

    dev_info(dev, "Temac base addr %08x\n", lp->base_addr);


	spin_lock_init(&lp->reset_lock);

    addr = lp->base_addr;
    *(u32*) (addr  + 0x0404) = 0xdb000000;
    *(u32*) (addr  + 0x0404) = 0x5b000000;
    *(u32*) (addr  + 0x0408) = 0xd0000000;
    *(u32*) (addr  + 0x0408) = 0x50000000;
    *(u32*) (addr  + 0x0500) = 0x00000058;
    *(u32*) (addr  + 0x0508) = 0x00001140;
    *(u32*) (addr  + 0x0504) = 0x00004800;

    do {
        udelay(1000);
    }
    while (((*(u32*) (addr+0x050C)) & 0x10000) != 0x10000);

    *(u32*) (addr  + 0x0508) = 0x00001340;
    *(u32*) (addr  + 0x0504) = 0x00004800;

    do {
        udelay(1000);
    }
    while (((*(u32*) (addr+0x050C)) & 0x10000) != 0x10000);

    *(u32*) (addr  + 0x0504) = 0x00018800;

    do {
        udelay(1000);
    }
    while (((*(u32*) (addr+0x050C)) & 0x10000) != 0x10000);

    *(u32*) (addr  + 0x0504) = 0x00018800;

    do {
        udelay(1000);
    }
    while (((*(u32*) (addr+0x050C)) & 0x10000) != 0x10000);

    printk(KERN_INFO "Status register %x\n", *(u32*) (addr  + 0x050c));

error:

	return rc;
}

/**
 * temac_of_remove - Unbind the driver from the Maclite device.
 * @of_dev:	Pointer to OF device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees any resources allocated to
 * the device.
 *
 * Return:	0, always.
 */
static int temac_of_remove(struct platform_device *of_dev)
{

	return 0;
}


/* Match table for OF platform binding */
static struct of_device_id temac_of_match[] = {
	{ .compatible = "temac", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, temac_of_match);

static struct platform_driver temac_of_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = temac_of_match,
	},
	.probe		= temac_of_probe,
	.remove		= temac_of_remove,
};

module_platform_driver(temac_of_driver);

MODULE_AUTHOR("Youncta, Inc.");
MODULE_DESCRIPTION("Youncta Temac support driver");
MODULE_LICENSE("GPL");
