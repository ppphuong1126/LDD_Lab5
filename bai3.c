
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

#define DEVICE_NAME "simplebuf"
#define BUFFER_SIZE 128
#define NUM_DEVICES 2

struct simplebuf_device {
    struct cdev cdev;
    dev_t devt;
    char buffer[BUFFER_SIZE];
    size_t data_size;
};

static struct simplebuf_device sbdev[2];
static struct class *sbclass;

static int simplebuf_open(struct inode *inode, struct file *filp)
{
    struct simplebuf_device *dev;

    dev = container_of(inode->i_cdev,
                       struct simplebuf_device, cdev);

    filp->private_data = dev;

    pr_info("simplebuf: opened\n");

    return 0;
}

static int simplebuf_release(struct inode *inode, struct file *filp)
{
    pr_info("simplebuf: released\n");

    return 0;
}

static ssize_t simplebuf_read(struct file *filp, char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct simplebuf_device *dev = filp->private_data;
    size_t available, not_copied, copied;

    if (count == 0)
        return 0;

    if (*ppos < 0 || *ppos > (loff_t)BUFFER_SIZE)
        return -EINVAL;

    if (*ppos >= (loff_t)dev->data_size)
        return 0; /* EOF */

    available = dev->data_size - (size_t)*ppos;

    if (count > available)
        count = available;

    not_copied = copy_to_user(
        buf,
        dev->buffer + (size_t)*ppos,
        count
    );

    copied = count - not_copied;

    if (copied == 0)
        return -EFAULT;

    *ppos += copied;

    return copied;
}

static ssize_t simplebuf_write(struct file *filp,
                               const char __user *buf,
                               size_t count, loff_t *ppos)
{
    struct simplebuf_device *dev = filp->private_data;
    size_t available, not_copied, copied;

    if (count == 0)
        return 0;

    if (*ppos < 0 || *ppos > (loff_t)BUFFER_SIZE)
        return -EINVAL;

    if (*ppos == (loff_t)BUFFER_SIZE)
        return -ENOSPC;

    available = BUFFER_SIZE - (size_t)*ppos;

    if (count > available)
        count = available;

    not_copied = copy_from_user(
        dev->buffer + (size_t)*ppos,
        buf,
        count
    );

    copied = count - not_copied;

    if (copied == 0)
        return -EFAULT;

    *ppos += copied;

    if (*ppos > (loff_t)dev->data_size)
        dev->data_size = (size_t)*ppos;

    return copied;
}

static loff_t simplebuf_llseek(struct file *filp,
                               loff_t offset, int whence)
{
    struct simplebuf_device *dev = filp->private_data;
    loff_t base, new_pos;

    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;

    case SEEK_CUR:
        base = filp->f_pos;
        break;

    case SEEK_END:
        base = (loff_t)dev->data_size;
        break;

    default:
        return -EINVAL;
    }

    if (offset < -base ||
        offset > (loff_t)dev->data_size - base)
        return -EINVAL;

    new_pos = base + offset;
    filp->f_pos = new_pos;

    return new_pos;
}

static const struct file_operations simplebuf_fops = {
    .owner   = THIS_MODULE,
    .open    = simplebuf_open,
    .release = simplebuf_release,
    .read    = simplebuf_read,
    .write   = simplebuf_write,
    .llseek  = simplebuf_llseek,
};

static int __init simplebuf_init(void)
{
    struct device *dev;
    int ret;
    int i;
    int cdev_count = 0;
    int device_count = 0;
    ret = alloc_chrdev_region(&sbdev[0].devt, 0, NUM_DEVICES, DEVICE_NAME);

    if (ret < 0)
        return ret;

    for (i = 0; i < NUM_DEVICES; i++) {
        sbdev[i].devt = sbdev[0].devt + i;
        sbdev[i].data_size = 0;
        cdev_init(&sbdev[i].cdev, &simplebuf_fops);
        sbdev[i].cdev.owner = THIS_MODULE;

    ret = cdev_add(&sbdev[i].cdev, sbdev[i].devt, 1);
    if (ret < 0)
        goto err_cdev;
    cdev_count++;}
    sbclass = class_create(THIS_MODULE, DEVICE_NAME);

    if (IS_ERR(sbclass)) {
        ret = PTR_ERR(sbclass);
        goto err_cdev;
    }
    for(i = 0; i < NUM_DEVICES; i++) {
        dev = device_create(sbclass, NULL, sbdev[i].devt, NULL, "simplebuf%d", i);
        if (IS_ERR(dev)) {
            ret = PTR_ERR(dev);
            goto err_devices;
        }
        device_count++;
        pr_info(
        "simplebuf: registered simplebuf%d (major=%u, minor=%u)\n",
        i,
        MAJOR(sbdev[i].devt),
        MINOR(sbdev[i].devt));
    }
    return 0;
err_devices:
    while (device_count > 0) {
        device_count--;
        device_destroy(sbclass, sbdev[device_count].devt);
    }

    class_destroy(sbclass);

err_cdev:
    while (cdev_count > 0) {
        cdev_count--;
        cdev_del(&sbdev[cdev_count].cdev);
    }

    unregister_chrdev_region(sbdev[0].devt, NUM_DEVICES);

    return ret;
}

static void __exit simplebuf_exit(void)
{
    int i;
 for (i = 0; i < NUM_DEVICES; i++) {
        device_destroy(sbclass, sbdev[i].devt);
    }

    class_destroy(sbclass);

    for (i = 0; i < NUM_DEVICES; i++) {
        cdev_del(&sbdev[i].cdev);
    }
    unregister_chrdev_region(sbdev[0].devt, NUM_DEVICES);

    pr_info("simplebuf: unregistered\n");
}

module_init(simplebuf_init);
module_exit(simplebuf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LDD Teaching Laboratory");
MODULE_DESCRIPTION("Modern simplebuf character driver (single device)");
