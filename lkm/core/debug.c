#include "debug.h"
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>

#include <asm/pgtable.h>

static u32 target_pid = 1;

static struct dentry *debug_dir;
static struct dentry *debug_pgd;

static int pgd_show(struct seq_file *m, void *v) {
    struct task_struct *task;
    struct mm_struct *mm;
    pgd_t *pgd;
    int i;

    rcu_read_lock();
    task = pid_task(find_vpid(target_pid), PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        seq_printf(m, "error: PID %u not found\n", target_pid);
        return 0;
    }

    get_task_struct(task);
    rcu_read_unlock();

    mm = get_task_mm(task);
    put_task_struct(task);
    if (!mm) {
        seq_printf(m, "error: PID %d (%s) has no mm_struct (likely a kernel thread)\n", current->pid, current->comm);
        return 0;
    }

    pgd = mm->pgd;

    seq_printf(m, "#PID=%d\n", target_pid);
    seq_printf(m, "index,value\n");

    for (i = 256; i < PTRS_PER_PGD; ++i) {
        pgd_t entry;

        entry = pgd[i];
        if ((pgd_val(entry) & 0b1) != 0) {
            seq_printf(m, "%3d,0x%016lx\n", i, (unsigned long)pgd_val(entry));
        }
    }

    return 0;
}

static int debug_file_open(struct inode *inode, struct file *file) {
    return single_open(file, pgd_show, NULL);
}

static const struct file_operations debug_fops = {
    .owner = THIS_MODULE,
    .open = debug_file_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

int debug_init(void) {
    debug_dir = debugfs_create_dir("ss_dbg", NULL);
    if (!debug_dir) {
        pr_err("cannot create debugfs directory");
        return -ENOMEM;
    }

    debugfs_create_u32("target_pid", 0644, debug_dir, &target_pid);

    debug_pgd = debugfs_create_file("pgd", 0400, debug_dir, NULL, &debug_fops);
    if (!debug_pgd) {
        pr_err("cannot create debugfs file");
        debugfs_remove(debug_dir);
        return -ENOMEM;
    }

    return 0;
}

void debug_exit(void) {
    debugfs_remove_recursive(debug_dir);
}