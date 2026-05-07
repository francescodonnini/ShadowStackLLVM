#include "api.h"
#include "chrdev.h"
#include "chrdev_ioctl.h"
#include <asm/errno.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static long check_ioctl_cmd(unsigned int cmd) {
    if (_IOC_TYPE(cmd) != IOCTL_SHADOW_MAGIC) {
        pr_err("wrong magic number, expected %d but got %d", IOCTL_SHADOW_MAGIC, _IOC_TYPE(cmd));
        return -EINVAL;
    }
    if (_IOC_NR(cmd) > IOCTL_SHADOW_MAX_NR) {
        pr_err("number too high");
        return -EINVAL;
    }

    switch (cmd) {
    case IOCTL_SHADOW_REQ:
    case IOCTL_SHADOW_FREE:
    case IOCTL_SHADOW_FORK:
        break;
    default:
        return -ENOTTY;
    }

    if (!(_IOC_DIR(cmd) & (_IOC_READ | _IOC_WRITE))) {
        pr_err("ioctl: invalid request direction\n");
        return -EINVAL;
    }
    
    return 0;
}

long chrdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    void __user *user_arg = (void __user *)arg;
    long err;

    err = check_ioctl_cmd(cmd);
    if (err)
        return err;

    switch (cmd) {
    case IOCTL_SHADOW_REQ: {
        struct ioctl_mem_params req;
        unsigned long long addr;

        if (copy_from_user(&req, user_arg, sizeof(req)))
            return -EFAULT;

        req.error = sa_alloc(&addr);
        req.addr = addr;

        if (copy_to_user(user_arg, &req, sizeof(req)))
            return -EFAULT;
        
        break;
    }
    case IOCTL_SHADOW_FREE: {
        struct ioctl_mem_params req;
        unsigned long addr;

        if (copy_from_user(&req, user_arg, sizeof(req)))
            return -EFAULT;

        req.error = sa_free(req.addr);
        req.addr = addr;

        if (copy_to_user(user_arg, &req, sizeof(req)))
            return -EFAULT;
        
        break;
    }
    case IOCTL_SHADOW_FORK: {
        struct ioctl_fork_params fork_req;
        
        if (copy_from_user(&fork_req, user_arg, sizeof(fork_req)))
            return -EFAULT;
            
        fork_req.error = sa_fork(fork_req.p_tgid, fork_req.p_pid);
        
        if (copy_to_user(user_arg, &fork_req, sizeof(fork_req)))
            return -EFAULT;
        break;
    }
    }

    return 0;
}