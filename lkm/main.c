#include "api.h"
#include "chrdev.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>

static int __init runtime_init(void) {
    int err;

    if (boot_cpu_has(X86_FEATURE_PTI)) {
        pr_info("PTI is enabled, shadow stack will be disabled");
        return -EINVAL;
    }

    if (resolve_symbols() < 0) {
        return -1;
    }

    err = chrdev_init();
    if (err) {
        goto chrdev_failed;
    }
    return err;

chrdev_failed:
    chrdev_cleanup();
    pr_err("runtime_init failed, got error %d", err);
    return err;
}

static void __exit runtime_exit(void) {
    chrdev_cleanup();
}

MODULE_AUTHOR("Francesco Donnini <donnini.francesco00@gmail.com>");
MODULE_DESCRIPTION("Block-device snapshot");
MODULE_LICENSE("GPL");

module_init(runtime_init);
module_exit(runtime_exit);