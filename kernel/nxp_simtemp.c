#include <linux/module.h>      
#include <linux/init.h> 
#include <linux/fs.h>          // For alloc_chrdev_region, file_operations
#include <linux/cdev.h>        // For cdev functions
#include <linux/device.h>      // For class_create and device_create
#include <linux/uaccess.h>     // For copy_to_user (for user space)
#include <linux/jiffies.h>     // For jiffies (time tick counter) (used for simulating temperature variation)
#include <linux/version.h>     // For class create differences
#include <linux/random.h>      // For random numb generation (temp generation)
#include "nxp_simtemp.h"

#define DEVICE_NAME "simtemp"
#define CLASS_NAME  "simtemp"

static struct simtemp_dev simtemp;   // Device instance
static dev_t dev_num;                // Device number? (Pending about adding multiple instances support)

// Function prototypes
static int simtemp_open(struct inode *inode, struct file *file);
static int simtemp_release(struct inode *inode, struct file *file);
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t len, loff_t *offset);

// File operations structure
static const struct file_operations simtemp_fops = {
    .owner = THIS_MODULE,         // Points to this module
    .open = simtemp_open,         // Called when /dev/simtemp is opened
    .release = simtemp_release,   // Called when device is closed
    .read = simtemp_read,         // Called when user reads from device
};

// Called when device file is opened
static int simtemp_open(struct inode *inode, struct file *file)
{
    pr_info("simtemp: device opened\n");   
    return 0;
}

// Called when device file is closed
static int simtemp_release(struct inode *inode, struct file *file)
{
    pr_info("simtemp: device closed\n"); 
    return 0;
}

// Called when user reads from device (/dev/simtemp)
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t len, loff_t *offset)
{
    int ret;                                     // For copy_to_user return
    int temp;                                    // Temporary temperature value
    char temp_str[16];                           // Buffer for text representation
    size_t count;                                // Number of bytes to copy

    if (*offset > 0)
        return 0;

    temp = (20000 + (get_random_u32() % 15001))/1000;   // Simulated temperature (20000 to 35000)
    simtemp.temperature = temp;            // Store simulated value (Celsius)

    count = snprintf(temp_str, sizeof(temp_str), "%d\n", temp); // Format as string

    // Copy temperature string to user space
    ret = copy_to_user(buf, temp_str, count);
    if (ret != 0)
        return -EFAULT;


    *offset += count;

    pr_info("simtemp: temperature read = %d Celsius\n", temp);

    return count;
}

// Called when module is loaded (insmod)
static int __init simtemp_init(void)
{
    int ret;                                     // Return value

    pr_info("simtemp: Initializing simulated temperature driver\n");

    // Allocate device number (major and minor)
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("simtemp: failed to allocate device number\n");
        return ret;
    }

    // Initialize our cdev structure
    cdev_init(&simtemp.cdev, &simtemp_fops);
    simtemp.cdev.owner = THIS_MODULE;

    // Add the cdev to the system
    ret = cdev_add(&simtemp.cdev, dev_num, 1);
    if (ret < 0) {
        pr_err("simtemp: failed to add cdev\n");
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    // Create device class (visible in /sys/class/)
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)  
        simtemp.class = class_create(CLASS_NAME);
    #else  
        simtemp.class = class_create(THIS_MODULE, CLASS_NAME);
    #endif
    
    if (IS_ERR(simtemp.class)) {
        pr_err("simtemp: failed to create class\n");
        cdev_del(&simtemp.cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(simtemp.class);
    }
    simtemp.class->devnode = simtemp_devnode; // Force 0666 priviledge for device file
    // Create the device node in /dev/
    simtemp.device = device_create(simtemp.class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(simtemp.device)) {
        pr_err("simtemp: failed to create device\n");
        class_destroy(simtemp.class);
        cdev_del(&simtemp.cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(simtemp.device);
    }

    pr_info("simtemp: module loaded successfully (major=%d, minor=%d)\n",
            MAJOR(dev_num), MINOR(dev_num));
    return 0;                                   // Success
}

// Function for removing the module
static void __exit simtemp_exit(void)
{
    device_destroy(simtemp.class, dev_num);      // Remove /dev/simtemp
    class_destroy(simtemp.class);                // Remove /sys/class/simtemp
    cdev_del(&simtemp.cdev);                     // Delete cdev structure
    unregister_chrdev_region(dev_num, 1);        // Free device number

    pr_info("simtemp: module unloaded\n");
}

// Register init and exit functions
module_init(simtemp_init);
module_exit(simtemp_exit);

// Module information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cesar Ochoa");
MODULE_DESCRIPTION("Simulated temperature sensor for NXP Challenge");