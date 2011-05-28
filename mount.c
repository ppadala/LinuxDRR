/* Program to connect the DRRQ to a backing device */
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "drr.h"

int main(const int argc, const char **argv)
{
    const char *drrq_name, *backing_name;
    int drrq_fd, backing_fd;


    /* TODO: check parameter length */ 
    drrq_name = argv[1];
    backing_name = argv[2];
	
    backing_fd = open(backing_name, O_RDWR);
    if (backing_fd < 0) {
        perror("");
        exit(-1);
    }
	
    printf("Backing device fd = %d\n", backing_fd);
    
    drrq_fd = open(drrq_name, 0);
    if (drrq_fd < 0) {
        perror("");
        close(backing_fd);
        exit(-1);
    }
	printf("DRRQ fd = %d\n", drrq_fd);
    
    if (ioctl(drrq_fd, DRR_SET_BACKING_DEVICE, backing_fd) < 0) {
        perror("");
    }
    close(backing_fd);
    close(drrq_fd);
}
