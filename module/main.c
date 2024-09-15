#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>

// Define the ioctl command (example)
#define IOCTL_RESET _IO(0x07, 1)
#define IOCTL_POOPY_FACE _IO(0x07, 2)

int main() {
    int fd;

    // Open the device file (replace with the actual device file path)
    fd = open("/dev/hangman", O_RDWR);

    int x = 1;
    ioctl(fd,IOCTL_POOPY_FACE, &x);

    close(fd);
    return 0;
}

