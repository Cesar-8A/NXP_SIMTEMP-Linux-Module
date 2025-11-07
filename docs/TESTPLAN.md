# **NXP Simtemp \- Test Plan**

## **1\. Introduction**

This document provides a detailed set of test cases to verify the functionality of the nxp\_simtemp kernel module and its userspace applications (CLI/GUI). The tests are designed to cover all core requirements, acceptance criteria (Section 3), and high-level test suggestions (Section 5\) from the NXP Candidate Challenge document.

## **2\. Test Environments**

This plan uses two distinct environments to test all project features:

1. **Local Test Harness (Ubuntu VM, TEST \= 1):**  
   * **Purpose:** To verify the core driver logic (cdev, sysfs, poll, API contract) and the userspace applications (CLI/GUI).  
   * **Setup:** The driver is compiled with \#define TEST 1\.  
   * **Mechanism:** The driver's init function creates a local platform\_device (platform\_device\_register\_simple), and the driver matches via name (.of\_match\_table \= NULL).  
2. **DT-Mode (Raspberry Pi, TEST \= 0):**  
   * **Purpose:** To verify the "stretch goal" of Device Tree (DT) binding.  
   * **Setup:** The driver is compiled with \#define TEST 0\.  
   * **Mechanism:** A Device Tree Overlay (.dtbo) is loaded at boot. The driver matches via compatible string (.of\_match\_table \= simtemp\_of\_match).

## **3\. Test Cases**

### **Scenario 1: Core Functionality (Local Test Harness)**

Objective: Verify all core requirements (T1-T6) using the automated demo script and manual CLI/GUI tests.  
Environment: Local Test Harness (Ubuntu VM, TEST \= 1).

| Test ID | Description | Test Steps | Expected Result | Status |
| :---- | :---- | :---- | :---- | :---- |
| **T1.1** | **Build Script** (Req 2.3) | 1\. cd to project root. 2\. Run ./scripts/build.sh. | 1\. Script succeeds with "--- Build complete \---". 2\. kernel/nxp\_simtemp.ko file is created. | \[ \] |
| **T1.2** | **Automated Acceptance Test** (Req 2.3, 3.5, T1, T3) | 1\. Run sudo ./scripts/run\_demo.sh. | 1\. Script insmods the driver. 2\. Runs the CLI test (main.py \--test). 3\. CLI test reports **PASS** (verifies poll for POLLPRI). 4\. Script rmmods the driver cleanly. 5\. Final output is **"--- DEMO SUPERADA (PASS) \---"**. | \[ \] |
| **T1.3** | **Manual Load / Unload** (Req 3.1, 3.4, T1) | 1\. Run dmesg \-w in Terminal 1\. 2\. In T2: sudo insmod kernel/nxp\_simtemp.ko. 3\. ls \-l /dev/simtemp and ls \-l /sys/class/simtemp/simtemp/. 4\. In T2: sudo rmmod nxp\_simtemp. | 1\. T1: dmesg shows "probe successful". 2\. T2: Device nodes /dev/simtemp and sysfs files exist. 3\. T1: dmesg shows "remove function called" and "module unloaded" with no errors or warnings. | \[ \] |
| **T2.1** | **Data Path (Periodic Read)** (Req 2.2, 3.2, T2) | 1\. Load module (sudo insmod ...). 2\. Run python3 user/cli/main.py. | 1\. CLI prints live, timestamped data. 2\. Timestamps are **correct (current date/time)**, not "1970". 3\. Data is printed approx. every 1000ms (default). | \[ \] |
| **T2.2** | **API Contract (Partial Read)** (Req 2.1, T6) | 1\. Load module. 2\. Run cat /dev/simtemp. | 1\. Command **must fail**. 2\. Output is: cat: /dev/simtemp: Invalid argument. 3\. This verifies the len \!= sizeof(struct) check in simtemp\_read. | \[ \] |
| **T3.1** | **Config Path (sampling\_ms)** (Req 2.1, 3.3, T2) | 1\. In T1: python3 user/cli/main.py. 2\. In T2: sudo echo 100 \> /sys/class/simtemp/simtemp/sampling\_ms. | 1\. T1: The data output in the CLI speeds up to \~10 samples/sec. 2\. dmesg shows "sampling interval updated to 100 ms". | \[ \] |
| **T3.2** | **Config Path (mode)** (Req 2.1, 3.3) | 1\. In T1: python3 user/cli/main.py. 2\. In T2: sudo echo "ramp" \> /sys/class/simtemp/simtemp/mode. | 1\. dmesg shows "TEMP MODE HAS CHANGED TO ramp MODE". 2\. T1: The temperature values in the CLI output begin to increase steadily. | \[ \] |
| **T3.3** | **Config Path (stats)** (Req 2.1, 3.3, T4) | 1\. Load module and let it run for 5 seconds. 2\. cat /sys/class/simtemp/simtemp/stats. | 1\. Output shows non-zero values for samples\_generated. 2\. If an alert occurred, alerts\_triggered is non-zero. | \[ \] |
| **T4.1** | **Concurrency (Read \+ Write)** (Req 2.1, T5) | 1\. In T1: python3 user/cli/main.py. 2\. In T2: sudo echo "noisy" \> /sys/class/simtemp/simtemp/mode. 3\. In T2: sudo echo 200 \> /sys/class/simtemp/simtemp/sampling\_ms. | 1\. T1 (Reader) **does not crash** or deadlock. 2\. T1 output visibly changes (wider temp range and slower frequency). 3\. dmesg confirms all changes. | \[ \] |

### **Scenario 2: GUI Functionality (Stretch Goal)**

Objective: Verify the optional GUI dashboard functions correctly.  
Environment: Local Test Harness (Ubuntu VM, TEST \= 1).

| Test ID | Description | Test Steps | Expected Result | Status |
| :---- | :---- | :---- | :---- | :---- |
| **T5.1** | **GUI Load & Read** | 1\. Load module (sudo insmod ...). 2\. Run sudo python3 user/gui/gui.py. | 1\. GUI window appears **quickly** (using lightweight Canvas). 2\. Status bar shows "Connected". 3\. Live Temperature (--.- °C) updates to a real value. 4\. Graph starts plotting blue data line. | \[ \] |
| **T5.2** | **GUI Config (Sampling & Mode)** | 1\. In GUI, change "Sampling (ms)" to 500\. 2\. Change "Mode" to ramp. 3\. Click "Apply Config". | 1\. dmesg confirms sampling interval updated to 500 and mode changed to ramp. 2\. Graph data points slow down. 3\. Graph data line shows a clear, steady upward trend. | \[ \] |
| **T5.3** | **GUI Config (Threshold)** | 1\. In GUI, change "Threshold (C)" to 35.0. 2\. Click "Apply Config". | 1\. dmesg confirms threshold\_mC was set to 35000\. 2\. The **red dashed line** on the graph moves up to the 35.0 position. 3\. Status bar updates with new config. | \[ \] |
| **T5.4** | **GUI Alert Visualization** | 1\. Set Mode to normal, Threshold to 28.0. 2\. Wait for a sample to be generated below 28.0°C. | 1\. When a sample (e.g., 26.5°C) is read: 2\. The "Alert" circle indicator turns **red**. | \[ \] |
| **T5.5** | **GUI Close** | 1\. Close the GUI window. | 1\. The window closes cleanly. 2\. The dmesg log does **not** show any kernel errors or warnings. | \[ \] |

### **Scenario 3: Device Tree (DT) Functionality (Stretch Goal)**

Objective: Verify the driver, in production mode, correctly binds to a Device Tree node.  
Environment: DT-Mode (Raspberry Pi, TEST \= 0).

| Test ID | Description | Test Steps | Expected Result | Status |
| :---- | :---- | :---- | :---- | :---- |
| **T6.1** | **Compile DT Overlay** | 1\. On Pi, compile driver with TEST \= 0\. 2\. Run dtc \-@ \-I dts \-O dtb \-o nxp-simtemp.dtbo dts/nxp-simtemp.dtsi. | 1\. Command succeeds (a warning is OK). 2\. nxp-simtemp.dtbo file is created. | \[ \] |
| **T6.2** | **Load DT Overlay** | 1\. sudo cp nxp-simtemp.dtbo /boot/firmware/overlays/ 2\. Add dtoverlay=nxp-simtemp to /boot/firmware/config.txt. 3\. sudo reboot. | 1\. Pi reboots successfully. | \[ \] |
| **T6.3** | **Verify DT-Mode Driver Load** | 1\. After reboot, run dmesg \-w in T1. 2\. In T2: sudo insmod kernel/nxp\_simtemp.ko. (Must be the TEST \= 0 version). | 1\. dmesg **MUST** show the log: DT config loaded (interval=200 ms, threshold=30000 mC). 2\. This proves probe read the values from the .dtbo file. 3\. ls \-l /dev/simtemp shows the device was created. | \[ \] |
| **T6.4** | **Run Demo Script on DT** | 1\. Run sudo ./scripts/run\_demo.sh on the Pi. | 1\. The script should run and report **"--- DEMO SUPERADA (PASS) \---"**. 2\. This proves the full API (sysfs, poll, read) works correctly on the DT-bound device. | \[ \] |
