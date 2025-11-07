# **NXP 'simtemp' Virtual Sensor Challenge**

Solution by: Cesar Ochoa  
Date: November 6, 2025  

This repository contains my solution to the "NXP Systems Software Engineer Candidate Challenge." This project implements a virtual temperature sensor (nxp_simtemp) as a dual-mode platform_driver in the Linux kernel, complete with user-space CLI and GUI applications for control and visualization.

## **Submission Links (For Reviewer)**

* **Git Repository:**
* **Video Demo (2-3 min):** 

## **1. Implemented Features**

This project fulfills all core requirements and several stretch goals:

* **Kernel Driver (nxp_simtemp.ko):**
  * Implemented as a platform_driver that binds via name (for local testing) or Device Tree (for production).
  * **Dual-Mode Build:** Can be compiled for "Local Test Mode" (TEST = 1) or "DT Mode" (TEST = 0) by changing a singleb #define TEST flag.
* **cdev API:** Exposes /dev/simtemp for **binary** reads (struct simtemp_sample).
* **poll() API:** Implements efficient (0% CPU) poll() support for two distinct events:
  * POLLIN (New data available).
  * POLLPRI (Threshold cross event).
* **sysfs API:** Full controls under /sys/class/simtemp/simtemp/:
  * sampling_ms (RW): Controls the timer interval.
  * threshold_mC (RW): Configures the alert threshold in milli-Celsius.
  * mode (RW): Controls the generator (normal, noisy, ramp).
  * stats (RO): Exposes sample, alert, and error counters.
* **ioctl API:** Includes ioctl for atomic configuration (demonstration).
* **CLI Application (user/cli/main.py):**
  * A full-featured tool to monitor, configure, and test the driver.
  * Includes an acceptance test mode (--test) used by the demo script.
* **GUI Application:**
  * A user/gui/gui.py dashboard (Python/Tkinter) featuring a multi-threaded architecture (GUI thread + Worker thread).
  * Visualizes live temperature and threshold on a real-time **Matplotlib graph**.
  * Provides GUI controls for sampling_ms, threshold_mC, and mode.
* **Device Tree Support:**
  * The driver (in TEST = 0 mode) implements of_match_table binding and reads properties (sampling-ms, threshold-mC) from the DT.  
  * An overlay snippet (dts/nxp-simtemp.dtsi) is provided, ready for QEMU or Raspberry Pi?.  
* **Support Scripts:**
  * scripts/build.sh: Builds the driver.
  * scripts/run_demo.sh: Automated acceptance test script.

## **2\. Repository Structure**

simtemp/  
├─ kernel/  
│  ├─ nxp_simtemp.c       \# (Dual-mode driver: TEST=1 or TEST=0)  
│  ├─ nxp_simtemp.h  
│  ├─ nxp_simtemp_ioctl.h \# (Binary/ioctl API)  
│  ├─ Makefile  
├─ user/  
│  ├─ cli/  
│  │  └─ main.py          \# (CLI with \--test mode)  
│  └─ gui/    
│     └─ gui.py           \# (Optional GUI with Matplotlib)  
├─ scripts/  
│  ├─ build.sh           \# (Build script)  
│  ├─ run\_demo.sh        \# (Acceptance test script)  
│  └─ lint.sh            \# (Optional: style linter)  
├─ dts/  
│  └─ nxp-simtemp.dtsi     \# (Device Tree snippet for stretch   goal)  
└─ docs/  
   ├─ README.md  
   ├─ DESIGN.md          \# (Architecture diagram and design   decisions) ONGOING  
   ├─ TESTPLAN.md        \# (Test Plan)    
   └─ AI_NOTES.md        \# (Notes on AI usage) ONGOING  

## **3. Prerequisites (Ubuntu LTS)**

sudo apt update  
sudo apt install build-essential linux-headers-$(uname -r)
                 python3-tk python3-matplotlib python3-pip  
Also install the python requirements file

## **4. How to Compile**

The driver is currently in **Local Test Mode (TEST = 1)**, ready to run on a development VM.  
./scripts/build.sh  
This will compile kernel/nxp_simtemp.ko, the module can be inserted now

## **5 How to Test (Start Here!)**

We provide three ways to test the driver, from simplest to most advanced.

###### **A. Acceptance Test** ----------------

This script automates the build, load, test  
# Run as root (required for insmod/rmmod and sysfs)
sudo ./scripts/run_demo.sh

### **B. Manual GUI Test (Recommended for Demo)**

This is the best way to *see* the driver in action.
**Terminal 1: Load the Module**
- Load the driver (compiled with TEST = 1)
sudo insmod kernel/nxp_simtemp.ko

- (Optional) Watch kernel logs to see the "probe"
sudo dmesg -w

**Terminal 2: Run the GUI**
- Run the GUI with sudo (for sysfs permissions)
sudo python3 user/gui/gui.py (or CLI if preferred)

* **What to look for:**
  1. The temperature graph will move in real-time (by default it provides record of the last 50 samples,
  change MAX_SAMPLES on code CONSTATS section if more or less samples required).
  2. Change the "Mode" to ramp, noisy or normal to change the way temperature data are generated.
  3. Change the "Threshold (C)" (e.g., from 27.0 to 30.0) and press "Apply". You will see the red threshold line on the graph move and the alarm light turn red if threshold passed.

**To Clean Up (in Terminal 1):**
sudo rmmod nxp_simtemp

###### **C. Stretch Goal Test (Production Mode - Device Tree)** ----------------

This test verifies the driver works in an embedded (ARM) environment and binds to the Device Tree.

To test driver on DT Binding nxp-simtemp.dtsi some extra steps must be done:
- compiled to a dtbo using device-tree-compiler
- inserted into firmware overlays
- add to boot firmware config file
- reebot, now excecute ./scripts/build.sh and insert module

1. **Edit the Driver:**
   * Change kernel/nxp_simtemp.c to #define TEST 0.
2. **Follow the Guide:**
   * Load the .dtbo overlay, and test.
3. **Verify:**
   * When running insmod on the ARM arquitecture, dmesg will show:
     simtemp: DT config loaded (interval=200 ms, threshold=30000 mC)
   * This proves the driver read its configuration from dts/nxp-simtemp.dtsi.

## **6. Additional Documentation**

* **docs/DESIGN.md:** Explains the architecture, block diagram, locking choices (spinlock vs. spin_lock_bh), and API tradeoffs (sysfs vs. ioctl).
* **docs/TESTPLAN.md:** Details test plan
* **docs/AI_NOTES.md:** Details how AI was used for code review, debugging, and documentation.