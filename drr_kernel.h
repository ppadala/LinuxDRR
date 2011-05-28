#ifndef __DRR_KERNEL_H
#define __DRR_KERNEL_H

#include "drr.h"

#include <linux/genhd.h>

/* queue to hold bios for each DRRQ */
struct drrq {
    struct bio *bio;
    struct drrq *next;
};

#define DRR_MAX_CREDIT 100 /* 100 outstanding requests allowed at once */

struct drr_dev_t {
    spinlock_t lock;                /* for mutual exclusion */
    struct request_queue *queue;
    struct gendisk *gd;

    /* backing device info */
	struct file *backing_file;
	struct block_device *backing_dev;
	char backing_name[BDEVNAME_SIZE];

    /* queue head and tail */
    struct drrq *qhead;
    struct drrq *qtail;

    /* current credit */
    atomic_t credit;
};

#endif
