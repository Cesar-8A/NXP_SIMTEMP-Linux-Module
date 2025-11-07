#
# NEW: This entire file is new.
# Makefile for building the 'nxp_simtemp' out-of-tree kernel module.
#

# obj-m specifies the kernel object file to build.
# The build system will look for nxp_simtemp.c and compile it.
obj-m := nxp_simtemp.o

# KDIR: The location of the kernel source/headers tree.
# We read it from the environment, or default to the running kernel's build dir.
KDIR ?= /lib/modules/$(shell uname -r)/build

# PWD: The current working directory (our module's source directory)
PWD := $(shell pwd)

# Default target (called when you just run 'make')
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Target to clean up built files
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Target for help
help:
	$(MAKE) -C $(KDIR) M=$(PWD) help

.PHONY: all clean help