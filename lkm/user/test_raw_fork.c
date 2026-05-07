#include "lkm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/syscall.h>

static inline pid_t sys_gettid(void) {
    return syscall(SYS_gettid);
}

int main() {
    int fd;
    struct ioctl_mem_params req = {0};
    char *shadow_ptr;
    pid_t pid;
    int sync_pipe[2];

    fd = open("/dev/shadowstack", O_RDWR);
    if (fd < 0) {
        perror("[Parent] Failed to open /dev/shadowstack");
        return 1;
    }

    if (ioctl(fd, IOCTL_SHADOW_REQ, &req) < 0) {
        perror("[Parent] IOCTL_SHADOW_REQ failed");
        close(fd);
        return 1;
    }
    if (req.error) {
        printf("[Parent] Kernel module returned error code: %ld\n", req.error);
        close(fd);
        return 1;
    }

    shadow_ptr = (char *)req.addr;
    printf("[Parent] Allocated shadow page at %p\n", shadow_ptr);

    strcpy(shadow_ptr, "Hello from PARENT!");
    printf("[Parent] Wrote to shadow page: '%s'\n", shadow_ptr);

    if (pipe(sync_pipe) < 0) {
        perror("[Parent] Pipe failed");
        close(fd);
        return 1;
    }

    pid_t p_tgid = getpid();
    pid_t p_pid = sys_gettid();

    printf("[Parent] Forking child process...\n");
    pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return 1;
    }

    if (pid == 0) {
        close(sync_pipe[0]);

        printf("[Child] Begin setup\n");

        close(fd);

        int child_fd = open("/dev/shadowstack", O_RDWR);
        if (child_fd < 0) {
            perror("[Child] Failed to open device");
            exit(1);
        }

        struct ioctl_fork_params fork_req = {
            .error = 0,
            .p_pid = p_pid,
            .p_tgid = p_tgid,
        };

        printf("[Child] Requesting deep copy via IOCTL_SHADOW_FORK...\n");
        if (ioctl(child_fd, IOCTL_SHADOW_FORK, &fork_req) < 0) {
            perror("[Child] IOCTL_SHADOW_FORK failed");
            exit(1);
        }

        char done = '1';
        write(sync_pipe[1], &done, 1);
        close(sync_pipe[1]);

        printf("[Child]  Reading inherited shadow page at  %p\n", shadow_ptr);
        printf("[Child]  Reading inherited shadow page:   '%s'\n", shadow_ptr);
        
        strcpy(shadow_ptr, "Hello from CHILD!");
        printf("[Child]  Wrote to shadow page: '%s'\n", shadow_ptr);
        
        printf("[Child] End\n");
        close(child_fd);
        exit(0);

    } else {
        close(sync_pipe[1]); 
        char done;
        read(sync_pipe[0], &done, 1);
        close(sync_pipe[0]);
        printf("[Parent] Child deep copy complete. Resuming...\n");

        wait(NULL); 
        
        printf("[Parent] Reading shadow page after child exit: '%s'\n", shadow_ptr);
        
        req.addr = (unsigned long long)shadow_ptr;
        ioctl(fd, IOCTL_SHADOW_FREE, &req);
        close(fd);
    }

    return 0;
}