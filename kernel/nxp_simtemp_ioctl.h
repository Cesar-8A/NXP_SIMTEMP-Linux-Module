#ifndef NXP_SIMTEMP_IOCTL_H
#define NXP_SIMTEMP_IOCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>

struct simtemp_sample {
    __u64 timestamp_ns;   /* ktime_get_ns() */
    __s32 temp_mC;        /* milli-degree Celsius (e.g., 44123 = 44.123 C) */
    __u32 flags;          /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */
} __attribute__((packed)); // NEW: Ensure compiler doesn't add padding

// Flag definitions for the .flags field
#define SIMTEMP_FLAG_NEW_SAMPLE (1 << 0)
#define SIMTEMP_FLAG_THRESHOLD_CROSSED (1 << 1)


// ioctl definitions (for atomic config)
#define SIMTEMP_IOC_MAGIC 'p'

// Struct for atomically setting/getting config 
struct simtemp_config {
    __u32 sampling_ms;
    __s32 threshold_mC;
};

#define SIMTEMP_IOC_SET_CONFIG _IOW(SIMTEMP_IOC_MAGIC, 1, struct simtemp_config)
#define SIMTEMP_IOC_GET_CONFIG _IOR(SIMTEMP_IOC_MAGIC, 2, struct simtemp_config)


#endif // NXP_SIMTEMP_IOCTL_H