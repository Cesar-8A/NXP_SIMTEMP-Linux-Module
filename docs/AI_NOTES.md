# **AI Usage Notes (NXP Challenge)**

This document details how AI assistants chatbots were used as pair-programming partners and for coding, architecture suggestions, and debugging.

I developed this project using two chatbots (Google Gemini and OpenAI GPT) and the next source [https://sysprog21.github.io/lkmpg/\#kernel-module-package](https://sysprog21.github.io/lkmpg/#kernel-module-package) as reference.

## **1\. Role of the AI**

The AI's role was not to "solve the challenge". Instead, it was used as an interactive partner and guide reference.   
Since I did not have previous experience building kernel modules, I started by searching guides and documentation, and asked the AI to suggest useful learning information for the challenge.

Once I knew the basics, I requested an initial code (to use as a blueprint), context, and all validation. The AI's role was to:

1. **Generate blueprints** to use as the initial code to meet specific challenge requirements (as to generate character device and platform device).  
2. **Generate** code for new, well-defined features (like spinlock proper use).  
3. **Debug** runtime errors (like dmesg logs or compile errors) that I provided.  
4. **Improve** documentation structure (like this one) based on the final project results.

## **2\. Overall Development Process**

The project was developed iteratively, this was the initial flow (as example of how I used AI):

1. **Me:** Requested suggestions on some learnings that I should have to achieve the challenge, provided context of my current knowledge and asked for feedback and suggestions on resources for my learnings.  
2. **AI:** Provide some recommendations for learnings and highlighted the most relevant parts.  
3. **Me:** Requested the initial nxp\_simtemp.c/.h blueprints (giving context of the challenge requirements) with basic cdev, , and sysfs logic.  
4. **AI:** Provided the blueprints and suggested a development flow and possible next goals to achieve.  
5. **Me:** Developed the basic initial code based on the blueprints. Then asked the AI for examples of testing through a sh program and proper/safe ways to connect the initial code to a primitive user app (CLI) (for the sh file to run).  
6. **AI:** Generated a Python CLI and run\_demo.sh script to enable testing.  
7. **Me:** Aligned the generated code to my code context and ran the run\_demo.sh, checked dmesg and encountered bugs. I searched for bugs info on stack overflow and requested AI suggestions on solving.  
8. **AI:** Diagnosed the dmesg logs and proposed solutions.  
9. **Me:** Discard some suggestions (due to possible AI hallucinations) and use some others.

Then the flow was basically just following the challenges criteria and suggestions aligned with the AI suggestions as well.

## **3\. Key Prompts and validations**

This section details some key prompts used to guide the AI to help me in what I need and not let mix ideas or mess the code.

### **Prompt: Refactor to platform\_driver**

**Role:** You are a Linux kernel developer specialized in character device drivers.  
**Task:** To convert the initial cdev module into a full platform\_driver and add the required API features.

**Prompt:**

"Act as a senior Linux kernel developer. I have an initial nxp\_simtemp.c and nxp\_simtemp.h (files provided). The code implements a basic cdev with a timer.

Please refactor this code to meet the next challenge requirements:

1. Show how to implement generic full platform\_driver (using platform\_driver\_register, probe, and remove). And explain how to implement that code considering current code.  
2. Show some generic binary read() API: take into account simtemp struct sample. And explain how to implement that code considering current code.  
3. Update the simtemp\_read function to only return this binary struct and validate the len (of current file implementation).  
4. Add the ioctl logic for SIMTEMP\_IOC\_SET\_CONFIG (atomic set) and SIMTEMP\_IOC\_GET\_CONFIG.

**Validation:** Searched on linux kernel book for platform device guide and confirm, in this case the AI hallucinated the sintaxis version so I used the example on the bug mixed with the AI example.

### **Prompt: Creating the CLI and Test Script**

**Role:** You are a Linux kernel developer specialized in character device drivers.  
**Task:** To create the userspace tools needed to test the "test harness."

**Prompt:**

"Generate a Python 3 CLI application (user/cli/main.py) to interact with the nxp\_simtemp driver. It must:

1. Use os.read(fd, STRUCT\_SIZE) to read the binary struct simtemp\_sample.  
2. Use struct.unpack() to decode the binary data.  
3. Use select.poll() to efficiently wait for events. It must handle both POLLIN (new data) and POLLPRI (threshold alert) and print a message for each.  
4. Support arguments for configuring sampling\_ms, mode, and threshold\_mC by writing to sysfs.  
5. Include a \--test mode that sets a known threshold, waits for a POLLPRI event within a timeout, and exits with code 0 (Pass) or 1 (Fail)."

**Validation:** Just read the code and run it to confirm, in this case I did not have to do modifications.

### **Prompt: The Lightweight GUI (Refactoring matplotlib)**

**Role:** You are a Linux kernel developer specialized in character device drivers.  
**Task:** The first GUI with matplotlib was too slow on the Raspberry Pi. This prompt was to refactor it for an embedded-friendly solution.

**Prompt:**

"The matplotlib-based GUI is too slow to load on my Raspberry Pi. Refactor my user/gui/gui.py script.

1. Remove all dependencies on matplotlib and numpy.  
2. Replace the FigureCanvasTkAgg with a standard tk.Canvas widget.  
3. Implement a new redraw\_canvas() function that draws the graph manually.  
4. This function must include helper functions (\_map\_x, \_map\_y) to convert temperature/sample data into (x, y) pixel coordinates.  
5. It must draw the grid, axes, the red threshold line (create\_line), and the blue data line (create\_line).  
6. Ensure the apply\_config function (for changing the threshold) now calls self.redraw\_canvas() instead of the old matplotlib functions."

**Validation:** For this I searched for TKInter examples on the web, since the code had some bugs I searched on stackoverflow for solutions, and asked other AI bot for reference.

### **Prompt: Device Tree Overlay for Raspberry Pi**

**Role:** You are a Linux kernel developer specialized in character device drivers.  
**Task:** The initial .dtsi snippet was generic. It failed to compile on the Pi.

**Prompt:**

"My generic dts/nxp-simtemp.dtsi file is failing to compile on my Raspberry Pi with dtc. The error is syntax error.  
I need to format this file as a proper **Raspberry Pi Device Tree Overlay (.dtbo)**.  
Please generate the correct dts/nxp-simtemp.dtsi content, including the required headers (/dts-v1/, /plugin/), the root node (/), the fragment@0, the target \=\<\&soc\>;, and the \_\_overlay\_\_ { ... } wrapper around my simtemp0 node."

**Validation:** First I tried the AI suggestions, thenI looked at the Raspberry Pi documentation linux kernel guide for debugging the AI mistake (sintaxis).

**Prompt: Kernel Module Freeze**

**Role:** You are a Linux kernel developer specialized in character device drivers.  
**Task:** Help debug a driver where calling cat /dev/simtemp causes the terminal to hang indefinitely.

**Context:**  
The driver uses wait\_event\_interruptible() to block when no new data is available. A kernel thread periodically updates a temperature variable.  
Userspace reads the latest value through read().

**Requirements:**

1. Identify the probable cause for the blocking behavior.  
2. Explain under what condition wake\_up\_interruptible() must be called.  
3. Suggest instrumentation (e.g., pr\_info()) to verify wakeup timing.  
4. Propose a fix ensuring the device properly wakes when new data arrives.

### **Prompt: Spinlock and Concurrency Safety**

**Goal:** Fix race conditions and ensure concurrency safety between kthread and read/write operations.

**Prompt:**

**Role:** Act as a Linux kernel synchronization expert.  
**Task:** Review and debug concurrency issues in a simulated temperature driver.

**Symptoms:**

Kernel occasionally crashes or returns corrupted data when multiple reads/writes occur.  
The module uses a global variable for temperature, accessed by both the kthread and user read().

**Requirements:**

1. Identify where spinlocks or mutexes are required.  
2. Explain why a spinlock may be more appropriate than a mutex in this case.  
3. Show how to protect access to the shared variable (temperature) safely.  
4. Provide an updated code snippet using spin\_lock\_irqsave() / spin\_unlock\_irqrestore().  
5. Ensure the locking mechanism avoids deadlocks and works in interrupt context if needed.