#ifndef __DRR_KERNEL_H
#define __DRR_KERNEL_H

#include "drr.h"

#include <linux/genhd.h>

struct drr_dev_t {
    spinlock_t lock;                /* for mutual exclusion */
    struct request_queue *queue;
    struct gendisk *gd;

    /* backing device info */
	struct file *backing_file;
	struct block_device *backing_dev;
	char backing_name[BDEVNAME_SIZE];
	
};

#endif
