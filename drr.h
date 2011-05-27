#ifndef __DRR_H
#define __DRR_H

#include <linux/ioctl.h>

#define DRR_NQS 8
#define DRR_MINORS DRR_NQS
#define DRR_QNAME_SIZE 16

#define DRR_IOCTL_MAGIC 0xDE
#define DRR_SET_BACKING_DEVICE _IOR(DRR_IOCTL_MAGIC, 1, int)

#endif
