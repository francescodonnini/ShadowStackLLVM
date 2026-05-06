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
    struct ioctl_mem_params __user *user_arg = (struct ioctl_mem_params __user *)arg;
    struct ioctl_mem_params req;
    unsigned long long addr;
    long err;

    err = check_ioctl_cmd(cmd);
    if (err)
        return err;

    if (copy_from_user(&req, user_arg, sizeof(req)))
        return -EFAULT;

    switch (cmd) {
    case IOCTL_SHADOW_REQ:
        req.error = sa_alloc(&addr);
        req.addr = addr;
        break;
    case IOCTL_SHADOW_FREE:
        req.error = sa_free(req.addr);
        break;
    }

    if (copy_to_user(user_arg, &req, sizeof(req)))
        return -EFAULT;

    return 0;
}