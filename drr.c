#include <linux/module.h> 
#include <linux/fs.h>     /* register_blk_dev */
#include <linux/string.h> /* memset */
#include <linux/blkdev.h> /* blk_init_queue, requeust_queue_t */
#include <linux/file.h>   /* fget */

#include "drr_kernel.h"

static int drr_major = 0;
static struct drr_dev_t drr_dev[DRR_MINORS];

static int drr_open(struct block_device *bdev, fmode_t mode)
{
    printk("open called\n");
    return 0;
}

/* upcall from block device to DRR driver */
static void drr_bio_end_io(struct bio *bio, int error)
{
	struct bio *drr_bio = (struct bio *)bio->bi_private;

    printk(KERN_INFO "drr_bio_end_io: called for %lu sector\n", bio->bi_sector);
    bio_endio(drr_bio, error); /* signal that our bio is also done */
	bio_put(bio);
}

static int drr_pass_bio(struct drr_dev_t *dev, struct bio *bio)
{
    struct bio *backing_bio;

    backing_bio = bio_clone(bio, GFP_KERNEL);

    if(backing_bio == NULL)
        return -ENOMEM; /* TODO: handle this in top layer */

    if (dev->backing_dev == NULL) {
        printk(KERN_ERR "Got I/O request before SET_FD command\n");
        return -ENOTTY;
    }
    else {
        backing_bio->bi_bdev = dev->backing_dev;
        backing_bio->bi_end_io = drr_bio_end_io;
        backing_bio->bi_private = bio;
        generic_make_request(backing_bio);
    }

    return 0;
}

static int drr_make_request(struct request_queue *q, struct bio *bio)
{
    struct drr_dev_t *dev = q->queuedata;
    
    printk(KERN_INFO "drr_make_request: called for %lu sector\n", bio->bi_sector);
    drr_pass_bio(dev, bio); 
    /* If we don't return zero here, upper layers keep re-trying, wierd */
    return 0;
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

static int drr_setup_vbd( struct drr_dev_t *dev, int which)
{   
    memset(dev, 0, sizeof(struct drr_dev_t));

    spin_lock_init(&dev->lock);   /* this must be done before initializing req Q */

    dev->queue = blk_alloc_queue(GFP_KERNEL);
    if(dev->queue == NULL) {
        printk(KERN_WARNING "DRR: cannot create requeust queue");
        goto error_init_queue;
    }
    dev->queue->queuedata = dev;

    /* By-passing the request queue and use stacking driver approach */
    blk_queue_make_request(dev->queue, drr_make_request);

    atomic_set(&dev->credit,  DRR_MAX_CREDIT);

    dev->qtail = NULL;
    dev->qhead = NULL;

    dev->gd = alloc_disk(1);
    if(!dev->gd) {
        printk(KERN_NOTICE "alloc_disk failure\n");
        goto error_disk_alloc;
    }
    dev->gd->major = drr_major;
    dev->gd->first_minor = which;
    dev->gd->fops = &drr_fops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    snprintf(dev->gd->disk_name, 32, "drr%c", which + 'a');

    /* this is important. If capacity is set to anything else,
       make_request will be called and if we don't have a backing device
       bad things will happen */
    set_capacity(dev->gd, 0);
    add_disk(dev->gd);
    return 0;

error_disk_alloc:
    blk_cleanup_queue(dev->queue);
error_init_queue:
    return -EBUSY;
}

static int __init drr_init( void )
{   int error = 0, i;

    drr_major = register_blkdev(drr_major, "drr");
    if(drr_major <= 0) {
        printk(KERN_WARNING "DRR: unable to get major number");
        error = -EBUSY;
        goto error_register;
    }

    for(i = 0;i < DRR_MINORS; ++i) 
        if(drr_setup_vbd(&drr_dev[i], i) < 0)
            goto error_init_vbd; /* TODO: fix all the partial setups to be undone */

    return 0;

error_init_vbd:
    unregister_blkdev(drr_major, "drr");
error_register:
    return error;
}

static void __exit drr_exit( void )
{   int i;
    struct drr_dev_t *dev;

    for(i = 0; i < DRR_MINORS; ++i) {
        dev = &drr_dev[i];
        del_gendisk(dev->gd);
        put_disk(dev->gd);
        blk_cleanup_queue(dev->queue);
    }
    unregister_blkdev(drr_major, "drr");
    printk("<1>Goodbye cruel world\n");
}

module_init( drr_init );
module_exit( drr_exit );

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pradeep Padala <ppadala@gmail.com>");
MODULE_DESCRIPTION("DRR I/O Scheduler for Linux"); 
