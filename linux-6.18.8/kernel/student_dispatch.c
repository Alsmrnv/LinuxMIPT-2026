#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/student_dispatch.h>

#define STUDENT_OP_MAX 256

struct student_entry {
    student_handler_t fn;
    struct module *owner;
};

static struct student_entry table[STUDENT_OP_MAX];
static DEFINE_MUTEX(student_lock);


int register_student_handler(u32 op, student_handler_t fn, struct module *owner) {
    if (op >= STUDENT_OP_MAX)
        return -EINVAL;
    
    mutex_lock(&student_lock);
    
    if (table[op].fn) {
        mutex_unlock(&student_lock);
        return -EEXIST;
    }

    table[op].fn = fn;
    table[op].owner = owner;

    mutex_unlock(&student_lock);

    return 0;
}
EXPORT_SYMBOL_GPL(register_student_handler);

int unregister_student_handler(u32 op) {
    mutex_lock(&student_lock);

    table[op].fn = NULL;
    table[op].owner = NULL;

    mutex_unlock(&student_lock);

    return 0;
}
EXPORT_SYMBOL_GPL(unregister_student_handler);

SYSCALL_DEFINE2(student_dispatch, u32, op, u64, value) {
    student_handler_t fn;
    struct module *owner;
    long ret;

    if (op >= STUDENT_OP_MAX)
        return -EINVAL;

    mutex_lock(&student_lock);

    fn = table[op].fn;
    owner = table[op].owner;

    if (!fn) {
        mutex_unlock(&student_lock);
        return -ENOSYS;
    }

    if (!try_module_get(owner)) {
        mutex_unlock(&student_lock);
        return -ENODEV;
    }

    mutex_unlock(&student_lock);

    ret = fn((void *)&value);

    module_put(owner);
    
    return ret;
}
