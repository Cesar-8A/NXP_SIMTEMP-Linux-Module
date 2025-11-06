#include <linux/module.h>      
#include <linux/init.h> 
#include <linux/fs.h>          // For alloc_chrdev_region, file_operations
#include <linux/cdev.h>        // For cdev functions
#include <linux/device.h>      // For class_create and device_create
#include <linux/uaccess.h>     // For copy_to_user (for user space)
#include <linux/jiffies.h>     // For jiffies (time tick counter) (used for simulating temperature variation)
#include <linux/version.h>     // For class create differences
#include <linux/random.h>      // For random numb generation (temp generation)
#include <linux/poll.h>        // FOr polling inclusion
#include "nxp_simtemp.h"

#define DEVICE_NAME "simtemp"
#define CLASS_NAME  "simtemp"

static struct simtemp_dev *simdev;   // Device instance
static dev_t dev_num;                // Device number? (Pending about adding multiple instances support)

// Function prototypes (file operations)
static int simtemp_open(struct inode *inode, struct file *file);
static int simtemp_release(struct inode *inode, struct file *file);
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t len, loff_t *offset);
static __poll_t simtemp_poll(struct file *file, poll_table *wait);


// Sysfs attribute handlers
static ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf);

static ssize_t threshold_lower_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t threshold_lower_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count);

static ssize_t threshold_flag_show(struct device *dev,struct device_attribute *attr, char *buf);
// Create sysfs attribute
static DEVICE_ATTR_RW(sampling_ms);
static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RW(threshold_lower);
static DEVICE_ATTR_RO(threshold_flag);


//Function prototypes (utilities)
static void simtemp_timer_callback(struct timer_list *t); // For periodic reading generation



// File operations structure
static const struct file_operations simtemp_fops = {
    .owner = THIS_MODULE,         // Points to this module
    .open = simtemp_open,         // Called when /dev/simtemp is opened
    .release = simtemp_release,   // Called when device is closed
    .read = simtemp_read,         // Called when user reads from device
    .poll = simtemp_poll,         //
};


// Called when device file is opened FILE OPERATION FUNCTION
static int simtemp_open(struct inode *inode, struct file *file)
{
    pr_info("simtemp: device opened\n");   
    return 0;
}

// Called when device file is closed FILE OPERATION FUNCTION
static int simtemp_release(struct inode *inode, struct file *file)
{
    pr_info("simtemp: device closed\n"); 
    return 0;
}

// Called when user reads from device (/dev/simtemp) FILE OPERATION FUNCTION
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t len, loff_t *offset)
{
    int ret;
    int temp;
    char temp_str[16];
    size_t count;
    
    // if no data, wait, interruptable
    if (simdev->count == 0) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        // wait if buffer empty, interruptable 
        if (wait_event_interruptible(simdev->read_queue, simdev->count > 0))
            return -ERESTARTSYS;
    }        

    // disable Softirqs to acces buffer
    spin_lock_bh(&simdev->lock); 

    // Read from buffer
    temp = simdev->buffer[simdev->tail];
    simdev->tail = (simdev->tail + 1) % SIMTEMP_BUFFER_SIZE;
    simdev->count--;
    
    // Realese resourse 
    spin_unlock_bh(&simdev->lock); // enable Softirqs

    count = snprintf(temp_str, sizeof(temp_str), "%d\n", temp); 

    // Copy temperature string to user space
    ret = copy_to_user(buf, temp_str, count);
    if (ret != 0)
        return -EFAULT;

    pr_info("simtemp: temperature read = %d Celsius\n", temp);

    return count;
}

// Function that manages ring buffer and periodic data reading
static void simtemp_timer_callback(struct timer_list *t)
{
    struct simtemp_dev *dev = from_timer(dev, t, timer);
    int new_temp = 2500 + (get_random_u32() % 1001); // 25.00–35.00 C

    spin_lock(&dev->lock);

    if (dev->count < SIMTEMP_BUFFER_SIZE) {
        dev->buffer[dev->head] = new_temp;
        dev->head = (dev->head + 1) % SIMTEMP_BUFFER_SIZE;
        dev->count++;
        dev->temperature = new_temp;

        //Threshold comparison logic
        if (new_temp <= dev->threshold_lower) {
            if (!dev->threshold_flag) {
                // Event just triggered (transition from safe → below threshold)
                dev->threshold_flag = true;
                dev->threshold_event = true;
                wake_up_interruptible(&dev->threshold_queue); // wake any poll/select waiting
                pr_info("simtemp: TEMP FLAG ACTIVATED (temp=%d, thr=%d)\n",
                        new_temp, dev->threshold_lower);
            }
        } else {
            dev->threshold_flag = false; // reset when back above threshold
        }

        // Notify readers that new data is available
        wake_up_interruptible(&dev->read_queue);
    }

    spin_unlock(&dev->lock);

    // Reschedule timer for next sample
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->interval_ms));
}

//poll() - for select(), poll(), epoll() FILE OPERATION FUNCTION
static __poll_t simtemp_poll(struct file *file, poll_table *wait)
{
    __poll_t mask = 0;

    poll_wait(file, &simdev->read_queue, wait);
    poll_wait(file, &simdev->threshold_queue, wait);

    // Check for data
    spin_lock_bh(&simdev->lock);

    if (simdev->count > 0)
        mask |= POLLIN | POLLRDNORM;
    if (simdev->threshold_event){
        mask |= POLLPRI; // threshold event
        simdev->threshold_event = false;
    }

    spin_unlock_bh(&simdev->lock); // Usar spin_unlock_bh

    return mask;
}


// show current value of sampling_ms SYSFS ATTRIBUTE HANDLER
static ssize_t sampling_ms_show(struct device *dev,
                                        struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", simdev->interval_ms);
}


//allow changing sampling_ms value from sysfs and restarts timer SYSFS ATTRIBUTE HANDLER
static ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long val;
    int ret = kstrtoul(buf, 10, &val);
    if (ret)
        return ret;

    //check sampling range
    if (val < 1 || val > 10000)
        return -EINVAL;

    spin_lock(&simdev->lock);
    simdev->interval_ms = val; //update range value
    mod_timer(&simdev->timer, jiffies + msecs_to_jiffies(simdev->interval_ms)); //Restast timer
    spin_unlock(&simdev->lock);

    pr_info("simtemp: sampling interval updated to %lu ms\n", val);

    return count;
}

//SYSFS ATTRIBUTE HANDLER
static ssize_t threshold_lower_show(struct device *dev, struct device_attribute *attr, char *buf){
    return sprintf(buf, "%d\n", simdev->threshold_lower);
}

//SYSFS ATTRIBUTE HANDLER
static ssize_t threshold_lower_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    int val;
    if (kstrtoint(buf, 10, &val))
        return -EINVAL;

    spin_lock(&simdev->lock);
    simdev->threshold_lower = val;
    spin_unlock(&simdev->lock);
    return count;
}

//SYSFS ATTRIBUTE HANDLER
static ssize_t threshold_flag_show(struct device *dev, struct device_attribute *attr, char *buf){
    return sprintf(buf, "%d\n", simdev->threshold_flag ? 1 : 0);
}

// read-only temperature SYSFS ATTRIBUTE HANDLER
static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int temp;
    spin_lock(&simdev->lock);
    temp = simdev->temperature;           // tomar el valor actual
    spin_unlock(&simdev->lock);
    return sprintf(buf, "%d\n", temp);    // devolver como string
}


// Called when module is loaded (insmod)
// Called when module is loaded (insmod)
static int __init simtemp_init(void)
{
    int ret;

    pr_info("simtemp: Initializing simulated temperature driver\n");

    //Allocate memory for device structure (dynamic allocation for ring buffer, locks, etc.)
    simdev = kzalloc(sizeof(*simdev), GFP_KERNEL);
    if (!simdev)
        return -ENOMEM;

    //Initialize synchronization
    spin_lock_init(&simdev->lock);                  // Protects access to ring buffer and counters
    init_waitqueue_head(&simdev->read_queue);   
    init_waitqueue_head(&simdev->threshold_queue); 

    //Initialize ring buffer indices
    simdev->head = 0;                           // Start of data
    simdev->tail = 0;                           // End of data
    simdev->count = 0;                          // How many samples currently stored

    simdev->threshold_lower = 2700; // default 27.00°C
    simdev->threshold_flag = false;
    simdev->threshold_event = false;


    // Allocate device number (major and minor)
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        kfree(simdev);                          //free allocated memory if registration fails
        return ret;
    }

    //Initialize cdev 
    cdev_init(&simdev->cdev, &simtemp_fops);
    simdev->cdev.owner = THIS_MODULE;

    // Add the cdev to the system
    ret = cdev_add(&simdev->cdev, dev_num, 1);
    if (ret < 0) {
        pr_err("simtemp: failed to add cdev\n");
        unregister_chrdev_region(dev_num, 1);
        kfree(simdev);                          //free on error
        return ret;
    }

    // Initialize and start periodic timer for temperature updates
    timer_setup(&simdev->timer, simtemp_timer_callback, 0);
    simdev->interval_ms = 1000; // Update every 1s
    mod_timer(&simdev->timer, jiffies + msecs_to_jiffies(simdev->interval_ms));

    // Create device class (visible in /sys/class/)
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)  
        simdev->class = class_create(CLASS_NAME);
    #else  
        simdev->class = class_create(THIS_MODULE, CLASS_NAME);
    #endif
    
    if (IS_ERR(simdev->class)) {
        pr_err("simtemp: failed to create class\n");
        cdev_del(&simdev->cdev);
        unregister_chrdev_region(dev_num, 1);
        kfree(simdev);
        return PTR_ERR(simdev->class);
    }

    simdev->class->devnode = simtemp_devnode; // Force 0666 privilege for device file


    // Create the device node in /dev/
    simdev->device = device_create(simdev->class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(simdev->device)) {
        pr_err("simtemp: failed to create device\n");
        class_destroy(simdev->class);
        cdev_del(&simdev->cdev);
        unregister_chrdev_region(dev_num, 1);
        kfree(simdev);
        return PTR_ERR(simdev->device);
    }

    //log confirmation of buffer and queue setup
    pr_info("simtemp: ring buffer initialized (size=%d samples)\n", SIMTEMP_BUFFER_SIZE);
    pr_info("simtemp: waitqueue initialized for blocking read\n");

    pr_info("simtemp: module loaded successfully (major=%d, minor=%d)\n",
            MAJOR(dev_num), MINOR(dev_num));



    //Create sysfs sampling ms attribute: /sys/class/simtemp/simtemp/sampling_ms
    ret = device_create_file(simdev->device, &dev_attr_sampling_ms);
    if (ret)
        pr_err("simtemp: failed to create sysfs attribute sampling_ms\n");
    else
        pr_info("simtemp: sysfs attribute /sys/class/simtemp/simtemp/sampling_ms ready\n");

    //Create sysfs temperature attribute: /sys/class/simtemp/simtemp/sampling_ms
    ret = device_create_file(simdev->device, &dev_attr_temperature);
    if (ret)
        pr_err("simtemp: failed to create sysfs attribute temperature\n");
    else
        pr_info("simtemp: sysfs attribute /sys/class/simtemp/simtemp/temperature ready\n");

    //Show debug logs
    pr_info("simtemp: sysfs attribute /sys/class/simtemp/simtemp/sampling_ms ready\n");
    pr_info("simtemp: module loaded (major=%d, minor=%d)\n", MAJOR(dev_num), MINOR(dev_num));

    ret = device_create_file(simdev->device, &dev_attr_threshold_lower);
    if (ret)
        pr_err("simtemp: failed to create sysfs attribute threshold_lower\n");
    else
        pr_info("simtemp: sysfs attribute /sys/class/simtemp/simtemp/threshold_lower ready\n");
    
    ret = device_create_file(simdev->device, &dev_attr_threshold_flag);
    if (ret)
        pr_err("simtemp: failed to create sysfs attribute threshold_flag\n");
    else
        pr_info("simtemp: sysfs attribute /sys/class/simtemp/simtemp/threshold_flag ready\n");


    return 0; // Success
}

// Function for removing the module
static void __exit simtemp_exit(void)
{
    if (simdev) {
        // Wake any waiters so they don't remain blocked after unload
        wake_up_interruptible_all(&simdev->read_queue);
        wake_up_interruptible_all(&simdev->threshold_queue);
        del_timer_sync(&simdev->timer);              // Erase timer for periodic readings
        device_remove_file(simdev->device, &dev_attr_sampling_ms); // Erase sysfs sampling ms attribute
        device_remove_file(simdev->device, &dev_attr_temperature); // Erase sysfs temperature attribute
        device_remove_file(simdev->device, &dev_attr_threshold_flag); // Erase sysfs threshold_flag
        device_remove_file(simdev->device, &dev_attr_threshold_lower); // Erase sysfs threshold_lower
        device_destroy(simdev->class, dev_num);      // Remove /dev/simtemp
        class_destroy(simdev->class);               // Remove /sys/class/simtemp
        cdev_del(&simdev->cdev);                   // Delete cdev structure
        unregister_chrdev_region(dev_num, 1);        // Free device number
        kfree(simdev);                               // Free allocated space for buffer

        pr_info("simtemp: module unloaded\n");
    }
}


// Register init and exit functions
module_init(simtemp_init);
module_exit(simtemp_exit);

// Module information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cesar Ochoa");
MODULE_DESCRIPTION("Simulated temperature sensor for NXP Challenge");