#ifndef _NXP_SIMTEMP_H
#define _NXP_SIMTEMP_H

#include <linux/types.h>

#define SIMTEMP_DEV_NAME "simtemp"
#define SIMTEMP_COMPAT "nxp,simtemp"
#define SIMTEMP_DEFAULT_SAMPLING_MS 1000 // 1s as default 
#define SIMTEMP_DEFAULT_THRESHOLD_mC 90000 // 90.0 C 

struct simtemp_sample {
	__u64 timestamp_ns;   
	__s32 temp_mC;       
	__u32 flags;          /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */
} __attribute__((packed));


struct simtemp_dev;

#endif