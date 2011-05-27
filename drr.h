#ifndef __DRR_H
#define __DRR_H

#include <linux/genhd.h>

#define DRR_NQS 8
#define DRR_MINORS DRR_NQS
#define DRR_QNAME_SIZE 16

struct drr_dev_t {
    spinlock_t lock;                /* for mutual exclusion */
    struct request_queue *queue;
    struct gendisk *gd;
};

#endif
