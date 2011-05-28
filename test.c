#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

int main()
{   int fd, ret;
    char buf[512], newbuf[512];

    fd = open("/dev/drra", O_WRONLY);
    if(fd < 0) {
        perror("");
        exit(-1);
    }
    strcpy(buf, "hello world\n");
    ret = write(fd, buf, strlen(buf));
    printf("Wrote %d bytes\n", ret);
    if(ret != strlen(buf)) {
        perror("");
    }
    close(fd);

    fd = open("/dev/drra", O_RDONLY);
    ret = read(fd, newbuf, 512);
    newbuf[511] = '\0';
    printf("Read %d bytes with content = %s\n", ret, newbuf);
    close(fd);
}
