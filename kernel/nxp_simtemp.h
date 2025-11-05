#ifndef SIMTEMP_H
#define SIMTEMP_H

#include <linux/types.h>

// Structure for representing the simulated temperature device
struct simtemp_dev {
    struct cdev cdev;         // Character device structure
    int temperature;          // Current temperature value
    struct class *class;      // Device class for udev
    struct device *device;    // Device node (/dev/simtemp)
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
