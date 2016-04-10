/*
 * Copyright (c) 2016 Greg Becker.  All rights reserved.
 *
 * The following code implements a skeleton character driver for Linux
 * built for the purpose of exploring the linux kernel and developing
 * code that runs in the kernel environment.
 *
 * In its base configuration it presents a handful of character devices
 * (e.g., /dev/xx1, /dev/xx2, ...) that provide read, write, and mmap
 * interfaces to what is effectively an unbounded in-kernel RAM buffer.
 * (Not really a RAM disk as it does not implement partitioning nor
 * and of the disk ioctls).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>

#define XX_BASENAME         "xx"

typedef struct address_space aspace_t;
typedef struct file_operations fops_t;

/* There is one unit object for each device node in /dev
 * (e.g., /dev/xxctl, /dev/xx1, ...)
 */
struct xx_unit {
    dev_t               un_devno;
    struct device      *un_device;
    uid_t               un_uid;
    gid_t               un_gid;
    mode_t              un_mode;
    aspace_t           *un_mapping;
    atomic_t            un_refcnt;
    char                un_name[32];
    const fops_t       *un_fops;
};

/* Default unit type specific information.
 */
struct xx_utype {
    uid_t               ut_uid;
    gid_t               ut_gid;
    mode_t              ut_mode;
    const fops_t       *ut_fops;
};

/* Driver instance data (i.e., globals).
 */
struct xx_inst {
    struct cdev         i_cdev;
    dev_t               i_devno;
    struct class       *i_class;
    struct mutex        i_lock;
    struct xx_unit     *i_unit[];
};

static int xx_open(struct inode *ip, struct file *fp);
static int xx_release(struct inode *ip, struct file *fp);
static loff_t xx_llseek(struct file *fp, loff_t loff, int whence);
static long xx_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);
static ssize_t xx_read(struct file *fp, char *ubuf, size_t size, loff_t *loff);
static ssize_t xx_write(struct file *fp, const char *ubuf, size_t size, loff_t *loff);
static int xx_mmap(struct file *fp, struct vm_area_struct *vma);

static int xx_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
static void xx_vm_open(struct vm_area_struct *vma);
static void xx_vm_close(struct vm_area_struct *vma);

static void xx_exit_impl(void);

static const struct file_operations xx_fops_ctl = {
    .owner          = THIS_MODULE,
    .open           = xx_open,
    .release        = xx_release,
    .unlocked_ioctl = xx_ioctl,
};

static const struct file_operations xx_fops_rw = {
    .owner          = THIS_MODULE,
    .open           = xx_open,
    .release        = xx_release,
    .llseek         = xx_llseek,
    .unlocked_ioctl = xx_ioctl,
    .read           = xx_read,
    .write          = xx_write,
    .mmap           = xx_mmap,
};

static const struct vm_operations_struct xx_vm_ops = {
    .open           = xx_vm_open,
    .close          = xx_vm_close,
    .fault          = xx_vm_fault,
};

static const struct xx_utype xx_utype_ctl = {
    .ut_uid     = 0,
    .ut_gid     = 0,
    .ut_mode    = 0644,
    .ut_fops    = &xx_fops_ctl,
};

static const struct xx_utype xx_utype_rw = {
    .ut_uid     = 0,
    .ut_gid     = 6,
    .ut_mode    = 0660,
    .ut_fops    = &xx_fops_rw,
};

static struct xx_inst *inst;

static unsigned int xx_units_max = 4;
module_param(xx_units_max, uint, 0444);
MODULE_PARM_DESC(xx_units_max, "max devices");

static unsigned int xx_trace = 0;
module_param(xx_trace, uint, 0444);
MODULE_PARM_DESC(xx_trace, "enable simple function call tracing");


#define TRC(...)                                \
do {                                        	\
    if (xx_trace)                               \
        trc(__func__, __LINE__, __VA_ARGS__);   \
} while (0)

static void
trc(const char *func, int line, const char *fmt, ...)
{
    char msg[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf((msg), sizeof(msg), fmt, ap);
    va_end(ap);

    printk(KERN_INFO "%4d %-12s %s\n", line, func, msg);
}

static struct xx_unit *
xx_unit_create(const struct xx_utype *utype, const char *fmt, uint minor)
{
    struct xx_unit *unit;

    if (inst->i_unit[minor])
        return NULL;

    mutex_lock(&inst->i_lock);
    inst->i_unit[minor] = (void *)-1;
    mutex_unlock(&inst->i_lock);

    unit = kzalloc(sizeof(*unit), GFP_KERNEL);
    if (unit) {
        snprintf(unit->un_name, sizeof(unit->un_name), fmt, minor);
        unit->un_devno = MKDEV(MAJOR(inst->i_devno), minor);
        atomic_set(&unit->un_refcnt, 1);
        unit->un_uid = utype->ut_uid;
        unit->un_gid = utype->ut_gid;
        unit->un_mode = utype->ut_mode;
        unit->un_fops = utype->ut_fops;

        TRC("creating %s...", unit->un_name);

        unit->un_device = device_create(inst->i_class, NULL,
                                        unit->un_devno, unit, unit->un_name);
        if (IS_ERR(unit->un_device)) {
            //rc = PTR_ERR(unit->un_device);
            kfree(unit);
            unit = NULL;
            goto error;
        }
    }

 error:
    mutex_lock(&inst->i_lock);
    inst->i_unit[minor] = unit;
    mutex_unlock(&inst->i_lock);

    return unit;
}

static void
xx_unit_destroy(struct xx_unit *unit)
{
    struct xx_unit **unitp = inst->i_unit + MINOR(unit->un_devno);

    mutex_lock(&inst->i_lock);
    *unitp = (void *)-1;
    mutex_unlock(&inst->i_lock);

    if (0 == atomic_read(&unit->un_refcnt)) {
        if (unit->un_mapping) {
            truncate_inode_pages(unit->un_mapping, 0);
            invalidate_mapping_pages(unit->un_mapping, 0, -1);
        }

        device_destroy(inst->i_class, unit->un_devno);
        kfree(unit);

        mutex_lock(&inst->i_lock);
        *unitp = NULL;
        mutex_unlock(&inst->i_lock);
    }
}

static struct xx_unit *
xx_unit_get_by_minor(uint minor)
{
    struct xx_unit *unit = NULL;

    if (minor < xx_units_max) {

        mutex_lock(&inst->i_lock);
        unit = inst->i_unit[minor];
        if (unit) {
            if (unit == (void *)-1) {
                unit = NULL;
            } else {
                atomic_inc(&unit->un_refcnt);
            }
        }
        mutex_unlock(&inst->i_lock);
    }

    return unit;
}

static void
xx_unit_get(struct xx_unit *unit)
{
    atomic_inc(&unit->un_refcnt);
}

static void
xx_unit_put(struct xx_unit *unit)
{
    if (unit) {
        if (0 == atomic_dec_return(&unit->un_refcnt)) {
            xx_unit_destroy(unit);
        }
    }
}

static int
xx_open(struct inode *ip, struct file *fp)
{
    struct xx_unit *unit;

    unit = xx_unit_get_by_minor(iminor(fp->f_inode));
    if (!unit)
        return -ENODEV;

    if (!fp->private_data) {
        if (unit->un_fops == &xx_fops_ctl) {
            nonseekable_open(ip, fp);
        }
        fp->f_op = unit->un_fops;
        fp->private_data = unit;
        unit->un_mapping = fp->f_mapping;
    }

    return 0;
}

static int
xx_release(struct inode *ip, struct file *fp)
{
    struct xx_unit *unit = fp->private_data;

    xx_unit_put(unit);

    return 0;
}


static loff_t
xx_llseek(struct file *fp, loff_t loff, int whence)
{
    //struct xx_unit *unit = fp->private_data;

    switch (whence) {
    case SEEK_SET:
        break;

    case SEEK_CUR:
        loff += fp->f_pos;
        break;

    case SEEK_END:
        loff = LONG_MAX;
        break;

    default:
        loff = -EINVAL;
        break;
    }

    if (loff >= 0)
        fp->f_pos = loff;

    return loff;
}

static long
xx_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
    //struct xx_unit *unit = fp->private_data;

    return -ENOTTY;
}

static ssize_t
xx_read(struct file *fp, char *ubuf, size_t size, loff_t *loff)
{
    //struct xx_unit *unit = fp->private_data;
    ulong offset, pgoff, resid;
    size_t nleft, length;
    struct page *page;

    nleft = size;

    while (nleft > 0) {
        pgoff = *loff >> PAGE_CACHE_SHIFT;
        offset = *loff - (pgoff << PAGE_CACHE_SHIFT);
        length = min_t(size_t, PAGE_CACHE_SIZE - offset, nleft);

        page = find_lock_page(fp->f_mapping, pgoff);
        if (page) {
            resid = copy_to_user(ubuf, page_address(page) + offset, length);
            unlock_page(page);

            page_cache_release(page);
        } else {
            resid = copy_to_user(ubuf, ZERO_PAGE(pgoff), length);
        }

        if (resid > 0) {
            nleft -= (length - resid);
            *loff += (length - resid);

            return ((nleft < size) ? (size - nleft) : -EFAULT);
        }

        *loff += length;
        nleft -= length;
        ubuf += length;
    }

    return (size - nleft);
}

static ssize_t
xx_write(struct file *fp, const char *ubuf, size_t size, loff_t *loff)
{
    //struct xx_unit *unit = fp->private_data;
    ulong offset, pgoff, resid;
    size_t nleft, length;
    struct page *page;

    nleft = size;

    while (nleft > 0) {
        pgoff = *loff >> PAGE_CACHE_SHIFT;
        offset = *loff - (pgoff << PAGE_CACHE_SHIFT);
        length = min_t(size_t, PAGE_CACHE_SIZE - offset, nleft);

        page = find_or_create_page(fp->f_mapping, pgoff, GFP_KERNEL);
        if (!page)
            return ((nleft < size) ? (size - nleft) : -ENOMEM);

        resid = copy_from_user(page_address(page) + offset, ubuf, length);
        if (resid > 0) {
            nleft -= (length - resid);
            *loff += (length - resid);
            unlock_page(page);

            page_cache_release(page);

            return ((nleft < size) ? (size - nleft) : -EFAULT);
        }

        if (!PageReserved(page))
            SetPageDirty(page);

        SetPageUptodate(page);
        unlock_page(page);

        page_cache_release(page);

        *loff += length;
        nleft -= length;
        ubuf += length;
    }

    return (size - nleft);
}

static int
xx_mmap(struct file *fp, struct vm_area_struct *vma)
{
    struct xx_unit *unit = fp->private_data;

    vma->vm_ops = &xx_vm_ops;
    vma->vm_flags |= (VM_IO | VM_DONTDUMP | VM_NORESERVE);
    vma->vm_private_data = unit;

    xx_vm_open(vma);
    
    return 0;
}

static void
xx_vm_open(struct vm_area_struct *vma)
{
    struct xx_unit *unit = vma->vm_private_data;

    xx_unit_get(unit);
}

static void
xx_vm_close(struct vm_area_struct *vma)
{
    struct xx_unit *unit = vma->vm_private_data;

    xx_unit_put(unit);
}

static int
xx_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    struct address_space *mapping = vma->vm_file->f_mapping;
    //struct xx_unit *unit = vma->vm_private_data;
    struct page *page;
    int rc;

    rc = 0;

    page = find_or_create_page(mapping, vmf->pgoff, GFP_KERNEL);
    if (!page)
        return VM_FAULT_OOM;

    if (!PageUptodate(page)) {
        memset(page_address(page), 0, PAGE_CACHE_SIZE);
        SetPageUptodate(page);
        rc = VM_FAULT_MAJOR;
    }
    unlock_page(page);

    vmf->page = page;

    return rc;
}

static int
xx_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    struct xx_unit *unit = dev_get_drvdata(dev);

    if (unit) {
        add_uevent_var(env, "DEVUID=%u", unit->un_uid);
        add_uevent_var(env, "DEVGID=%u", unit->un_gid);
        add_uevent_var(env, "DEVMODE=%#o", unit->un_mode);

        TRC("%-6s %u %u %03o",
            dev_name(dev), unit->un_uid, unit->un_gid, unit->un_mode);
    }

    return 0;
}

static int __init
xx_init(void)
{
    const char *errmsg = NULL;
    struct xx_unit *unit;
    size_t instsz;
    int rc;
    int i;

    TRC("loading %s module...", XX_BASENAME);

    instsz = sizeof(*inst) + sizeof(*unit) * xx_units_max;

    inst = kzalloc(instsz, GFP_KERNEL);
    if (!inst)
        return -ENOMEM;

    mutex_init(&inst->i_lock);
    cdev_init(&inst->i_cdev, &xx_fops_ctl);
    inst->i_cdev.owner = THIS_MODULE;

    rc = alloc_chrdev_region(&inst->i_devno, 0, xx_units_max, XX_BASENAME);
    if (rc) {
        errmsg = "alloc_chrdev_region() failed";
        goto error;
    }

    inst->i_class = class_create(THIS_MODULE, module_name(THIS_MODULE));
    if (IS_ERR(inst->i_class)) {
        errmsg = "class_create() failed";
        rc = PTR_ERR(inst->i_class);
        goto error;
    }

    inst->i_class->dev_uevent = xx_uevent;

    rc = cdev_add(&inst->i_cdev, inst->i_devno, xx_units_max);
    if (rc) {
        errmsg = "cdev_add() failed";
        goto error;
    }

    unit = xx_unit_create(&xx_utype_ctl, XX_BASENAME "ctl", 0);
    if (!unit) {
        errmsg = "unable to create control device";
        goto error;
    }

    for (i = 1; i < xx_units_max; ++i) {
        xx_unit_create(&xx_utype_rw, XX_BASENAME "%u", i);
    }

    TRC("%s module loaded", XX_BASENAME);

 error:
    if (rc) {
        printk(KERN_ERR "%s: %s: rc=%d\n", __func__, errmsg, rc);

        xx_exit_impl();
    }

    return rc;
}

static void
xx_exit_impl(void)
{
    int i;

    if (inst) {
        if (inst->i_devno != 0) {
            if (inst->i_class) {
                if (inst->i_cdev.ops) {
                    for (i = 0; i < xx_units_max; ++i) {
                        xx_unit_put(inst->i_unit[i]);
                    }
                    cdev_del(&inst->i_cdev);
                }
                class_destroy(inst->i_class);
            }
            unregister_chrdev_region(inst->i_devno, xx_units_max);
        }
        kfree(inst);
    }
}

static void __exit
xx_exit(void)
{
    xx_exit_impl();

    TRC("%s module unloaded", XX_BASENAME);
}

module_init(xx_init);
module_exit(xx_exit);

MODULE_VERSION("1.0.0");
MODULE_AUTHOR("Greg Becker");
MODULE_DESCRIPTION("test driver");
MODULE_LICENSE("GPL");
