#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

int main()
{   int fd, ret;
    char buf[512];

    fd = open("/dev/drra", O_WRONLY);
    if(fd < 0) {
        perror("");
        exit(-1);
    }
    strcpy(buf, "hello world\n");
    ret = write(fd, buf, strlen(buf));
    if(ret != strlen(buf)) {
        perror("");
    }
    close(fd);
}
