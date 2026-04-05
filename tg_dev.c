#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");


static const int kNumberOfChats = 3;
static const int kBufSize = 1024;
static const char* read_fifo_path = "/tmp/messenger_resps";
static const char* write_fifo_path = "/tmp/module_reqs";

static dev_t dev_num;
static struct class *cl;
static struct cdev c_dev;

static int fifo_send_request(const char *request, size_t len) {
    struct file* f;
    ssize_t written;
 
    f = filp_open(write_fifo_path, O_WRONLY, 0);
    if (IS_ERR(f)) {
        printk("send request: can't open fifo");
        return -1;
    }
 
    written = kernel_write(f, request, len, &f->f_pos);
    filp_close(f, NULL);
 
    if (written < 0) {
        printk("can't write to fifo");
        return written;
    }
    return 0;
}

static ssize_t fifo_read_response(char* out_buf, size_t size) {
    struct file* f;
    f = filp_open(read_fifo_path, O_RDONLY, 0); // TODO mb add O_NONBLOCK
    if (IS_ERR(f)) {
        printk("read response: can't open fifo");
        return -1;
    }

    ssize_t bytes_read = kernel_read(f, out_buf, size - 1, &f->f_pos);
    filp_close(f, NULL);

    if (bytes_read < 0) {
        printk("failed to read from fifo");
        return bytes_read;
    }

    out_buf[bytes_read] = '\0';
    return bytes_read;
}

static int tg_open(struct inode* i, struct file* f) {
    int chat_id = MINOR(i->i_rdev) - MINOR(dev_num) + 1;
    f->private_data = (void*)(long)chat_id;
    printk("opened chat with chat_id: %d\n", chat_id);
    return 0;
}

static int tg_close(struct inode* i, struct file* f) {
    int chat_id = (int)(long)f->private_data;
    printk("closed chat with chat_id: %d\n", chat_id);
    return 0;
}

static ssize_t tg_read(struct file* f, char __user* buf, size_t len, loff_t* off) {
    if (len == 0) {
        return 0;
    }

    int chat_id = (int)(long)f->private_data;

    char request[sizeof("read") + 3];
    snprintf(request, sizeof(request), "read %d", chat_id);

    int c = fifo_send_request(request, strlen(request));

    if (c < 0) {
        return c;
    }

    char* response = kmalloc(kBufSize, GFP_KERNEL);
    if (!response) {
        printk("can't allocate buf");
        return -1;
    }

    ssize_t resp_len = fifo_read_response(response, kBufSize);
    if (resp_len < 0) {
        kfree(response);
        return resp_len;
    }

    if (resp_len > len) {
        resp_len = len;
    }

    if (copy_to_user(buf, response, resp_len) > 0) {
        printk("can't copy buf to user");
        kfree(response);
        return -1;
    }

    kfree(response);

    printk("read %ld bytes", resp_len);

    *off += resp_len;
    return resp_len;
}

static ssize_t tg_write(struct file* f, const char __user* buf, size_t len, loff_t* off) {
    if (len == 0) {
        return 0;
    }
    
    if (len > kBufSize) {
        printk("message is too long");
        return -1;
    }

    int chat_id = (int)(long)f->private_data;
    char* msg = kmalloc(kBufSize, GFP_KERNEL);
    if (!msg) {
        printk("can't allocate buf");
        return -1;
    }

    if (copy_from_user(msg, buf, len)) {
        printk("can't copy buf from user");
        kfree(msg);
        return -1;
    }
    msg[len] = '\0';

    char* request = kmalloc(sizeof("write") + 3 + len, GFP_KERNEL);
    if (!request) {
        printk("can't allocate buf");
        return -1;
    }
    snprintf(request, sizeof("write") + 3 + len, "write %d %s\n", chat_id, msg);

    kfree(msg);
    
    int c = fifo_send_request(request, strlen(request));
    kfree(request);
    if (c < 0) {
        return c;
    }

    char* response = kmalloc(kBufSize, GFP_KERNEL);
    if (!response) {
        printk("can't allocate buf");
        return -1;
    }

    ssize_t resp_len = fifo_read_response(response, kBufSize);
    kfree(response);
    if (resp_len < 0) {
        return resp_len;
    }

    printk("written %ld bytes", len);

    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = tg_open,
    .release = tg_close,
    .read = tg_read,
    .write = tg_write
};


static int __init dev_init(void) {
    printk("Tg dev started initializing\n");

    if (alloc_chrdev_region(&dev_num, 0, kNumberOfChats, "Telegram") != 0) {
        printk("can't allocate a range for char device\n");
        return -1;
    }

    cdev_init(&c_dev, &fops);
    if (cdev_add(&c_dev, dev_num, kNumberOfChats) != 0) {
        printk("can't add operations to the device\n");
        unregister_chrdev_region(dev_num, kNumberOfChats);
        return -1;
    }

    cl = class_create("Telegram");
    if (IS_ERR(cl)) {
        printk("can't create class for device\n");
        cdev_del(&c_dev);
        unregister_chrdev_region(dev_num, kNumberOfChats);
        return -1;
    }

    for (int i = 0; i < kNumberOfChats; ++i) {
        struct device *dev = device_create(cl, NULL, MKDEV(MAJOR(dev_num), MINOR(dev_num) + i), NULL, "telegram/chat_%d", i + 1);

        if (IS_ERR(dev)) {
            printk("can't create device\n");
            for (int j = 0; j < i; ++j) {
                device_destroy(cl, MKDEV(MAJOR(dev_num), MINOR(dev_num) + j));
            }
            class_destroy(cl);
            cdev_del(&c_dev);
            unregister_chrdev_region(dev_num, kNumberOfChats);
            return -1;
        }
    }

    printk("Tg dev successfully initialized\n");

    return 0;
}

static void __exit dev_exit(void) {
    printk("Exiting tg dev module\n");

    for (int i = 0; i < kNumberOfChats; ++i) {
        device_destroy(cl, MKDEV(MAJOR(dev_num), MINOR(dev_num) + i));
    }
    class_destroy(cl);
    cdev_del(&c_dev);
    unregister_chrdev_region(dev_num, kNumberOfChats);
}


module_init(dev_init);
module_exit(dev_exit);
