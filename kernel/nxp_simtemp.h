#ifndef SIMTEMP_H
#define SIMTEMP_H

#include <linux/types.h>
//#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/cdev.h>


#define SIMTEMP_BUFFER_SIZE 16   // ring buffer size


// Structure for representing the simulated temperature device
struct simtemp_dev {
    struct cdev cdev;         // Character device structure
    int temperature;          // Current temperature value
    struct class *class;      // Device class for udev
    struct device *device;    // Device node (/dev/simtemp)

    // Ring buffer
    int buffer[SIMTEMP_BUFFER_SIZE];
    int head;
    int tail;
    int count;

    // for locking buffer reading
    spinlock_t lock;    
    wait_queue_head_t read_queue;  
    wait_queue_head_t threshold_queue;

    
    // Timer for periodic readings simulation
    struct timer_list timer;
    unsigned int interval_ms;

    // For flags threshold
    int threshold_lower;
    bool threshold_flag;
    bool threshold_event;

};

// For forcing 0666 for device file priviledge (non root)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    static char *simtemp_devnode(const struct device *dev, umode_t *mode)
#else
    static char *simtemp_devnode(struct device *dev, umode_t *mode)
#endif
{
    if (mode)
        *mode = 0666;  // rw-rw-rw-
    return NULL;
}




//For random temp generating PENDING, PERHAPS ADD A SINUSOIDAL WAVE AND NOISE TO SIMULATE MORE REALISTIC TEMP CHANGE
/*
static int temp_random_in_range(int min, int max){

    return 0;
}
*/

#endif // SIMTEMP_H