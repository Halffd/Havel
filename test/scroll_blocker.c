// scroll_blocker.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Halffd");
MODULE_DESCRIPTION("Mouse scroll event blocker with toggle");
MODULE_VERSION("1.0");

static bool block_scroll = false;
static struct proc_dir_entry *proc_entry;
static struct input_handler scroll_handler;
static LIST_HEAD(handle_list);

struct scroll_handle {
    struct input_handle handle;
    struct list_head node;
};

// The actual event filter - this is where the magic happens
static bool scroll_filter_event(struct input_handle *handle, unsigned int type,
                                unsigned int code, int value) {
    if (!block_scroll)
        return false;  // Let everything through when disabled
    
    if (type == EV_REL && (code == REL_WHEEL || code == REL_HWHEEL)) {
        // Eat that scroll event like it's breakfast
        printk(KERN_DEBUG "scroll_blocker: Blocked scroll event (code=%u, value=%d)\n", 
               code, value);
        return true;  // Event consumed, gone, poof
    }
    
    return false;  // Let other events pass
}

// Connect to mouse devices
static int scroll_connect(struct input_handler *handler, struct input_dev *dev,
                         const struct input_device_id *id) {
    struct scroll_handle *scroll_handle;
    int error;
    
    // Only care about devices with scroll wheels
    if (!test_bit(EV_REL, dev->evbit) || !test_bit(REL_WHEEL, dev->relbit))
        return -ENODEV;
    
    scroll_handle = kzalloc(sizeof(struct scroll_handle), GFP_KERNEL);
    if (!scroll_handle)
        return -ENOMEM;
    
    scroll_handle->handle.dev = dev;
    scroll_handle->handle.handler = handler;
    scroll_handle->handle.name = "scroll_blocker";
    scroll_handle->handle.private = scroll_handle;
    
    error = input_register_handle(&scroll_handle->handle);
    if (error) {
        kfree(scroll_handle);
        return error;
    }
    
    error = input_open_device(&scroll_handle->handle);
    if (error) {
        input_unregister_handle(&scroll_handle->handle);
        kfree(scroll_handle);
        return error;
    }
    
    list_add(&scroll_handle->node, &handle_list);
    printk(KERN_INFO "scroll_blocker: Connected to %s\n", dev->name);
    
    return 0;
}

// Disconnect from devices
static void scroll_disconnect(struct input_handle *handle) {
    struct scroll_handle *scroll_handle = handle->private;
    
    printk(KERN_INFO "scroll_blocker: Disconnected from %s\n", handle->dev->name);
    
    input_close_device(handle);
    input_unregister_handle(handle);
    list_del(&scroll_handle->node);
    kfree(scroll_handle);
}

// Device matching - we want mice
static const struct input_device_id scroll_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = { BIT_MASK(EV_REL) },
        .relbit = { BIT_MASK(REL_WHEEL) },
    },
    { }, // Terminator
};

MODULE_DEVICE_TABLE(input, scroll_ids);

// Proc file operations for toggle control
static ssize_t proc_read(struct file *file, char __user *buffer, size_t count, loff_t *pos) {
    char buf[32];
    int len;
    
    if (*pos > 0)
        return 0;
    
    len = snprintf(buf, sizeof(buf), "%s\n", block_scroll ? "enabled" : "disabled");
    
    if (copy_to_user(buffer, buf, len))
        return -EFAULT;
    
    *pos = len;
    return len;
}

static ssize_t proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos) {
    char buf[16];
    
    if (count >= sizeof(buf))
        return -EINVAL;
    
    if (copy_from_user(buf, buffer, count))
        return -EFAULT;
    
    buf[count] = '\0';
    
    if (strncmp(buf, "1", 1) == 0 || strncmp(buf, "enable", 6) == 0) {
        block_scroll = true;
        printk(KERN_INFO "scroll_blocker: Scroll blocking ENABLED\n");
    } else if (strncmp(buf, "0", 1) == 0 || strncmp(buf, "disable", 7) == 0) {
        block_scroll = false;
        printk(KERN_INFO "scroll_blocker: Scroll blocking DISABLED\n");
    } else {
        return -EINVAL;
    }
    
    return count;
}

static const struct proc_ops proc_fops = {
    .proc_read = proc_read,
    .proc_write = proc_write,
};

// Module initialization
static int __init scroll_blocker_init(void) {
    int error;
    
    // Create proc entry for control
    proc_entry = proc_create("scroll_blocker", 0666, NULL, &proc_fops);
    if (!proc_entry) {
        printk(KERN_ERR "scroll_blocker: Failed to create proc entry\n");
        return -ENOMEM;
    }
    
    // Register input handler
    scroll_handler.filter = scroll_filter_event;
    scroll_handler.connect = scroll_connect;
    scroll_handler.disconnect = scroll_disconnect;
    scroll_handler.name = "scroll_blocker";
    scroll_handler.id_table = scroll_ids;
    
    error = input_register_handler(&scroll_handler);
    if (error) {
        printk(KERN_ERR "scroll_blocker: Failed to register input handler: %d\n", error);
        proc_remove(proc_entry);
        return error;
    }
    
    printk(KERN_INFO "scroll_blocker: Module loaded. Use /proc/scroll_blocker to toggle\n");
    return 0;
}

// Module cleanup - this is where we unfuck everything
static void __exit scroll_blocker_exit(void) {
    struct scroll_handle *scroll_handle, *tmp;
    
    // Disconnect from all devices first
    list_for_each_entry_safe(scroll_handle, tmp, &handle_list, node) {
        input_close_device(&scroll_handle->handle);
        input_unregister_handle(&scroll_handle->handle);
        list_del(&scroll_handle->node);
        kfree(scroll_handle);
    }
    
    // Unregister handler
    input_unregister_handler(&scroll_handler);
    
    // Remove proc entry
    proc_remove(proc_entry);
    
    printk(KERN_INFO "scroll_blocker: Module unloaded, scroll restored\n");
}

module_init(scroll_blocker_init);
module_exit(scroll_blocker_exit);
