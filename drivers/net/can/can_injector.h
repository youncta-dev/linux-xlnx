#ifndef CANINJ_H
#define CANINJ_H

#include <linux/if_vlan.h>

#define CANINJ_MAJOR    231

/* Use 'P' as magic number */
#define CANINJ_IOC_MAGIC  'P'

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly
 * G means "Get" (to a pointed var)
 * Q means "Query", response is on the return value
 * X means "eXchange": G and S atomically
 * H means "sHift": T and Q atomically
 */

#define CANINJ_IOCG_INFO               _IOR(CANINJ_IOC_MAGIC,  0, int)
#define CANINJ_IOCS_INFO               _IOW(CANINJ_IOC_MAGIC,  1, int)
#define CANINJ_IOCT_DEBUG		       _IOW(CANINJ_IOC_MAGIC,  2, int)

#define CANINJ_IOC_MAXNR            2


#define UPDATE_BITS(act,val,mask)	(((act) | ((val) & (mask))) & ((val) | ~(mask)))
#define CANINJ_PKT_DUMP_RX         0x00000001
#define CANINJ_PKT_DUMP_TX         0x00000002
#define CANINJ_MISC                0x00000004
#define CANINJ_DEBUG_SPINLOCKS     0x00000008


#ifdef __KERNEL__ 



#include <linux/cdev.h>


typedef struct caninj_dev_s
{
  void **data;
  struct cdev cdev;
  struct semaphore sem;     /* Mutual exclusion */	
  spinlock_t lock_rx;	
  spinlock_t lock_tx;	
     
} caninj_dev_t;


#define SPIN_LOCK_IRQSAVE(x, y)             do { if (caninj_debug & CANINJ_DEBUG_SPINLOCKS) printk("%d\t %s spin_lock\n", __LINE__, #x); spin_lock_irqsave((x), (y)); } while (0);
#define SPIN_UNLOCK_IRQRESTORE(x, y)          do { if (caninj_debug & CANINJ_DEBUG_SPINLOCKS) printk("%d\t %s spin_unlock\n", __LINE__, #x); spin_unlock_irqrestore((x), (y)); } while (0);


#endif

#endif

