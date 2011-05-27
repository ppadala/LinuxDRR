#include <linux/module.h> 
#include <linux/fs.h>     /* register_blk_dev */
#include <linux/string.h> /* memset */
#include <linux/blkdev.h> /* blk_init_queue, requeust_queue_t */

#include "drr.h"

static int drr_major = 0;
static struct drr_dev_t *drr_dev;

static int drr_open(struct block_device *dev, fmode_t mode)
{
    printk("open called\n");
    return 0;
}

/* Function for handling requeust queue */
static void drr_request(struct request_queue *q)
{
    struct request *req;

    printk("drr_request called\n");

    req = blk_fetch_request(q);
    while(req != NULL) {
        //struct drr_dev *dev = req->rq_disk->private_data;

        unsigned long start = blk_rq_pos(req) << 9;
        unsigned long len  = blk_rq_cur_bytes(req);

        printk("start = %lu len = %lu\n", start, len);
        printk("Buffer contains = %s\n", req->buffer);

        if(! __blk_end_request_cur(req, 0)) {
            req = blk_fetch_request(q);
        }
    }

    printk("drr_request finished\n");
}

static const struct block_device_operations drr_fops  = {
    .owner = THIS_MODULE,
    .open = drr_open,
};

static int __init drr_init( void )
{   int error = 0;
    struct drr_dev_t *dev = drr_dev;

    dev = kmalloc(sizeof(struct drr_dev_t), GFP_KERNEL);
    if(!dev)
        return -ENOMEM;
    
    drr_major = register_blkdev(drr_major, "drr");
    if(drr_major <= 0) {
        printk(KERN_WARNING "DRR: unable to get major number");
        error = -EBUSY;
        goto error_register;
    }

    memset(dev, 0, sizeof(struct drr_dev_t));
    spin_lock_init(&dev->lock);              /* this must be done before initializing req Q */
    dev->queue = blk_init_queue(drr_request, &dev->lock);

    if(dev->queue == NULL) {
        printk(KERN_WARNING "DRR: cannot create requeust queue");
        error = -EBUSY;
        goto error_init_queue;
    }

    dev->gd = alloc_disk(DRR_MINORS);
    if(!dev->gd) {
        printk(KERN_NOTICE "alloc_disk failure\n");
        error = -EBUSY;
        goto error_disk_alloc;
    }
    dev->gd->major = drr_major;
    dev->gd->first_minor = 0;
    dev->gd->fops = &drr_fops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    snprintf(dev->gd->disk_name, 32, "drra");
    set_capacity(dev->gd, 4096);
    add_disk(dev->gd);

    return 0;

error_disk_alloc:
    blk_cleanup_queue(dev->queue);
error_init_queue:
    unregister_blkdev(drr_major, "drr");
error_register:
    kfree(dev);
    return error;

}

static void __exit drr_exit( void )
{
    put_disk(drr_dev->gd);
    blk_cleanup_queue(drr_dev->queue);
    unregister_blkdev(drr_major, "drr");
    kfree(drr_dev);
    printk("<1>Goodbye cruel world\n");
}

module_init( drr_init );
module_exit( drr_exit );

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pradeep Padala <ppadala@gmail.com>");
MODULE_DESCRIPTION("DRR I/O Scheduler for Linux"); 
