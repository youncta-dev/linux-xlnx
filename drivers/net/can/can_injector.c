#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/hardirq.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/ioctl.h>
#include <linux/completion.h>
#include <linux/skbuff.h>

#include "can_injector.h"

MODULE_LICENSE("GPL");

#define PKT_LEN	128

static unsigned int caninj_debug = 0;
static int caninj_major = CANINJ_MAJOR;

struct packet_type fast_can_proto;

static struct timer_list my_timer;    
static int my_timer_counter = 0;
static int my_timer_running = 0;
     
caninj_dev_t *caninj_device; 

static struct workqueue_struct *my_wq;
 
struct work_data {
    struct work_struct work;
    void* data;
};
static struct work_data* my_work;

static struct net_device* net_dev;



/* our packet handler */
int caninj_pkt_func(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *net) 
{
    unsigned long flags;
    caninj_dev_t* device = (caninj_dev_t*) dev;
    
    SPIN_LOCK_IRQSAVE(&(device->lock_rx), flags);

    if (skb->data) 
    {

        caninj_debug = CANINJ_PKT_DUMP_RX;
        // Flow is configured
        if (caninj_debug & CANINJ_PKT_DUMP_RX)
        {       
            int k;
            char buf[256];
            char* p = buf;
            p += sprintf(p, "datarx ");
            for (k = 0; k < 64; k++)
                p += sprintf(p, "%02x ", skb->data[k]);             
            p += sprintf(p, "\n");
            printk(buf);
        }    

    }
                
	kfree_skb(skb);
    SPIN_UNLOCK_IRQRESTORE(&(device->lock_rx), flags);
    
	return 0;
}


static void my_wq_function(struct work_struct *work)
{

  struct work_data * work_data = (struct work_data *)work;
  caninj_dev_t* dev = (caninj_dev_t*) work_data->data;
  struct sk_buff *skb;
  unsigned long flags;  

  if (caninj_debug & CANINJ_MISC)
      printk("WQ function %d\n", __LINE__);   
  

  if (caninj_debug & CANINJ_MISC)
      printk("About to send %d\n", __LINE__);   


  skb = dev_alloc_skb(PKT_LEN);

  //skb->nh.raw = skb->data;

  uint8_t* p = (uint8_t*) skb_put(skb, 8); 

  p[0] = 0xab;
  p[1] = 0xcd;
  p[2] = 0xef;
  p[3] = 0x33;


  skb->dev = net_dev;
  skb->protocol = __constant_htons(ETH_P_CAN);
  skb->ip_summed = CHECKSUM_NONE;

  //dev_queue_xmit(skb);
  //dev_put(net_dev);

  if (caninj_debug & CANINJ_PKT_DUMP_TX)
  {       
        int k;
        char buf[256];
        char* p = buf;
        p += sprintf(p, "datatx ");
        for (k = 0; k < 64; k++)
            p += sprintf(p, "%02x ", skb->data[k]);             
        p += sprintf(p, "\n");
        printk(buf);
        //printk("Sent packet for flow: %d to %02x:%02x:%02x:%02x:%02x:%02x\n", pFlow->flow_id, pFlow->dst_mac[0], pFlow->dst_mac[1], pFlow->dst_mac[2], pFlow->dst_mac[3], pFlow->dst_mac[4], pFlow->dst_mac[5]);                        
  }

 
  return;
}

static void my_timer_func(unsigned long ptr)
{    
    int ret;

/*
    if (my_timer_counter == 0)
        printk("timer %d %d\n", __LINE__, jiffies);   
    my_timer_counter = (my_timer_counter + 1) % 50;
*/    
        
    ret = queue_work(my_wq, &my_work->work );
    if (ret == 0)
        printk("%s %d Work already on queue\n", __FILE__, __LINE__);
    

	/* initialize timer */
	init_timer( &my_timer );

    my_timer.function = my_timer_func;
    my_timer.data = (unsigned long) caninj_device;
    my_timer.expires = jiffies + (2*HZ);
    add_timer(&my_timer);    

}

/*
* Open and close
*/

int caninj_open (struct inode *inode, struct file *filp)
{ 
    return 0;          /* success */
}

int caninj_release (struct inode *inode, struct file *filp)
{
   caninj_dev_t *dev; /* device information */

   /*  Find the device */
   dev = container_of(inode->i_cdev, caninj_dev_t, cdev);

   return 0;
}

/*
* Data management: read and write
*/

ssize_t caninj_read (struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
        ssize_t retval = 0;
        return retval;
}


long caninj_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{

	int err = 0, ret = 0;
    unsigned long flags;
    caninj_dev_t* dev = container_of(filp->f_mapping->host->i_cdev, caninj_dev_t, cdev);
    
    /* don't even decode wrong cmds: better returning  ENOTTY than EFAULT */
    if (_IOC_TYPE(cmd) != CANINJ_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > CANINJ_IOC_MAXNR) return -ENOTTY;

    /*
     * the type is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. Note that the type is user-oriented, while
     * verify_area is kernel-oriented, so the concept of "read" and
     * "write" is reversed
     */
    if (_IOC_DIR(cmd) & _IOC_READ)
            err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
            err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
            return -EFAULT;

    switch(cmd) 
    {
      case CANINJ_IOCT_DEBUG:
        if (down_interruptible (&(dev->sem)))
          return -ERESTARTSYS;
        caninj_debug = UPDATE_BITS(caninj_debug, (arg >> 8), arg & 0xff) ;
        printk("Flowmng debug status: %08x\n", caninj_debug);
        up (&(dev->sem));                
        break;
                                      
      default:  /* redundant, as cmd was checked against MAXNR */
        return -ENOTTY;
    }

    return ret;
}

struct file_operations caninj_fops = {
        .owner =                THIS_MODULE,
        .read =                 caninj_read,
        //.write =              caninj_write,
        .unlocked_ioctl =       caninj_ioctl,
        .open =                 caninj_open,
        .release =              caninj_release,
};

static void caninj_setup_cdev(caninj_dev_t *dev)
{
    int err, devno = MKDEV(caninj_major, 0);
    
    cdev_init(&dev->cdev, &caninj_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &caninj_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    /* Fail gracefully if need be */
    if (err)
      printk(KERN_NOTICE "Error %d adding scull0", err);
}



static int __init caninj_init(void)
{
  struct timeval time;  
  int result;
  dev_t dev = MKDEV(caninj_major, 0);

  printk(KERN_NOTICE "caninj_init major %d", caninj_major);

  if ((result = register_chrdev_region(dev, 1, "caninj")) < 0)
    return result;

  // Find network device

  net_dev = dev_get_by_name(&init_net, (const char*) "can0");

  if (net_dev)
  {
    printk("ifIndex %d\n", net_dev->ifindex);
  }

  printk(KERN_NOTICE "caninj_init %p", net_dev);

  /*
   * Set up the device
   */
   
  caninj_device = kmalloc(sizeof (caninj_dev_t), GFP_KERNEL);
  if (!caninj_device) {
          result = -ENOMEM;
          goto fail;
  }
  memset(caninj_device, 0, sizeof (caninj_dev_t));

  sema_init (&(caninj_device->sem), 1);        
  caninj_setup_cdev(caninj_device);
  spin_lock_init(&caninj_device->lock_tx);
  spin_lock_init(&caninj_device->lock_rx);


  // Injecting workqueue

  my_wq = create_workqueue("my_workqueue");
  if (my_wq) {

    /* Prepare some work*/
    my_work = (struct work_data*) kmalloc(sizeof(struct work_data), GFP_KERNEL);
    if (my_work) {
      my_work->data = (void *) caninj_device;


      INIT_WORK(&(my_work->work), (void (*)(struct work_struct *)) my_wq_function);

    }

  }

   
  init_timer(&my_timer);
  my_timer.function = my_timer_func;
  my_timer.data = (unsigned long) caninj_device;
  
  do_gettimeofday(&time);

  my_timer.expires = jiffies + (2*HZ);
  add_timer(&my_timer);


  fast_can_proto.dev = NULL;

  fast_can_proto.type = htons(ETH_P_CAN | (0x01 << 8));

  fast_can_proto.func = caninj_pkt_func;
  dev_add_pack(&fast_can_proto); 
  
  return 0;

fail:
  unregister_chrdev_region(dev, 1);
  return result;  
}

static void __exit caninj_cleanup(void)
{
  dev_remove_pack(&fast_can_proto);
  
  del_timer(&my_timer);
  
  flush_workqueue(my_wq);

  destroy_workqueue(my_wq);
  
  cdev_del(&caninj_device->cdev);
  kfree(caninj_device);

  unregister_chrdev_region(MKDEV (caninj_major, 0), 1);

  return;
}



module_init(caninj_init);
module_exit(caninj_cleanup);

