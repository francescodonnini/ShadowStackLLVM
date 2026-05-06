#include "lkm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

int main() {
    int fd;
    struct ioctl_mem_params req = {0};
    char *shadow_ptr;
    pid_t pid;

    fd = open("/dev/shadowstack", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/shadowstack");
        return 1;
    }

    if (ioctl(fd, IOCTL_SHADOW_REQ, &req) < 0) {
        perror("IOCTL_SHADOW_REQ failed");
        close(fd);
        return 1;
    }
    if (req.error) {
        printf("Kernel module returned error code: %ld\n", req.error);
        close(fd);
        return 1;
    }

    shadow_ptr = (char *)req.addr;
    printf("[Parent] Allocated shadow page at %p\n", shadow_ptr);

    strcpy(shadow_ptr, "Hello from PARENT!");
    printf("[Parent] Wrote to shadow page: '%s'\n", shadow_ptr);

    printf("[Parent] Forking child process...\n");
    pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return 1;
    }

    if (pid == 0) {
        printf("[Child] Begin\n");

        printf("[Child]  Reading inherited shadow page: '%s'\n", shadow_ptr);
        
        strcpy(shadow_ptr, "Hello from CHILD!");
        printf("[Child]  Wrote to shadow page: '%s'\n", shadow_ptr);
        
        printf("[Child] End\n");
        exit(0);
    } else {
        wait(NULL); 
        
        printf("[Parent] Reading shadow page after child exit: '%s'\n", shadow_ptr);
        
        req.addr = (unsigned long long)shadow_ptr;
        ioctl(fd, IOCTL_SHADOW_FREE, &req);
        close(fd);
    }

    return 0;
}