#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define TICKET_DEVS	2
#define NAME_LEN	32
#define LINE_LEN	48
#define CMD_LEN		64

static int nseats = 10;
module_param(nseats, int, 0444);
MODULE_PARM_DESC(nseats, "Number of seats in each service");

static int delay_ms = 2000;
module_param(delay_ms, int, 0644);
MODULE_PARM_DESC(delay_ms, "Artificial delay inside read and write, ms");

struct seat {
	int sold;
	char owner[NAME_LEN];
};

struct ticket_dev {
	struct seat *seats;
	struct mutex lock;
	wait_queue_head_t queue;
	int readers;
	int writers;
	int waiting_writers;
	int minor;
	struct cdev cdev;
};

static dev_t first_devno;
static struct class *ticket_class;
static struct ticket_dev services[TICKET_DEVS];

static int ticket_open(struct inode *inode, struct file *filp)
{
	filp->private_data = container_of(inode->i_cdev, struct ticket_dev,
					  cdev);
	return 0;
}

static int ticket_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t ticket_read(struct file *filp, char __user *buf, size_t count,
			   loff_t *f_pos)
{
	struct ticket_dev *dev = filp->private_data;
	size_t size = (nseats + 1) * LINE_LEN;
	size_t len;
	char *kbuf;
	ssize_t ret;
	int i;

	if (*f_pos > 0)
		return 0;

	kbuf = kmalloc(size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (mutex_lock_interruptible(&dev->lock)) {
		ret = -ERESTARTSYS;
		goto out_free;
	}
	/*
	 * Sleeping with the mutex held would block the writer that has to
	 * finish and wake us up, so the mutex is dropped before waiting.
	 */
	while (dev->writers > 0 || dev->waiting_writers > 0) {
		mutex_unlock(&dev->lock);
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out_free;
		}
		pr_info("ticket%d: reader %d waits\n", dev->minor,
			current->pid);
		if (wait_event_interruptible(dev->queue, dev->writers == 0 &&
					     dev->waiting_writers == 0)) {
			ret = -ERESTARTSYS;
			goto out_free;
		}
		if (mutex_lock_interruptible(&dev->lock)) {
			ret = -ERESTARTSYS;
			goto out_free;
		}
	}
	dev->readers++;
	mutex_unlock(&dev->lock);

	pr_info("ticket%d: reader %d started (readers=%d)\n", dev->minor,
		current->pid, dev->readers);

	len = scnprintf(kbuf, size, "service %d\n", dev->minor);
	for (i = 0; i < nseats; i++)
		len += scnprintf(kbuf + len, size - len, "%3d  %s\n", i,
				 dev->seats[i].sold ? dev->seats[i].owner :
				 "free");
	msleep(delay_ms);

	mutex_lock(&dev->lock);
	dev->readers--;
	mutex_unlock(&dev->lock);
	wake_up_interruptible(&dev->queue);
	pr_info("ticket%d: reader %d finished\n", dev->minor, current->pid);

	if (len > count)
		len = count;
	if (copy_to_user(buf, kbuf, len)) {
		ret = -EFAULT;
		goto out_free;
	}
	*f_pos += len;
	ret = len;

out_free:
	kfree(kbuf);
	return ret;
}

static ssize_t ticket_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	struct ticket_dev *dev = filp->private_data;
	char cmd[CMD_LEN];
	char name[NAME_LEN];
	ssize_t ret;
	int n;

	if (count == 0 || count >= CMD_LEN)
		return -EINVAL;
	if (copy_from_user(cmd, buf, count))
		return -EFAULT;
	cmd[count] = '\0';
	if (sscanf(cmd, "%d %31s", &n, name) != 2)
		return -EINVAL;
	if (n < 0 || n >= nseats)
		return -EINVAL;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	dev->waiting_writers++;
	while (dev->readers > 0 || dev->writers > 0) {
		mutex_unlock(&dev->lock);
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out_cancel;
		}
		pr_info("ticket%d: writer %d waits (readers=%d writers=%d)\n",
			dev->minor, current->pid, dev->readers, dev->writers);
		if (wait_event_interruptible(dev->queue, dev->readers == 0 &&
					     dev->writers == 0)) {
			ret = -ERESTARTSYS;
			goto out_cancel;
		}
		if (mutex_lock_interruptible(&dev->lock)) {
			ret = -ERESTARTSYS;
			goto out_cancel;
		}
	}
	dev->waiting_writers--;
	dev->writers = 1;
	mutex_unlock(&dev->lock);

	pr_info("ticket%d: writer %d started, nobody else inside\n",
		dev->minor, current->pid);
	msleep(delay_ms);

	mutex_lock(&dev->lock);
	if (dev->seats[n].sold) {
		pr_info("ticket%d: writer %d: seat %d is taken\n", dev->minor,
			current->pid, n);
		ret = -EEXIST;
	} else {
		dev->seats[n].sold = 1;
		strscpy(dev->seats[n].owner, name, NAME_LEN);
		pr_info("ticket%d: writer %d bought seat %d for %s\n",
			dev->minor, current->pid, n, name);
		ret = count;
	}
	dev->writers = 0;
	mutex_unlock(&dev->lock);
	wake_up_interruptible(&dev->queue);
	return ret;

out_cancel:
	/* Readers may be waiting only because of this writer. */
	mutex_lock(&dev->lock);
	dev->waiting_writers--;
	mutex_unlock(&dev->lock);
	wake_up_interruptible(&dev->queue);
	return ret;
}

static const struct file_operations ticket_fops = {
	.owner = THIS_MODULE,
	.open = ticket_open,
	.release = ticket_release,
	.read = ticket_read,
	.write = ticket_write,
};

static char *ticket_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

static int ticket_setup(struct ticket_dev *dev, int minor)
{
	dev_t devno = MKDEV(MAJOR(first_devno), minor);
	struct device *d;
	int ret;

	dev->seats = kcalloc(nseats, sizeof(*dev->seats), GFP_KERNEL);
	if (!dev->seats)
		return -ENOMEM;
	mutex_init(&dev->lock);
	init_waitqueue_head(&dev->queue);
	dev->minor = minor;

	cdev_init(&dev->cdev, &ticket_fops);
	dev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&dev->cdev, devno, 1);
	if (ret)
		goto err_free;

	d = device_create(ticket_class, NULL, devno, NULL, "ticket%d", minor);
	if (IS_ERR(d)) {
		ret = PTR_ERR(d);
		goto err_cdev;
	}
	return 0;

err_cdev:
	cdev_del(&dev->cdev);
err_free:
	kfree(dev->seats);
	return ret;
}

static void ticket_cleanup(struct ticket_dev *dev)
{
	device_destroy(ticket_class, dev->cdev.dev);
	cdev_del(&dev->cdev);
	kfree(dev->seats);
}

static int __init ticket_init(void)
{
	int ret;
	int i;

	if (nseats <= 0 || delay_ms < 0)
		return -EINVAL;

	ret = alloc_chrdev_region(&first_devno, 0, TICKET_DEVS, "ticket");
	if (ret)
		return ret;

	ticket_class = class_create("ticket");
	if (IS_ERR(ticket_class)) {
		ret = PTR_ERR(ticket_class);
		goto err_region;
	}
	ticket_class->devnode = ticket_devnode;

	for (i = 0; i < TICKET_DEVS; i++) {
		ret = ticket_setup(&services[i], i);
		if (ret)
			goto err_devs;
	}

	pr_info("ticket: loaded, major=%d nseats=%d delay_ms=%d\n",
		MAJOR(first_devno), nseats, delay_ms);
	return 0;

err_devs:
	while (--i >= 0)
		ticket_cleanup(&services[i]);
	class_destroy(ticket_class);
err_region:
	unregister_chrdev_region(first_devno, TICKET_DEVS);
	return ret;
}

static void __exit ticket_exit(void)
{
	int i;

	for (i = TICKET_DEVS - 1; i >= 0; i--)
		ticket_cleanup(&services[i]);
	class_destroy(ticket_class);
	unregister_chrdev_region(first_devno, TICKET_DEVS);
	pr_info("ticket: unloaded\n");
}

module_init(ticket_init);
module_exit(ticket_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Scull-like ticket sale driver with reader/writer locking");
