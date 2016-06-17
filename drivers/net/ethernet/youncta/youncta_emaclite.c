/*
 * Xilinx MacLite Linux driver for the Xilinx Ethernet MAC Lite device.
 *
 * This is a new flat driver which is based on the original Mac_lite
 * driver from John Williams <john.williams@xilinx.com>.
 *
 * 2007 - 2013 (c) Xilinx, Inc.
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
#include <linux/debugfs.h>

#define DRIVER_NAME "youncta_emaclite"

#define YEL_MAX_FRAME_LEN           0x800
#define YEL_BD_LEN                  4
#define YEL_FRAME_TRAIL_PADDING     2
#define YEL_MAX_BUFFER_SIZE         0x10000

#define SIOCDEVDUMPTCAM     SIOCDEVPRIVATE
#define SIOCDEVWRITETCAMENTRY    (SIOCDEVPRIVATE+1)

#define YEL_RPLR_LENGTH_MASK	0x0000FFFF	/* Rx packet length */
#define YEL_HEADER_OFFSET	    12		/* Offset to length field */
#define YEL_HEADER_SHIFT	    16		/* Shift value for length */

/* General Ethernet Definitions */
#define YEL_ARP_PACKET_SIZE		    28	/* Max ARP packet size */
#define YEL_HEADER_IP_LENGTH_OFFSET	16	/* IP Length Offset */

#define TX_TIMEOUT		(60*HZ)		/* Tx timeout is 60 seconds. */
#define ALIGNMENT		4

/* BUFFER_ALIGN(adr) calculates the number of bytes to the next alignment. */
#define BUFFER_ALIGN(adr) ((ALIGNMENT - ((u32) adr)) % ALIGNMENT)

#define YEMAC_DEBUG_DUMP_TX     0x00000001
#define YEMAC_DEBUG_DUMP_RX     0x00000002

/**
 * struct net_local - Our private per device data
 * @ndev:		    instance of the network device
 * @rx_frame_buf:	rx frame buf start
 * @tx_frame_buf:	tx frame buf start
 * @rx_frame_off:	next rx buffer to write to
 * @tx_frame_off:	next tx buffer to read from
 * @base_addr:		base address of the Maclite device
 * @reset_lock:		lock used for synchronization
 * @deferred_skb:	holds an skb (for transmission at a later time) when the
 *			Tx buffer is not free
 * @phy_dev:		pointer to the PHY device
 * @phy_node:		pointer to the PHY device node
 * @mii_bus:		pointer to the MII bus
 * @mdio_irqs:		IRQs table for MDIO bus
 * @last_link:		last link status
 * @has_mdio:		indicates whether MDIO is included in the HW
 */
struct net_local {

	struct net_device *ndev;

	void __iomem *base_addr;
	void __iomem *bd_area;

	void __iomem *rx_frame_buf;
	void __iomem *tx_frame_buf;
	u32 rx_frame_off;
	u32 tx_frame_off;

	spinlock_t reset_lock;
	struct sk_buff *deferred_skb;

	struct phy_device *phy_dev;
	struct device_node *phy_node;

	struct mii_bus *mii_bus;
	int mdio_irqs[PHY_MAX_ADDR];

	int last_link;
	bool has_mdio;
};


typedef struct { 
    volatile u8    mach[4]; // 0: mac23_16, 1: mac31_24, 2: mac39_32, 3: mac47_40
    volatile u16   vlan;
    volatile u8    macl[2]; // 0: mac07_00, 1: mac15_08
    volatile u8    mach_mask[4];
    volatile u16   vlan_mask;
    volatile u8    macl_mask[2];
    volatile u32   info;
    volatile u32   entry_num;
    volatile u32   pad[2];
} __attribute__((packed)) tcam_entry; 

struct timer_list rx_timer;

static struct dentry *dir = 0;

static u32 yemac_debug = 0;

/*************************/
/* MacLite driver calls */
/*************************/


/**
 * yemaclite_enable_interrupts - Enable the interrupts for the MacLite device
 * @drvdata:	Pointer to the Maclite device private data
 *
 * This function enables the Tx and Rx interrupts for the Maclite device along
 * with the Global Interrupt Enable.
 */
static void yemaclite_enable_interrupts(struct net_local *drvdata)
{

}

/**
 * yemaclite_disable_interrupts - Disable the interrupts for the MacLite device
 * @drvdata:	Pointer to the Maclite device private data
 *
 * This function disables the Tx and Rx interrupts for the Maclite device,
 * along with the Global Interrupt Enable.
 */
static void yemaclite_disable_interrupts(struct net_local *drvdata)
{

}



/**
 * yemaclite_send_data - Send an Ethernet frame
 * @drvdata:	Pointer to the Maclite device private data
 * @data:	Pointer to the data to be sent
 * @byte_count:	Total frame size, including header
 *
 * This function checks if the Tx buffer of the Maclite device is free to send
 * data. If so, it fills the Tx buffer with data for transmission. Otherwise, it
 * returns an error.
 *
 * Return:	0 upon success or -1 if the buffer(s) are full.
 *
 * Note:	The maximum Tx packet size can not be more than Ethernet header
 *		(14 Bytes) + Maximum MTU (1500 bytes). This is excluding FCS.
 */
static int yemaclite_send_data(struct net_local *drvdata, u8 *data,
			       unsigned int byte_count)
{

	void __iomem *addr;
    int i;
    char msg[1024] = { 0 };
    char* p = msg;

	/* Determine the expected Tx buffer address */
	addr = drvdata->tx_frame_buf + drvdata->tx_frame_off;

	/* If the length is too large, truncate it */
	if (byte_count > YEL_MAX_FRAME_LEN)
		byte_count = YEL_MAX_FRAME_LEN;

    memcpy((u8*) addr + YEL_BD_LEN, data, byte_count); 

    if (*((u32*) addr) & 0x80000000) 
    {
        printk(KERN_INFO "TX descriptor not available\n");
    }   
    *(u32*)(addr) = 0x80000000 | (unsigned int) (byte_count);
	wmb();

    drvdata->tx_frame_off += YEL_MAX_FRAME_LEN;

    if (drvdata->tx_frame_off >= YEL_MAX_BUFFER_SIZE)
        drvdata->tx_frame_off = 0;

    if (yemac_debug & YEMAC_DEBUG_DUMP_TX)
    {
        for (i = 0; i < byte_count; i++) 
        {
            p += sprintf(p, "%02x ", data[i]);
        }
        
        printk(KERN_INFO "Len: %d TX: %s\n", byte_count, msg);
    }

	return 0;
}

/**
 * yemaclite_recv_data - Receive a frame
 * @drvdata:	Pointer to the Maclite device private data
 * @data:	Address where the data is to be received
 *
 * This function is intended to be called from the interrupt context or
 * with a wrapper which waits for the receive frame to be available.
 *
 * Return:	Total number of bytes received
 */
static u16 yemaclite_recv_data(struct net_local *drvdata, u8 *data)
{
	void __iomem *addr;
    int i = 0;
	u16 length = 0;

    char msg[1024] = { 0 };
    char* p = msg;

	/* Determine the expected buffer address */
	addr = (drvdata->rx_frame_buf + drvdata->rx_frame_off);
    

    // Check if descriptor is full
    if (*((u32*) addr) & 0x80000000) {

        length = (*((u32*) addr) & 0x1FFF);

        memcpy(data, (u8*) addr + YEL_BD_LEN + YEL_FRAME_TRAIL_PADDING, length);

         // Invalidate bd
        *((u32*) addr) &= 0x7FFFFFFF;
	    wmb();


        if (yemac_debug & YEMAC_DEBUG_DUMP_RX)
        {
            for (i = 0; i < length; i++) 
            {
                p += sprintf(p, "%02x ", data[i]);
            }

            printk(KERN_INFO "Len %d RX: %s\n", length, msg);
        }

    }

	return length;
}

/**
 * yemaclite_update_address - Update the MAC address in the device
 * @drvdata:	Pointer to the Maclite device private data
 * @address_ptr:Pointer to the MAC address (MAC address is a 48-bit value)
 *
 * Tx must be idle and Rx should be idle for deterministic results.
 * It is recommended that this function should be called after the
 * initialization and before transmission of any packets from the device.
 * The MAC address can be programmed using any of the two transmit
 * buffers (if configured).
 */
static void yemaclite_update_address(struct net_local *drvdata,
				     u8 *address_ptr)
{
	void __iomem *reg_area = drvdata->base_addr;


    *(u32*) (reg_area + 0x1004) = 0x1;
	*(u32*) (reg_area + 0x1204) = 0x1;
	*(u32*) (reg_area + 0x1304) = 0x1;
	*(u32*) (reg_area + 0x4004) = 0x1;
}

/**
 * yemaclite_set_mac_address - Set the MAC address for this device
 * @dev:	Pointer to the network device instance
 * @addr:	Void pointer to the sockaddr structure
 *
 * This function copies the HW address from the sockaddr strucutre to the
 * net_device structure and updates the address in HW.
 *
 * Return:	Error if the net device is busy or 0 if the addr is set
 *		successfully
 */
static int yemaclite_set_mac_address(struct net_device *dev, void *address)
{
	struct net_local *lp = netdev_priv(dev);
	struct sockaddr *addr = address;

	if (netif_running(dev))
		return -EBUSY;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	yemaclite_update_address(lp, dev->dev_addr);
	return 0;
}

/**
 * yemaclite_tx_timeout - Callback for Tx Timeout
 * @dev:	Pointer to the network device
 *
 * This function is called when Tx time out occurs for Maclite device.
 */
static void yemaclite_tx_timeout(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned long flags;

	dev_err(&lp->ndev->dev, "Exceeded transmit timeout of %lu ms\n",
		TX_TIMEOUT * 1000UL / HZ);

	dev->stats.tx_errors++;

	/* Reset the device */
	spin_lock_irqsave(&lp->reset_lock, flags);

	/* Shouldn't really be necessary, but shouldn't hurt */
	netif_stop_queue(dev);

	yemaclite_disable_interrupts(lp);
	yemaclite_enable_interrupts(lp);

	if (lp->deferred_skb) {
		dev_kfree_skb(lp->deferred_skb);
		lp->deferred_skb = NULL;
		dev->stats.tx_errors++;
	}

	/* To exclude tx timeout */
	dev->trans_start = jiffies; /* prevent tx timeout */

	/* We're all ready to go. Start the queue */
	netif_wake_queue(dev);
	spin_unlock_irqrestore(&lp->reset_lock, flags);
}

/**********************/
/* Interrupt Handlers */
/**********************/

/**
 * yemaclite_tx_handler - Interrupt handler for frames sent
 * @dev:	Pointer to the network device
 *
 * This function updates the number of packets transmitted and handles the
 * deferred skb, if there is one.
 */
static void yemaclite_tx_handler(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);

	dev->stats.tx_packets++;
	if (lp->deferred_skb) {
		if (yemaclite_send_data(lp,
					(u8 *) lp->deferred_skb->data,
					lp->deferred_skb->len) != 0)
			return;
		else {
			dev->stats.tx_bytes += lp->deferred_skb->len;
			dev_kfree_skb_irq(lp->deferred_skb);
			lp->deferred_skb = NULL;
			dev->trans_start = jiffies; /* prevent tx timeout */
			netif_wake_queue(dev);
		}
	}
}

/**
 * yemaclite_rx_handler- Interrupt handler for frames received
 * @dev:	Pointer to the network device
 *
 * This function allocates memory for a socket buffer, fills it with data
 * received and hands it over to the TCP/IP stack.
 */
static void yemaclite_rx_handler(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	struct sk_buff *skb;
	unsigned int align;
	u32 len;
    int i = 0; int frames = 0;

	/* Determine the expected buffer address */
	void __iomem *addr = (lp->rx_frame_buf + lp->rx_frame_off);

    
    // while descriptor is full
    while (*((u32*) addr) & 0x80000000) {

	    len = ETH_FRAME_LEN + ETH_FCS_LEN;
	    skb = netdev_alloc_skb(dev, len + ALIGNMENT);
	    if (!skb) {
		    /* Couldn't get memory. */
		    dev->stats.rx_dropped++;
		    dev_err(&lp->ndev->dev, "Could not allocate receive buffer\n");
		    return;
	    }

	    /*
	     * A new skb should have the data halfword aligned, but this code is
	     * here just in case that isn't true. Calculate how many
	     * bytes we should reserve to get the data to start on a word
	     * boundary */
	    align = BUFFER_ALIGN(skb->data);
	    if (align)
		    skb_reserve(skb, align);

	    skb_reserve(skb, 2);

	    len = yemaclite_recv_data(lp, (u8 *) skb->data);

	    if (len == 0) {
		    dev_kfree_skb_irq(skb);
		    return;
	    }

        lp->rx_frame_off += YEL_MAX_FRAME_LEN;
        if (lp->rx_frame_off >= YEL_MAX_BUFFER_SIZE)
          lp->rx_frame_off = 0;

        addr = (lp->rx_frame_buf + lp->rx_frame_off);

	    skb_put(skb, len);	/* Tell the skb how much data we got */

	    skb->protocol = eth_type_trans(skb, dev);
	    skb_checksum_none_assert(skb);

	    dev->stats.rx_packets++;
	    dev->stats.rx_bytes += len;

	    if (!skb_defer_rx_timestamp(skb))
		    netif_rx(skb);	/* Send the packet upstream */

    }


}




void rx_timer_callback( unsigned long data )
{
  
  //yemaclite_rx_handler((struct net_device *) data);

  rx_timer.expires = jiffies + msecs_to_jiffies(100);
  add_timer (&rx_timer); /* setup the timer again */
}

/**
 * yemaclite_interrupt - Interrupt handler for this driver
 * @irq:	Irq of the Maclite device
 * @dev_id:	Void pointer to the network device instance used as callback
 *		reference
 *
 * This function handles the Tx and Rx interrupts of the MacLite device.
 */
static irqreturn_t yemaclite_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;

    yemaclite_rx_handler(dev);

	return IRQ_HANDLED;
}

/**********************/
/* MDIO Bus functions */
/**********************/

/**
 * yemaclite_mdio_wait - Wait for the MDIO to be ready to use
 * @lp:		Pointer to the Maclite device private data
 *
 * This function waits till the device is ready to accept a new MDIO
 * request.
 *
 * Return:	0 for success or ETIMEDOUT for a timeout
 */

static int yemaclite_mdio_wait(struct net_local *lp)
{

	return 0;
}

/**
 * yemaclite_mdio_read - Read from a given MII management register
 * @bus:	the mii_bus struct
 * @phy_id:	the phy address
 * @reg:	register number to read from
 *
 * This function waits till the device is ready to accept a new MDIO
 * request and then writes the phy address to the MDIO Address register
 * and reads data from MDIO Read Data register, when its available.
 *
 * Return:	Value read from the MII management register
 */
static int yemaclite_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{


	return 0;
}

/**
 * yemaclite_mdio_write - Write to a given MII management register
 * @bus:	the mii_bus struct
 * @phy_id:	the phy address
 * @reg:	register number to write to
 * @val:	value to write to the register number specified by reg
 *
 * This function waits till the device is ready to accept a new MDIO
 * request and then writes the val to the MDIO Write Data register.
 */
static int yemaclite_mdio_write(struct mii_bus *bus, int phy_id, int reg, u16 val)
{


	return 0;
}

/**
 * yemaclite_mdio_setup - Register mii_bus for the Maclite device
 * @lp:		Pointer to the Maclite device private data
 * @ofdev:	Pointer to OF device structure
 *
 * This function enables MDIO bus in the Maclite device and registers a
 * mii_bus.
 *
 * Return:	0 upon success or a negative error upon failure
 */
static int yemaclite_mdio_setup(struct net_local *lp, struct device *dev)
{

	return 0;
}

/**
 * yemaclite_adjust_link - Link state callback for the Maclite device
 * @ndev: pointer to net_device struct
 *
 * There's nothing in the Maclite device to be configured when the link
 * state changes. We just print the status.
 */
static void yemaclite_adjust_link(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phy = lp->phy_dev;
	int link_state;

	/* hash together the state values to decide if something has changed */
	link_state = phy->speed | (phy->duplex << 1) | phy->link;

	if (lp->last_link != link_state) {
		lp->last_link = link_state;
		phy_print_status(phy);
	}
}

/**
 * yemaclite_open - Open the network device
 * @dev:	Pointer to the network device
 *
 * This function sets the MAC address, requests an IRQ and enables interrupts
 * for the Maclite device and starts the Tx queue.
 * It also connects to the phy device, if MDIO is included in Maclite device.
 */
static int yemaclite_open(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
    int ret = -1, retval = -1;

	/* Just to be safe, stop the device first */
	yemaclite_disable_interrupts(lp);

	if (lp->phy_node) {
		u32 bmcr;

		lp->phy_dev = of_phy_connect(lp->ndev, lp->phy_node,
					     yemaclite_adjust_link, 0,
					     PHY_INTERFACE_MODE_MII);
		if (!lp->phy_dev) {
			dev_err(&lp->ndev->dev, "of_phy_connect() failed\n");
			return -ENODEV;
		}

		/* MacLite doesn't support giga-bit speeds */
		lp->phy_dev->supported &= (PHY_BASIC_FEATURES);
		lp->phy_dev->advertising = lp->phy_dev->supported;

		/* Don't advertise 1000BASE-T Full/Half duplex speeds */
		phy_write(lp->phy_dev, MII_CTRL1000, 0);

		/* Advertise only 10 and 100mbps full/half duplex speeds */
		phy_write(lp->phy_dev, MII_ADVERTISE, ADVERTISE_ALL |
			  ADVERTISE_CSMA);

		/* Restart auto negotiation */
		bmcr = phy_read(lp->phy_dev, MII_BMCR);
		bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
		phy_write(lp->phy_dev, MII_BMCR, bmcr);

		phy_start(lp->phy_dev);
	}

	/* Set the MAC address each time opened */
	yemaclite_update_address(lp, dev->dev_addr);

	/* Grab the IRQ */
	retval = request_irq(dev->irq, yemaclite_interrupt, 0, dev->name, dev);
	if (retval) {
		dev_err(&lp->ndev->dev, "Could not allocate interrupt %d\n",
			dev->irq);
		return retval;
	}

	/* Enable Interrupts */
	yemaclite_enable_interrupts(lp);

	/* We're ready to go */
	netif_start_queue(dev);


    init_timer (&rx_timer);
    rx_timer.function = rx_timer_callback;
    rx_timer.expires = jiffies + msecs_to_jiffies(100) ;
    rx_timer.data = dev;

    add_timer (&rx_timer);


	return 0;
}

/**
 * yemaclite_close - Close the network device
 * @dev:	Pointer to the network device
 *
 * This function stops the Tx queue, disables interrupts and frees the IRQ for
 * the Maclite device.
 * It also disconnects the phy device associated with the Maclite device.
 */
static int yemaclite_close(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);

    del_timer( &rx_timer );

	netif_stop_queue(dev);
	yemaclite_disable_interrupts(lp);
	free_irq(dev->irq, dev);

	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);
	lp->phy_dev = NULL;

	return 0;
}

/**
 * yemaclite_send - Transmit a frame
 * @orig_skb:	Pointer to the socket buffer to be transmitted
 * @dev:	Pointer to the network device
 *
 * This function checks if the Tx buffer of the Maclite device is free to send
 * data. If so, it fills the Tx buffer with data from socket buffer data,
 * updates the stats and frees the socket buffer. The Tx completion is signaled
 * by an interrupt. If the Tx buffer isn't free, then the socket buffer is
 * deferred and the Tx queue is stopped so that the deferred socket buffer can
 * be transmitted when the Maclite device is free to transmit data.
 *
 * Return:	0, always.
 */
static int yemaclite_send(struct sk_buff *orig_skb, struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	struct sk_buff *new_skb;
	unsigned int len;
	unsigned long flags;

	len = orig_skb->len;

	new_skb = orig_skb;

	spin_lock_irqsave(&lp->reset_lock, flags);
	if (yemaclite_send_data(lp, (u8 *) new_skb->data, len) != 0) {
		/* If the Maclite Tx buffer is busy, stop the Tx queue and
		 * defer the skb for transmission during the ISR, after the
		 * current transmission is complete */
		netif_stop_queue(dev);
		lp->deferred_skb = new_skb;
		/* Take the time stamp now, since we can't do this in an ISR. */
		skb_tx_timestamp(new_skb);
		spin_unlock_irqrestore(&lp->reset_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&lp->reset_lock, flags);

	skb_tx_timestamp(new_skb);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += len;
	dev_consume_skb_any(new_skb);

	return 0;
}

/**
 * yemaclite_remove_ndev - Free the network device
 * @ndev:	Pointer to the network device to be freed
 *
 * This function un maps the IO region of the Maclite device and frees the net
 * device.
 */
static void yemaclite_remove_ndev(struct net_device *ndev)
{
	if (ndev) {
		free_netdev(ndev);
	}
}


static struct net_device_ops yemaclite_netdev_ops;

/**
 * yemaclite_of_probe - Probe method for the Maclite device.
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
static int yemaclite_of_probe(struct platform_device *ofdev)
{
	struct resource *res;
    struct dentry *junk;
	struct net_device *ndev = NULL;
	struct net_local *lp = NULL;
	struct device *dev = &ofdev->dev;
	const void *mac_address;
	void __iomem *reg_area;

	int rc = 0;

	dev_info(dev, "Youncta emaclite driver\n");

	/* Create an ethernet device instance */
	ndev = alloc_etherdev(sizeof(struct net_local));
	if (!ndev)
		return -ENOMEM;

	dev_set_drvdata(dev, ndev);
	SET_NETDEV_DEV(ndev, &ofdev->dev);

	lp = netdev_priv(ndev);
	lp->ndev = ndev;

	res = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	lp->base_addr = devm_ioremap_resource(&ofdev->dev, res);
	if (IS_ERR(lp->base_addr)) {
		rc = PTR_ERR(lp->base_addr);
		goto error;
	}
	ndev->mem_start = res->start;

	res = platform_get_resource(ofdev, IORESOURCE_MEM, 1);
	lp->bd_area = devm_ioremap_resource(&ofdev->dev, res);
	if (IS_ERR(lp->bd_area)) {
		rc = PTR_ERR(lp->bd_area);
		goto error;
	}


	ndev->mem_end = res->end;

	/* Get IRQ for the device */
	res = platform_get_resource(ofdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "no IRQ found\n");
		rc = -ENXIO;
		goto error;
	}

	ndev->irq = res->start;



	spin_lock_init(&lp->reset_lock);
	lp->rx_frame_buf = lp->bd_area;
	lp->rx_frame_off = 0x0;
	lp->tx_frame_buf = lp->bd_area + YEL_MAX_BUFFER_SIZE;
	lp->tx_frame_off = 0x0;


    printk(KERN_INFO, "rx bd %x, tx db %x\n", lp->rx_frame_buf, lp->tx_frame_buf);
	mac_address = of_get_mac_address(ofdev->dev.of_node);

	if (mac_address)
		/* Set the MAC address. */
		memcpy(ndev->dev_addr, mac_address, ETH_ALEN);
	else
		dev_warn(dev, "No MAC address found\n");

    reg_area = lp->base_addr;

	/* Set the MAC address in the MacLite device */
	yemaclite_update_address(lp, ndev->dev_addr);

//	lp->phy_node = of_parse_phandle(ofdev->dev.of_node, "phy-handle", 0);
//	rc = yemaclite_mdio_setup(lp, &ofdev->dev);
//	if (rc)
//		dev_warn(&ofdev->dev, "error registering MDIO bus, phy_node %p\n", lp->phy_node);

	dev_info(dev, "MAC address is now %pM\n", ndev->dev_addr);

    *(u32*)(reg_area+0x8118) = 0x00800; //Eth Rx management.
    *(u32*)(reg_area+0x8118) = 0x00801; //Eth Rx management.

    *(u32*)(reg_area+0x811C) = 0x00800; //Eth Tx management.
    *(u32*)(reg_area+0x811C) = 0x00801; //Eth Tx management. 

	ndev->netdev_ops = &yemaclite_netdev_ops;

	ndev->flags &= ~IFF_MULTICAST;
	ndev->watchdog_timeo = TX_TIMEOUT;

	/* Finally, register the device */
	rc = register_netdev(ndev);
	if (rc) {
		dev_err(dev,
			"Cannot register network device, aborting\n");
		goto error;
	}

	dev_info(dev,
		 "Youncta MacLite at 0x%08X mapped to 0x%08X, irq=%d\n",
		 (unsigned int __force)ndev->mem_start,
		 (unsigned int __force)lp->base_addr, ndev->irq);


    dir = debugfs_create_dir("yemac", 0);
    if (!dir) {
        printk(KERN_INFO "debugfs: cannot create /sys/kernel/debug/yemac\n");
    }
    
    
    junk = debugfs_create_u32("yemac_debug", 0666, dir, &yemac_debug);
    if (!junk) {
        printk(KERN_ALERT "debugfs: failed to create /sys/kernel/debug/yemac/yemac_debug\n");
    }
    
	return 0;

error:
	yemaclite_remove_ndev(ndev);
	return rc;
}

/**
 * yemaclite_of_remove - Unbind the driver from the Maclite device.
 * @of_dev:	Pointer to OF device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees any resources allocated to
 * the device.
 *
 * Return:	0, always.
 */
static int yemaclite_of_remove(struct platform_device *of_dev)
{
	struct net_device *ndev = platform_get_drvdata(of_dev);

	struct net_local *lp = netdev_priv(ndev);

	/* Un-register the mii_bus, if configured */
	if (lp->has_mdio) {
		mdiobus_unregister(lp->mii_bus);
		kfree(lp->mii_bus->irq);
		mdiobus_free(lp->mii_bus);
		lp->mii_bus = NULL;
	}

	unregister_netdev(ndev);

	of_node_put(lp->phy_node);
	lp->phy_node = NULL;

	yemaclite_remove_ndev(ndev);

	return 0;
}

/**
 * yemaclite_ioctl 
 *
 * Return:	
 */

static int yemaclite_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct net_local *lp = netdev_priv(dev);
    __iomem u32 *tcam_addr_r = lp->base_addr + 0xD000;
    __iomem u32 *tcam_addr_w = lp->base_addr + 0xC000;    
   
    tcam_entry tcam;
    int i, ret;

    u32* p_tcam = (u32*) &tcam;

    u8* data = (u8*) (ifr->ifr_data);

    switch (cmd) 
    {
        case SIOCDEVDUMPTCAM:
            
            for (i = 0; i < 16; i++)
            {

                *(p_tcam + 0) = *(tcam_addr_r + 0 + 8*i);
                *(p_tcam + 1) = *(tcam_addr_r + 1 + 8*i);
                *(p_tcam + 2) = *(tcam_addr_r + 2 + 8*i);
                *(p_tcam + 3) = *(tcam_addr_r + 3 + 8*i);
                *(p_tcam + 4) = *(tcam_addr_r + 4 + 8*i);
                *(p_tcam + 5) = 0;
                *(p_tcam + 6) = 0;
                *(p_tcam + 7) = 0;

                ret = copy_to_user(data, &tcam, sizeof(tcam_entry));
                tcam.entry_num = i;
                data += sizeof(tcam_entry);

                //printk(KERN_INFO "MAC      %02x%02x%02x%02x%02x%02x VLAN      %04x\n", tcam.mac[5], tcam.mac[4], tcam.mac[3], tcam.mac[2], tcam.mac[1], tcam.mac[0], tcam.vlan);
                //printk(KERN_INFO "MAC MASK %02x%02x%02x%02x%02x%02x VLAN MASK %04x\n", tcam.mac_mask[5], tcam.mac_mask[4], tcam.mac_mask[3], tcam.mac_mask[2], tcam.mac_mask[1], tcam.mac_mask[0], tcam.vlan_mask);               

            }

        break;
        case SIOCDEVWRITETCAMENTRY:

            ret = copy_from_user(&tcam, data, sizeof(tcam_entry));
            i = tcam.entry_num;            

            *(tcam_addr_w + 0 + 8*i) = *(p_tcam + 0);
            *(tcam_addr_w + 1 + 8*i) = *(p_tcam + 1);
            *(tcam_addr_w + 2 + 8*i) = *(p_tcam + 2);
            *(tcam_addr_w + 3 + 8*i) = *(p_tcam + 3);
            *(tcam_addr_w + 4 + 8*i) = *(p_tcam + 4);


        break;
        default:
        break;
    }


    return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void
yemaclite_poll_controller(struct net_device *ndev)
{
	disable_irq(ndev->irq);
	yemaclite_interrupt(ndev->irq, ndev);
	enable_irq(ndev->irq);
}
#endif

static struct net_device_ops yemaclite_netdev_ops = {
	.ndo_open		        = yemaclite_open,
	.ndo_stop		        = yemaclite_close,
	.ndo_start_xmit		    = yemaclite_send,
	.ndo_set_mac_address	= yemaclite_set_mac_address,
	.ndo_tx_timeout		    = yemaclite_tx_timeout,
    .ndo_do_ioctl           = yemaclite_ioctl,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = yemaclite_poll_controller,
#endif
};

/* Match table for OF platform binding */
static struct of_device_id yemaclite_of_match[] = {
	{ .compatible = "yemacl", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, yemaclite_of_match);

static struct platform_driver yemaclite_of_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = yemaclite_of_match,
	},
	.probe		= yemaclite_of_probe,
	.remove		= yemaclite_of_remove,
};

module_platform_driver(yemaclite_of_driver);

MODULE_AUTHOR("Youncta, Inc.");
MODULE_DESCRIPTION("Youncta Ethernet MAC Lite driver");
MODULE_LICENSE("GPL");
