#include <linux/module.h> 
#include <linux/fs.h>     /* register_blk_dev */
#include <linux/string.h> /* memset */
#include <linux/blkdev.h> /* blk_init_queue, requeust_queue_t */
#include <linux/file.h>   /* fget */

#include "drr_kernel.h"

static int drr_major = 0;
static struct drr_dev_t drr_dev[DRR_MINORS];

static struct workqueue_struct *drr_workqueue = NULL;
struct work_struct work[DRR_MAX_WORK_THREADS];
int curr_workq = 0;
/* lock to synchronize the worker threads, 
   dev->lock is not enough, as workers run for all devs */
static spinlock_t worker_lock; 

static int drr_emptyq(struct drr_dev_t *dev);

static int drr_open(struct block_device *bdev, fmode_t mode)
{
    /* refcount? */
    return 0;
}

/* upcall from block device to DRR driver */
static void drr_bio_end_io(struct bio *bio, int error)
{
	struct bio *drr_bio = (struct bio *)bio->bi_private;
    struct drr_dev_t *dev = (struct drr_dev_t *) drr_bio->bi_bdev->bd_disk->private_data;

    printk(KERN_INFO "drr_bio_end_io: called for %d sector\n", (int)bio->bi_sector);
    bio_endio(drr_bio, error); /* signal that our bio is also done */
	bio_put(bio);

    atomic_inc(&dev->credit);
    if(atomic_read(&dev->credit) == DRR_MAX_CREDIT)
        atomic_set(&dev->credit, DRR_MAX_CREDIT);
    printk("dev->credit = %d\n", atomic_read(&dev->credit));

    /* queue work so that drr_make_request can continue if there are any requests */
    if(!drr_emptyq(dev)) {
        printk("launching workq %d\n", curr_workq);
        if(queue_work(drr_workqueue, &(work[curr_workq]))) {
            ++curr_workq;
            if(curr_workq == DRR_MAX_WORK_THREADS)
                curr_workq = 0;
        }
    }
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
        atomic_dec(&dev->credit);
        backing_bio->bi_bdev = dev->backing_dev;
        backing_bio->bi_end_io = drr_bio_end_io;
        backing_bio->bi_private = bio;
        generic_make_request(backing_bio);
    }

    return 0;
}

static void printq(struct drr_dev_t *dev)
{
    struct drrq *entry = dev->qhead;

    printk("head = %p ## ", dev->qhead);
    while(entry != NULL) {
        printk("%p -> ", entry);
        entry = entry->next;
    }

    printk("NULL ##");
    printk(" tail = %p\n", dev->qtail);

}

static struct bio *drr_dequeue(struct drr_dev_t *dev)
{
    struct drrq *temp = dev->qhead;
    struct bio *bio = temp->bio;
  
    printq(dev);
    if(dev->qhead == NULL) {
        printk("Q is NULL, should never come here\n");
        return NULL;
    }

    dev->qhead = dev->qhead->next;
    if(dev->qhead == NULL) /* empty queue */
        dev->qtail = NULL;

    kfree(temp);
    printq(dev);

    return bio;
}

static int drr_enqueue(struct drr_dev_t *dev, struct bio *bio)
{
    struct drrq *ior = kmalloc(sizeof(struct drrq), GFP_KERNEL); /* TODO: error check */
    
    printq(dev);
    ior->bio = bio;
    ior->next = NULL;

	if (dev->qtail != NULL) {
		dev->qtail->next = ior;
		dev->qtail = ior;
	}
	else {
        printk("Enqueueing the first one \n");
		dev->qhead = dev->qtail = ior;
	}
    printq(dev);

    return 0;
}

static int drr_emptyq(struct drr_dev_t *dev)
{
    return (dev->qhead == NULL) && (dev->qtail == NULL) ? 1: 0;
}

static void drr_workqueue_handler(struct work_struct *wk)
{   unsigned long flags;
    int i;
    struct drr_dev_t * dev;
    struct bio *bio;

    for(i = 0; i < DRR_MINORS; ++i) {
        dev = &drr_dev[i];
        /* if there are credits, dequeue */
        if(!drr_emptyq(dev) && atomic_read(&dev->credit) > 0) { 
            printk("workqueue_handler: dequeuing from queue %d: \n", i);
            spin_lock_irqsave(&worker_lock, flags); 
            bio = drr_dequeue(dev);
            drr_pass_bio(dev, bio); 
            spin_unlock_irqrestore(&worker_lock, flags);
            break; /* could continue, but let other worker threads 
                      pick up the work too */
        }
    }
}


static int drr_make_request(struct request_queue *q, struct bio *bio)
{
    struct drr_dev_t *dev = q->queuedata;
    unsigned long flags;

    printk(KERN_INFO "drr_make_request: called for %d sector\n", (int)bio->bi_sector);
    printk("dev->credit = %d\n", atomic_read(&dev->credit));
    
    spin_lock_irqsave(&worker_lock, flags);
    //bio->bi_private = dev; /* this is causing hard reset, why? */

    drr_enqueue(dev, bio);
    if(atomic_read(&dev->credit) > 0) { /* if there are credits, dequeue */
        struct bio *new_bio;
        new_bio = drr_dequeue(dev);
        drr_pass_bio(dev, new_bio); 
    }
    spin_unlock_irqrestore(&worker_lock, flags);

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

    drr_workqueue = create_singlethread_workqueue("drrq-bh");
	if (drr_workqueue == NULL) {
		printk(KERN_ERR "Unable to allocate workqueue\n");
		error = -ENOMEM;
		goto error_workqueue;
	}

    drr_major = register_blkdev(drr_major, "drr");
    if(drr_major <= 0) {
        printk(KERN_WARNING "DRR: unable to get major number");
        error = -EBUSY;
        goto error_register;
    }
    
	spin_lock_init(&worker_lock);

    for(i = 0;i < DRR_MAX_WORK_THREADS; ++i)
        INIT_WORK(&(work[i]), drr_workqueue_handler);

    for(i = 0;i < DRR_MINORS; ++i) {
        if(drr_setup_vbd(&drr_dev[i], i) < 0)
            goto error_init_vbd; /* TODO: fix all the partial setups to be undone */
    }

    return 0;

error_init_vbd:
    unregister_blkdev(drr_major, "drr");
error_register:
    destroy_workqueue(drr_workqueue);
error_workqueue:
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
    flush_workqueue(drr_workqueue);
	destroy_workqueue(drr_workqueue);
    unregister_blkdev(drr_major, "drr");
    printk("Goodbye cruel world\n");
}

module_init( drr_init );
module_exit( drr_exit );

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pradeep Padala <ppadala@gmail.com>");
MODULE_DESCRIPTION("DRR I/O Scheduler for Linux"); 
