#include "api.h"
#include "chrdev.h"
#include "chrdev_ioctl.h"
#include "pr_format.h"
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
    long err = check_ioctl_cmd(cmd);
    if (err) {
        return err;
    }
    if (cmd == IOCTL_SHADOW_REQ) {
        if (!(_IOC_DIR(cmd) & (_IOC_READ | _IOC_WRITE))) {
            return -EINVAL;
        }

        struct ioctl_params req;
        unsigned long rem = copy_from_user(&req, (struct ioctl_params *)arg, sizeof(req));
        if (rem > 0) {
            return -EINVAL;
        }
    
        struct ss_chunk *mem = map_shadow_stack(req.vaddr);
        if (IS_ERR(mem)) {
            pr_err("map_shadow_stack failed with error %ld", PTR_ERR(mem));
            return PTR_ERR(mem);
        }

        rem = copy_to_user(&(req.top), &mem->top, sizeof(mem->top));
        if (rem > 0) {
            return -EINVAL;
        }

        rem = copy_to_user(&(req.error), &err, sizeof(err));
        if (rem > 0) {
            return -EINVAL;
        }
        return 0;
    } else {
        return -ENOTTY;
    }
}