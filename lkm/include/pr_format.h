#ifndef LKM_PR_FORMAT_H
#define LKM_PR_FORMAT_H
#include <linux/errname.h>
#include <linux/module.h>

// adds some informations to the printk format string.
// e.g. printk(pr_format("hello")) prints "[<MODULE NAME>] hello"
#define pr_format(fmt) "[%s] %s: " "" fmt, module_name(THIS_MODULE), __FUNCTION__

static inline const char *errtoa(int err) {
    const char *s = errname(err);
    if (!s) {
        return "(INVALID ERROR)";
    }
    return s;
}

#endif