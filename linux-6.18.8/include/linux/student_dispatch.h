#ifndef STUDENT_DISPATCH_H
#define STUDENT_DISPATCH_H
#include <linux/types.h>

typedef long (*student_handler_t)(void *data);

int register_student_handler(u32 op, student_handler_t fn, struct module *owner);
int unregister_student_handler(u32 op);

#endif
