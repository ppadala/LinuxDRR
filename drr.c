#include <linux/module.h> 
#include <linux/fs.h>     /* register_blk_dev */
#include <linux/string.h> /* memset */
#include <linux/blkdev.h> /* blk_init_queue, requeust_queue_t */
#include <linux/file.h>   /* fget */

#include "drr_kernel.h"

static int drr_major = 0;
static struct drr_dev_t *drr_dev;

static int drr_open(struct block_device *bdev, fmode_t mode)
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
        struct drr_dev_t *dev = (struct drr_dev *)req->rq_disk->private_data;

        unsigned long start = blk_rq_pos(req) << 9;
        unsigned long len  = blk_rq_cur_bytes(req);
        
        printk("start = %lu len = %lu\n", start, len);
        printk("Buffer contains = %s\n", req->buffer);

/*        if(start + len > get_capacity(dev->gd)) /* out of bounds */
/*            __blk_end_request_cur(req, 0); /* end it right there */

        if(! __blk_end_request_cur(req, 0)) {
            req = blk_fetch_request(q);
        }
    }

    printk("drr_request finished\n");
}

static int drr_set_backing_fd(struct drr_dev_t *dev, unsigned long backing_fd)
{
    int error = 0;
	struct inode *backing_inode;
	struct gendisk *backing_disk;
	loff_t backing_size;

	/* TODO: Need to protect from changes, while the vbd is active */
    /* TODO: Error check, if we already having backing device set */

	dev->backing_file = fget(backing_fd);
	if (dev->backing_file == NULL) {
		error = -EBADF;
		goto error_fget;
	}

	backing_inode = dev->backing_file->f_mapping->host;
	if (!S_ISBLK(backing_inode->i_mode)) {
		printk(KERN_ERR "Unable to use non-block device as backing device\n");
		error = -ENOTBLK;
		goto error_isblk;
	}

	dev->backing_dev = backing_inode->i_bdev;
	bdevname(dev->backing_dev, dev->backing_name);

	backing_disk = dev->backing_dev->bd_disk;
	if (backing_disk->queue != NULL) {
		blk_queue_stack_limits(dev->queue, backing_disk->queue);
	}
	backing_size = i_size_read(backing_inode);
    /* set our vbd capacity to the same as backing device */
	set_capacity(dev->gd, backing_size / 512);

    /* umm, fops is read-only? */
	//dev->gd->fops->media_changed = backing_disk->fops->media_changed;
	//dev->gd->fops->revalidate_disk = backing_disk->fops->revalidate_disk;

    printk("Added backing device %s to %s, size %lld bytes\n", 
            dev->backing_name, dev->gd->disk_name, backing_size);
	return 0;

error_isblk:
	fput(dev->backing_file);
	dev->backing_file = NULL;
error_fget:
	return error;
}

static int drr_ioctl(struct block_device *bdev, fmode_t mode, 
                     unsigned int cmd, unsigned long arg)
{
    struct drr_dev_t *dev = bdev->bd_disk->private_data;
    int error = 0;

	spin_lock(&dev->lock);
    switch(cmd) {
        case DRR_SET_BACKING_DEVICE:
            /* arg contains the backing device's fd */
            error = drr_set_backing_fd(dev, arg);
            break;
        default:
            error = -EINVAL;
    }
	
    spin_unlock(&dev->lock);
    return error;
}

static struct block_device_operations drr_fops  = {
    .owner = THIS_MODULE,
    .open = drr_open,
    .ioctl = drr_ioctl,
};

static int __init drr_init( void )
{   int error = 0;
    struct drr_dev_t *dev;

    drr_dev = kmalloc(sizeof(struct drr_dev_t), GFP_KERNEL);
    dev = drr_dev;
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
    del_gendisk(drr_dev->gd);
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
