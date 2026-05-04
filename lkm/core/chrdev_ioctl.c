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
    return 0;
}

long chrdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    long err;

    err = check_ioctl_cmd(cmd);
    if (err) {
        return err;
    }
    if (cmd == IOCTL_SHADOW_REQ || cmd == IOCTL_SHADOW_FREE) {
        struct ioctl_params req;
        unsigned long rem;
        unsigned long long addr;

        if (!(_IOC_DIR(cmd) & (_IOC_READ | _IOC_WRITE))) {
            pr_err("ioctl: invalid request");
            return -EINVAL;
        }

        rem = copy_from_user(&req, (struct ioctl_params *)arg, sizeof(req));
        if (rem > 0) {
            return -EINVAL;
        }
    
        if (cmd == IOCTL_SHADOW_REQ) {
            req.error = sa_alloc(&addr);
            req.addr = addr;
        } else {
            req.error = sa_free(req.addr);
        }
        
        rem = copy_to_user((struct ioctl_params*)arg, &req, sizeof(req));
        if (rem > 0) {
            return -EINVAL;
        }
        return 0;
    } else {
        return -ENOTTY;
    }
}