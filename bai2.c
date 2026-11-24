
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
#define NODE_NAME "simplebuf0"
#define BUFFER_SIZE 128

struct simplebuf_device {
    struct cdev cdev;
    dev_t devt;
    char buffer[BUFFER_SIZE];
    size_t data_size;
};

static struct simplebuf_device sbdev;
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

    ret = alloc_chrdev_region(&sbdev.devt, 0, 2, DEVICE_NAME);

    if (ret < 0)
        return ret;

    cdev_init(&sbdev.cdev, &simplebuf_fops);
    sbdev.cdev.owner = THIS_MODULE;

    ret = cdev_add(&sbdev.cdev, sbdev.devt, 2);

    if (ret < 0)
        goto err_region;

    sbclass = class_create(THIS_MODULE, DEVICE_NAME);

    if (IS_ERR(sbclass)) {
        ret = PTR_ERR(sbclass);
        goto err_cdev;
    }

    dev = device_create( sbclass, NULL, sbdev.devt, NULL, "simplebuf0" );
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        goto err_class;
    }
    dev = device_create( sbclass, NULL, sbdev.devt + 1, NULL, "simplebuf1" );
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        goto err_class;
    }
    pr_info(
        "simplebuf: registered %s (major=%u, minor=%u)\n",
        NODE_NAME,
        MAJOR(sbdev.devt),
        MINOR(sbdev.devt)
    );
    pr_info(
        "simplebuf: registered %s (major=%u, minor=%u)\n",
        "simplebuf1",
        MAJOR(sbdev.devt + 1),
        MINOR(sbdev.devt + 1)
    );

    return 0;
err_device0:
    device_destroy(sbclass, sbdev.devt);
err_device1:
    device_destroy(sbclass, sbdev.devt + 1);
err_class:
    class_destroy(sbclass);

err_cdev:
    cdev_del(&sbdev.cdev);

err_region:
    unregister_chrdev_region(sbdev.devt, 1);

    return ret;
}

static void __exit simplebuf_exit(void)
{
    device_destroy(sbclass, sbdev.devt + 1);
    device_destroy(sbclass, sbdev.devt);
    class_destroy(sbclass);
    cdev_del(&sbdev.cdev);
    unregister_chrdev_region(sbdev.devt, 2);

    pr_info("simplebuf: unregistered\n");
}

module_init(simplebuf_init);
module_exit(simplebuf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LDD Teaching Laboratory");
MODULE_DESCRIPTION("Modern simplebuf character driver (single device)");
