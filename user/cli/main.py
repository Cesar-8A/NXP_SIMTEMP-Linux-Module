#!/usr/bin/env python3

import os
import fcntl
import struct
import select
import sys
import argparse
import time
from datetime import datetime

# Define the binary structure (must match nxp_simtemp_ioctl.h!)
# __u64 timestamp_ns -> 'Q' (unsigned long long, 8 bytes)
# __s32 temp_mC      -> 'i' (signed int, 4 bytes)
# __u32 flags        -> 'I' (unsigned int, 4 bytes)
STRUCT_FORMAT = 'Q i I'
STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)

# Flags (must match nxp_simtemp_ioctl.h)
SIMTEMP_FLAG_NEW_SAMPLE = (1 << 0)
SIMTEMP_FLAG_THRESHOLD_CROSSED = (1 << 1)

# Sysfs paths (assuming it's mounted at /sys/class/simtemp/simtemp)
SYSFS_PATH = "/sys/class/simtemp/simtemp"
DEVICE_PATH = "/dev/simtemp"

def sysfs_write(attr, value):
    """Write a value to a sysfs attribute."""
    path = os.path.join(SYSFS_PATH, attr)
    try:
        with open(path, 'w') as f:
            f.write(str(value))
        # print(f"SYSFS: Set {attr} = {value}")
    except IOError as e:
        print(f"Error writing to sysfs {path}: {e}", file=sys.stderr)
        print("Is the module loaded? (sudo insmod)", file=sys.stderr)
        sys.exit(1)

def sysfs_read(attr):
    """Read a value from a sysfs attribute."""
    path = os.path.join(SYSFS_PATH, attr)
    try:
        with open(path, 'r') as f:
            return f.read().strip()
    except IOError as e:
        print(f"Error reading from sysfs {path}: {e}", file=sys.stderr)
        sys.exit(1)

def run_monitor(dev_fd):
    """Main monitoring loop using poll."""
    print(f"Monitoring {DEVICE_PATH} (struct size={STRUCT_SIZE} bytes)...")
    print("Timestamp (ISO)         | Temp (C) | Alert")
    print("-" * 50)

    # Create a poll object
    poller = select.poll()
    # Register the file descriptor for:
    # POLLIN | POLLRDNORM -> Data ready to read
    # POLLPRI              -> Priority event (our threshold)
    poller.register(dev_fd, select.POLLIN | select.POLLRDNORM | select.POLLPRI)

    while True:
        try:
            # Wait indefinitely for an event
            events = poller.poll()
            
            for fd, event in events:
                # --- Threshold Alert Event (POLLPRI) ---
                if event & select.POLLPRI:
                    print(f"!!! THRESHOLD EVENT (POLLPRI) RECEIVED !!!")

                # --- Data Ready Event (POLLIN) ---
                if event & (select.POLLIN | select.POLLRDNORM):
                    # Read the exact number of bytes for our struct
                    binary_data = os.read(dev_fd, STRUCT_SIZE)
                    
                    if len(binary_data) == 0:
                        print("End of file (Is the module unloaded?). Exiting.")
                        return
                    
                    if len(binary_data) != STRUCT_SIZE:
                        print(f"Short read: {len(binary_data)}/{STRUCT_SIZE} bytes", file=sys.stderr)
                        continue

                    # Unpack the binary data
                    timestamp, temp, flags = struct.unpack(STRUCT_FORMAT, binary_data)
                    
                    # Format the output
                    temp_c = temp / 1000.0
                    ts_iso = datetime.fromtimestamp(timestamp / 1e9).isoformat(timespec='milliseconds')
                    
                    # Check the alert flag in the data
                    alert_flag = bool(flags & SIMTEMP_FLAG_THRESHOLD_CROSSED)
                    
                    print(f"{ts_iso} | {temp_c:8.3f} | {alert_flag}")

        except KeyboardInterrupt:
            print("\nMonitoring stopped by user.")
            break
        except Exception as e:
            print(f"Error in poll loop: {e}", file=sys.stderr)
            break

def run_test_mode():
    """
    Test mode (T3/T5 from the challenge):
    1. Set a low threshold (e.g. 30C)
    2. Set a fast sampling rate (100ms)
    3. Wait (with poll) for a POLLPRI event
    4. Fail (exit 1) if it doesn't occur in 3 periods.
    5. Succeed (exit 0) if it occurs.
    """
    print("--- Starting Test Mode (Acceptance Test) ---")
    
    # 1. Configure the device for the test
    #    (Assume 'normal' mode generates 25-35C)
    test_threshold = 30000 # 30.0 C (should trigger)
    test_period_ms = 100   # 100 ms
    
    print(f"Configuring: sampling_ms={test_period_ms}, threshold_mC={test_threshold}")
    sysfs_write("mode", "normal")
    sysfs_write("sampling_ms", test_period_ms)
    sysfs_write("threshold_mC", test_threshold)
    
    timeout_sec = (test_period_ms * 3) / 1000.0 + 0.1 # 3 periods + margin
    
    try:
        # Open the device
        fd = os.open(DEVICE_PATH, os.O_RDONLY)
        
        poller = select.poll()
        poller.register(fd, select.POLLPRI) # We only care about the alert
        
        print(f"Waiting for POLLPRI event (alert) (timeout={timeout_sec}s)...")
        
        # 2. Wait for the event
        # poll() takes the timeout in milliseconds
        events = poller.poll(timeout_sec * 1000)
        
        if not events:
            print("\n--- TEST FAILED (FAIL) ---")
            print("Timeout: The POLLPRI event (threshold alert) was not received.")
            print(f"Current stats: {sysfs_read('stats')}")
            return False

        # 3. Check the event
        for _, event in events:
            if event & select.POLLPRI:
                print("\n--- TEST PASSED (PASS) ---")
                print("POLLPRI event (threshold alert) received correctly.")
                return True
            else:
                print(f"Unexpected event: {event}")
        
        print("\n--- TEST FAILED (FAIL) ---")
        print("poll() woke up but the event was not POLLPRI.")
        return False

    except Exception as e:
        print(f"\n--- TEST FAILED (FAIL) ---")
        print(f"Error during the test: {e}", file=sys.stderr)
        return False
    finally:
        if 'fd' in locals():
            os.close(fd)
        # Restore configuration
        sysfs_write("sampling_ms", 1000)
        sysfs_write("threshold_mC", 27000)

def main():
    parser = argparse.ArgumentParser(description="CLI App for NXP simtemp driver")
    parser.add_argument(
        '--test', 
        action='store_true', 
        help="Run the acceptance test mode (T3/T5)."
    )
    parser.add_argument(
        '-s', '--set-sampling-ms', 
        type=int, 
        metavar="MS",
        help="Set the sampling interval (ms) via sysfs"
    )
    parser.add_argument(
        '-t', '--set-threshold-mc', 
        type=int, 
        metavar="MC",
        help="Set the threshold alert (milli-C) via sysfs"
    )
    parser.add_argument(
        '-m', '--set-mode', 
        type=str, 
        choices=['normal', 'noisy', 'ramp'],
        help="Set the simulation mode via sysfs"
    )
    
    args = parser.parse_args()

    # --- Test Mode ---
    if args.test:
        if not run_test_mode():
            sys.exit(1) # Exit with error for the demo script
        sys.exit(0)

    # --- Configuration Mode ---
    if args.set_sampling_ms is not None:
        sysfs_write("sampling_ms", args.set_sampling_ms)
    if args.set_threshold_mc is not None:
        sysfs_write("threshold_mC", args.set_threshold_mc)
    if args.set_mode:
        sysfs_write("mode", args.set_mode)

    # If only configuration was set, don't monitor
    if any([args.set_sampling_ms, args.set_threshold_mc, args.set_mode]):
        print("Configuration updated. Current stats:")
        print(sysfs_read("stats"))
        sys.exit(0)

    # --- Monitoring Mode (default) ---
    try:
        # Open the character device
        # We use os.open to get a file descriptor (int)
        # which is what 'select.poll()' needs.
        fd = os.open(DEVICE_PATH, os.O_RDONLY | os.O_NONBLOCK)
    except Exception as e:
        print(f"Error opening {DEVICE_PATH}: {e}", file=sys.stderr)
        print("Is the module loaded? (sudo insmod)", file=sys.stderr)
        sys.exit(1)

    run_monitor(fd)
    
    # Close the file descriptor
    os.close(fd)

if __name__ == "__main__":
    main()
