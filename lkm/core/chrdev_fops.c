#include "chrdev_fops.h"
#include "api.h"
#include <linux/sched.h>
#include <linux/sched/mm.h>

int shadow_open(struct inode *inode, struct file *file) {
    if (current->mm) {
        mmgrab(current->mm);
        file->private_data = current->mm;
    }
    return 0;
}

int shadow_release(struct inode *inode, struct file *file) {
    struct mm_struct *mm;
    
    mm = file->private_data;
    if (mm) {
        sa_tdown(mm);
        mmdrop(mm);
    }
    return 0;
}