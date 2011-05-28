/* grave yard for unused functions, which may come up later */
/* Function for handling requeust queue */
/* NOT USED for stacking driver approach */
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
