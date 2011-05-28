#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "drr.h"

int main(int argc, char *argv[])
{
    const char *dev_name = argv[1];
    int weight = atoi(argv[2]);
    int dev_fd;

    dev_fd = open(dev_name, O_RDWR);
    if (dev_fd < 0) {
        perror("");
        exit(-1);
    }
	
    if (ioctl(dev_fd, DRR_SET_WEIGHT, weight) < 0) {
        perror("");
    
	}
    close(dev_fd);
}
