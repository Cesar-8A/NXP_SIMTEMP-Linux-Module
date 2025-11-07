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

//Headers required for platform driver and Device Tree
#include <linux/platform_device.h> // For platform_driver
#include <linux/of.h>            // For Device Tree functions (of_property_read)
#include <linux/ktime.h>         // For ktime_get_ns()

#include "nxp_simtemp.h"

#define DEVICE_NAME "simtemp"
#define CLASS_NAME  "simtemp"          

#define TEST 1

#if TEST
    // Struct for test
    static struct platform_device *simtemp_pdev_test;
#endif
// Function prototypes (file operations)
static int simtemp_open(struct inode *inode, struct file *file);
static int simtemp_release(struct inode *inode, struct file *file);
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t len, loff_t *offset);
static __poll_t simtemp_poll(struct file *file, poll_table *wait);
// Prototype for ioctl
static long simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

// Prototypes for platform driver functions
static int simtemp_probe(struct platform_device *pdev);
static void simtemp_remove(struct platform_device *pdev);


// Sysfs attribute handlers
static ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t threshold_flag_show(struct device *dev, struct device_attribute *attr, char *buf);

// For Treshold sysfs attribute handler
static ssize_t threshold_mC_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t threshold_mC_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

// Prototypes for sysfs files (mode, stats)
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buf);


// Sysfs attribute creation
static DEVICE_ATTR_RW(sampling_ms);
static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RO(threshold_flag);
static DEVICE_ATTR_RW(threshold_mC);

// Attributes for mode and stats
static DEVICE_ATTR_RW(mode);
static DEVICE_ATTR_RO(stats);

// Device Tree match table
static const struct of_device_id simtemp_of_match[] = {
    { .compatible = "nxp,simtemp" }, // match the DTS file
    { /* nothing xd */ }
};
MODULE_DEVICE_TABLE(of, simtemp_of_match); // Expose table to user space

// Platform driver structure definition
static struct platform_driver simtemp_platform_driver = {
    .driver = {
        .name = DEVICE_NAME,
        #if TEST
        .of_match_table = NULL, 
        #else
        .of_match_table = simtemp_of_match,// Link to our DT match table (NULL FOR LOCAL TESTING)
        #endif
    },
    .probe = simtemp_probe,   // Called when device is found
    .remove = simtemp_remove, // Called when device is removed
};


// File operations structure
static const struct file_operations simtemp_fops = {
    .owner = THIS_MODULE,
    .open = simtemp_open,
    .release = simtemp_release,
    .read = simtemp_read,
    .poll = simtemp_poll,
    .unlocked_ioctl = simtemp_ioctl, // Register the ioctl handler
};

// Function for opening the device file
static int simtemp_open(struct inode *inode, struct file *file)
{
    // Get device struct from inodes cdev
    struct simtemp_dev *dev = container_of(inode->i_cdev, struct simtemp_dev, cdev);
    
    // Store the instance-specific 'dev' pointer
    file->private_data = dev;
    
    pr_info("simtemp: device opened\n");
    return 0;
}

// Function for releasing the device file
static int simtemp_release(struct inode *inode, struct file *file)
{
    file->private_data = NULL; // Clear private_data
    pr_info("simtemp: device closed\n");
    return 0;
}

// Function for reading from the device file
//******** Completely rewritten for binary, blocking, buffered read ******/
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t len, loff_t *offset)
{
    struct simtemp_dev *dev = file->private_data;
    struct simtemp_sample sample;
    
    // reading a single binary record
    if (len != sizeof(struct simtemp_sample))
        return -EINVAL; // Invalid argument (wrong read size)

    // Wait for data (if blocking)
    if (dev->count == 0) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN; // Return "try again" if non-blocking
        
        // wait (interruptibly) until dev->count > 0
        if (wait_event_interruptible(dev->read_queue, dev->count > 0))
            return -ERESTARTSYS; // Handle signal
    }

    // Extract data from buffer (critical section)
    spin_lock_bh(&dev->lock);
    
    if (dev->count == 0) {
        // Race condition check
        spin_unlock_bh(&dev->lock);
        return 0; 
    }

    // Copy from the ring buffer
    sample = dev->buffer[dev->tail];
    dev->tail = (dev->tail + 1) % SIMTEMP_BUFFER_SIZE;
    dev->count--;
    
    spin_unlock_bh(&dev->lock);

    // Copy data to user space
    if (copy_to_user(buf, &sample, sizeof(struct simtemp_sample))) {
        pr_warn("simtemp: copy_to_user failed\n");
        spin_lock_bh(&dev->lock);
        dev->stats.read_errors++; // Update stats
        spin_unlock_bh(&dev->lock);
        return -EFAULT;
    }

    // Return bytes read, as required by read()
    return sizeof(struct simtemp_sample);
}

// Function for polling
static __poll_t simtemp_poll(struct file *file, poll_table *wait)
{
    // Get device struct from private_data
    struct simtemp_dev *dev = file->private_data;
    __poll_t mask = 0;

    // Use the instance-specific wait queues
    poll_wait(file, &dev->read_queue, wait);
    poll_wait(file, &dev->threshold_queue, wait);

    spin_lock_bh(&dev->lock); // Use instance-specific lock
    
    // Check if data is available for reading
    if (dev->count > 0) // Use instance-specific 'count'
        mask |= POLLIN | POLLRDNORM;
        
    // Check if the threshold event has occurred
    if (dev->threshold_event) { // Use instance-specific 'threshold_event'
        mask |= POLLPRI; // Use POLLPRI for "priority" event
        dev->threshold_event = false; // Consume the event
    }
    
    spin_unlock_bh(&dev->lock); // Use instance-specific lock

    return mask;
}

// ioctl handler function
static long simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct simtemp_dev *dev = file->private_data;
    struct simtemp_config config;
    long ret = 0;

    switch (cmd) {
    case SIMTEMP_IOC_SET_CONFIG:
        // Copy config struct from user space
        if (copy_from_user(&config, (void __user *)arg, sizeof(config)))
            return -EFAULT;

        // Validate input
        if (config.sampling_ms < 1 || config.sampling_ms > 10000)
            return -EINVAL;

        spin_lock_bh(&dev->lock);
        dev->interval_ms = config.sampling_ms;
        dev->threshold_mC = config.threshold_mC;
        // Restart timer with new interval
        mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->interval_ms));
        spin_unlock_bh(&dev->lock);
        
        pr_info("simtemp: IOCTL config set (interval=%u, threshold=%d)\n",
                config.sampling_ms, dev->threshold_mC);
        break;
        
    case SIMTEMP_IOC_GET_CONFIG:
        spin_lock_bh(&dev->lock);
        config.sampling_ms = dev->interval_ms;
        config.threshold_mC = dev->threshold_mC;
        spin_unlock_bh(&dev->lock);

        // Copy config struct back to user space
        if (copy_to_user((void __user *)arg, &config, sizeof(config)))
            return -EFAULT;
        break;
        
    default:
        ret = -EINVAL; // Unknown command
    }
    
    return ret;
}


// Timer callback function (for periodic readings)
static void simtemp_timer_callback(struct timer_list *t)
{
    // Get dev struct from the timer
    struct simtemp_dev *dev = from_timer(dev, t, timer);
    
    // Define variables for the new binary sample
    struct simtemp_sample new_sample;
    int new_temp_mC;
    u32 flags = 0;

    // Simulate temperature reading based on mode
    spin_lock(&dev->lock); //Use spin_lock (not bh) in timer context
    
    switch (dev->mode) {
        case SIMTEMP_MODE_NORMAL:
        default:
            // 25.000 to 35.000 mC
            new_temp_mC = 25000 + (get_random_u32() % 10001); 
            break;
        case SIMTEMP_MODE_NOISY:
            // 20.000 to 40.000 mC
            new_temp_mC = 20000 + (get_random_u32() % 20001);
            break;
        case SIMTEMP_MODE_RAMP:
            // Simple ramp (just an example)
            new_temp_mC = (dev->stats.samples_generated % 20000) + 25000;
            break;
    }
    
    // Check threshold
    if (new_temp_mC <= dev->threshold_mC) { // Use renamed threshold_mC
        // If flag was not set before, trigger event
        if (!dev->threshold_flag) {
            dev->threshold_flag = true;
            dev->threshold_event = true;
            flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED; // Set binary flag
            dev->stats.alerts_triggered++;          // Update stats
            
            pr_info("simtemp: TEMP FLAG ACTIVATED (temp=%d, thr=%d)\n",
                    new_temp_mC, dev->threshold_mC);
            
            // Wake up poll()
            wake_up_interruptible(&dev->threshold_queue);
        }
    } else {
        // Reset flag if temperature is back above threshold
        dev->threshold_flag = false;
    }

    // Create and buffer the binary sample
    new_sample.timestamp_ns = ktime_get_ns();
    new_sample.temp_mC = new_temp_mC;
    new_sample.flags = flags | SIMTEMP_FLAG_NEW_SAMPLE;

    // Add to ring buffer if space available
    if (dev->count < SIMTEMP_BUFFER_SIZE) {
        dev->buffer[dev->head] = new_sample; // Store the struct
        dev->head = (dev->head + 1) % SIMTEMP_BUFFER_SIZE;
        dev->count++;
        dev->stats.samples_generated++; // Update stats
        
        // Wake up read() / poll()
        wake_up_interruptible(&dev->read_queue);
    } else {
        // pr_warn("simtemp: buffer overflow\n"); // NEW: Optional warning
    }

    spin_unlock(&dev->lock);

    // Reschedule timer
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->interval_ms));
}


// --- Sysfs Handlers ---
// MODIFIED: All handlers now use 'dev_get_drvdata(dev)' to get the
// instance-specific 'simdev' pointer.

// Handler for /sys/class/simtemp/simtemp/sampling_ms (show)
static ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    // Get instance struct from device
    struct simtemp_dev *simdev = dev_get_drvdata(dev);
    return sprintf(buf, "%u\n", simdev->interval_ms);
}

// Handler for /sys/class/simtemp/simtemp/sampling_ms (store)
static ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct simtemp_dev *simdev = dev_get_drvdata(dev);
    unsigned long val;
    int ret = kstrtoul(buf, 10, &val);
    if (ret) return ret;
    
    // Validate
    if (val < 1 || val > 10000) return -EINVAL; // 1ms to 10s

    spin_lock_bh(&simdev->lock);
    simdev->interval_ms = val;
    // Reschedule timer with new interval
    mod_timer(&simdev->timer, jiffies + msecs_to_jiffies(simdev->interval_ms)); 
    spin_unlock_bh(&simdev->lock); 

    pr_info("simtemp: sampling interval updated to %lu ms\n", val);
    return count;
}

// Handler for /sys/class/simtemp/simtemp/temperature (show)
static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *simdev = dev_get_drvdata(dev); 
    int temp = 2500; 
    
    spin_lock_bh(&simdev->lock); 
    // Read last temperature from the buffer
    if (simdev->count > 0) {
        int last_idx = (simdev->head == 0) ? (SIMTEMP_BUFFER_SIZE - 1) : (simdev->head - 1);
        temp = simdev->buffer[last_idx].temp_mC;
    }
    spin_unlock_bh(&simdev->lock); 
    
    return sprintf(buf, "%d\n", temp);
}

// Handler for /sys/class/simtemp/simtemp/threshold_flag (show)
static ssize_t threshold_flag_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *simdev = dev_get_drvdata(dev); 
    return sprintf(buf, "%d\n", simdev->threshold_flag); 
}

// MODIFIED: Renamed from threshold_lower_show
static ssize_t threshold_mC_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *simdev = dev_get_drvdata(dev); 
    return sprintf(buf, "%d\n", simdev->threshold_mC); 
}

// MODIFIED: enamed from threshold_lower_store
static ssize_t threshold_mC_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct simtemp_dev *simdev = dev_get_drvdata(dev); 
    int val; 
    
    if (kstrtoint(buf, 10, &val)) 
        return -EINVAL;

    spin_lock_bh(&simdev->lock); 
    simdev->threshold_mC = val; 
    spin_unlock_bh(&simdev->lock); 
    return count;
}

// Handler for /sys/class/simtemp/simtemp/mode (show)
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *simdev = dev_get_drvdata(dev);
    switch (simdev->mode) {
        case SIMTEMP_MODE_NORMAL: return sprintf(buf, "normal\n");
        case SIMTEMP_MODE_NOISY:  return sprintf(buf, "noisy\n");
        case SIMTEMP_MODE_RAMP:   return sprintf(buf, "ramp\n");
        default:                  return sprintf(buf, "unknown\n");
    }
}

// Handler for /sys/class/simtemp/simtemp/mode (store)
static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct simtemp_dev *simdev = dev_get_drvdata(dev);
    
    spin_lock_bh(&simdev->lock);
    if (strncmp(buf, "normal", 6) == 0){
        simdev->mode = SIMTEMP_MODE_NORMAL;
        pr_info("TEMP MODE HAS CHANGED TO normal MODE");
    
    }
    else if (strncmp(buf, "noisy", 5) == 0){
        simdev->mode = SIMTEMP_MODE_NOISY;
        pr_info("TEMP MODE HAS CHANGED TO noisy MODE");
    }
    else if (strncmp(buf, "ramp", 4) == 0){
        simdev->mode = SIMTEMP_MODE_RAMP;
        pr_info("TEMP MODE HAS CHANGED TO ramp MODE");
    }
    else {
        spin_unlock_bh(&simdev->lock);
        return -EINVAL; // Invalid mode
    }
    spin_unlock_bh(&simdev->lock);
    return count;
}

// Handler for /sys/class/simtemp/simtemp/stats (show)
static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *simdev = dev_get_drvdata(dev);
    u64 samples, alerts, errors;
    
    // Read stats atomically
    spin_lock_bh(&simdev->lock);
    samples = simdev->stats.samples_generated;
    alerts = simdev->stats.alerts_triggered;
    errors = simdev->stats.read_errors;
    spin_unlock_bh(&simdev->lock);
    
    return sprintf(buf, "samples_generated: %llu\nalerts_triggered: %llu\nread_errors: %llu\n",
                   samples, alerts, errors);
}


// This is now the 'probe' function for the platform driver.
// It contains all the setup logic from your original 'simtemp_init'.
static int simtemp_probe(struct platform_device *pdev)
{
    int ret;
    struct simtemp_dev *simdev; // Local variable, not global
    struct device *dev = &pdev->dev; // Device from platform_device

    pr_info("simtemp: probe function called!\n");

    // Allocate memory for our device struct.
    // 'devm_kzalloc' is managed by the device, automatically freed on remove.
    simdev = devm_kzalloc(dev, sizeof(*simdev), GFP_KERNEL);
    if (!simdev)
        return -ENOMEM;
    
    // Link the platform_device to our simdev struct
    platform_set_drvdata(pdev, simdev);

    // Initialize locks and wait queues
    spin_lock_init(&simdev->lock);
    init_waitqueue_head(&simdev->read_queue);
    init_waitqueue_head(&simdev->threshold_queue);
    simdev->head = 0;
    simdev->tail = 0;
    simdev->count = 0;

    #if TEST
        pr_info("simtemp: Using default config for local test\n");
        simdev->interval_ms = 1000;
        simdev->threshold_mC = 27000;
        simdev->mode = SIMTEMP_MODE_NORMAL;

    #else
        u32 val; // For reading DT properties
        // Always use defaults for local testing (no DT)
        pr_info("simtemp: Loading configuration from Device Tree\n");
        ret = of_property_read_u32(dev->of_node, "sampling-ms", &val);
        simdev->interval_ms = (ret == 0) ? val : 1000; // Default 1000ms

        ret = of_property_read_u32(dev->of_node, "threshold-mC", &val);
        simdev->threshold_mC = (ret == 0) ? (int)val : 27000; // Default 27C

        simdev->mode = SIMTEMP_MODE_NORMAL; // Default mode
        
        pr_info("simtemp: DT config loaded (interval=%u ms, threshold=%d mC)\n",
                simdev->interval_ms, simdev->threshold_mC);    
    #endif

    ret = alloc_chrdev_region(&simdev->dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("simtemp: failed to alloc chrdev region\n");
        return ret;
    }

    pr_info("simtemp: device number allocated (major=%d, minor=%d)\n",
            MAJOR(simdev->dev_num), MINOR(simdev->dev_num));

    // init chardev
    cdev_init(&simdev->cdev, &simtemp_fops);
    simdev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&simdev->cdev, simdev->dev_num, 1);
    if (ret < 0) {
        pr_err("simtemp: failed to add cdev\n");
        goto err_unregister_chrdev; // Error handling
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)  
    simdev->class = class_create(CLASS_NAME);
#else  
    simdev->class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(simdev->class)) {
        pr_err("simtemp: failed to create class\n");
        ret = PTR_ERR(simdev->class);
        goto err_cdev_del; 
    }
    
    simdev->class->devnode = simtemp_devnode;

    // Set dev (&pdev->dev) as parent
    // Pass simdev as driver data for sysfs handlers
    simdev->device = device_create(simdev->class, dev /*parent*/, simdev->dev_num,
                                   simdev /*drvdata*/, DEVICE_NAME);
    if (IS_ERR(simdev->device)) {
        pr_err("simtemp: failed to create device node\n");
        ret = PTR_ERR(simdev->device);
        goto err_class_destroy;
    }

    ret = device_create_file(simdev->device, &dev_attr_sampling_ms);
    if (ret) pr_err("simtemp: failed to create sysfs sampling_ms\n");
    
    ret = device_create_file(simdev->device, &dev_attr_temperature);
    if (ret) pr_err("simtemp: failed to create sysfs temperature\n");

    ret = device_create_file(simdev->device, &dev_attr_threshold_flag);
    if (ret) pr_err("simtemp: failed to create sysfs threshold_flag\n");
    
    ret = device_create_file(simdev->device, &dev_attr_threshold_mC);
    if (ret) pr_err("simtemp: failed to create sysfs threshold_mC\n");

    ret = device_create_file(simdev->device, &dev_attr_mode);
    if (ret) pr_err("simtemp: failed to create sysfs mode\n");
    
    ret = device_create_file(simdev->device, &dev_attr_stats);
    if (ret) pr_err("simtemp: failed to create sysfs stats\n");

    timer_setup(&simdev->timer, simtemp_timer_callback, 0);
    mod_timer(&simdev->timer, jiffies + msecs_to_jiffies(simdev->interval_ms));

    pr_info("simtemp: module loaded and probe successful\n");
    return 0; // Success

// handling goto labels for probe PENDING CHECK IF ALLOWED ON KERNEL DEV
err_class_destroy:
    class_destroy(simdev->class);
err_cdev_del:
    cdev_del(&simdev->cdev);
err_unregister_chrdev:
    unregister_chrdev_region(simdev->dev_num, 1);
    pr_err("simtemp: probe failed!\n");
    return ret;
}


// remove function for the platform driver.
static void simtemp_remove(struct platform_device *pdev)
{
    // Get simdev struct from the platform_device
    struct simtemp_dev *simdev = platform_get_drvdata(pdev);

    pr_info("simtemp: remove function called\n");

    del_timer_sync(&simdev->timer);
    

    wake_up_interruptible_all(&simdev->read_queue);
    wake_up_interruptible_all(&simdev->threshold_queue);

    device_remove_file(simdev->device, &dev_attr_sampling_ms);
    device_remove_file(simdev->device, &dev_attr_temperature);
    device_remove_file(simdev->device, &dev_attr_threshold_flag);
    device_remove_file(simdev->device, &dev_attr_threshold_mC);
    device_remove_file(simdev->device, &dev_attr_mode);    
    device_remove_file(simdev->device, &dev_attr_stats);   

    device_destroy(simdev->class, simdev->dev_num);
    
    class_destroy(simdev->class);

    cdev_del(&simdev->cdev);
    
    unregister_chrdev_region(simdev->dev_num, 1);

    pr_info("simtemp: module unloaded\n");
}


#if TEST
    // --- TEST MODE ---
    static int __init simtemp_driver_init(void)
    {
        int ret;
        pr_info("simtemp: Registering platform driver (TEST MODE)\n");
        
        ret = platform_driver_register(&simtemp_platform_driver);
        if (ret) {
            pr_err("simtemp: failed to register platform driver\n");
            return ret;
        }
        
        pr_info("simtemp: Registering local test device\n");
        simtemp_pdev_test = platform_device_register_simple(DEVICE_NAME, -1, NULL, 0);
        
        if (IS_ERR(simtemp_pdev_test)) {
            pr_err("simtemp: failed to register test device\n");
            platform_driver_unregister(&simtemp_platform_driver);
            return PTR_ERR(simtemp_pdev_test);
        }
        return 0; // Success
    }

    static void __exit simtemp_driver_exit(void)
    {
        pr_info("simtemp: Unregistering driver and test device (TEST MODE)\n");
        platform_device_unregister(simtemp_pdev_test);
        platform_driver_unregister(&simtemp_platform_driver);
    }

#else
    // --- MODO PRODUCCIÓN / DT ---
    static int __init simtemp_driver_init(void)
    {
        pr_info("simtemp: Registering platform driver (DT-MODE)\n");
        // Solo registrar el driver. El DT proveerá el dispositivo.
        return platform_driver_register(&simtemp_platform_driver);
    }

    static void __exit simtemp_driver_exit(void)
    {
        pr_info("simtemp: Unregistering platform driver (DT-MODE)\n");
        platform_driver_unregister(&simtemp_platform_driver);
    }
#endif


// Register init and exit functions
module_init(simtemp_driver_init);
module_exit(simtemp_driver_exit); 

// Module information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cesar Ochoa");
MODULE_DESCRIPTION("Simulated temperature sensor (Platform Driver) for NXP Challenge");