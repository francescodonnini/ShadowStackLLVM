ifneq ($(KERNELRELEASE),)

obj-m += shadowstack_runtime.o

shadowstack_runtime-objs := main.o \
                            core/chrdev.o \
							core/chrdev_fops.o \
                            core/chrdev_ioctl.o \
							core/debug.o \
                            core/api.o \
							core/pt_bindings.o

ccflags-y += -I$(src)/include
CFLAGS_main.o += -DDEBUG

else

PWD := $(CURDIR)
BUILD_DIR := $(PWD)/build

all:
	@mkdir -p "$(BUILD_DIR)"
	@rsync -a --exclude=build/ "$(PWD)/" "$(BUILD_DIR)/"
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M="$(BUILD_DIR)" modules

mount:
	@insmod "$(BUILD_DIR)/shadowstack_runtime.ko"

umount:
	@rmmod shadowstack_runtime

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M="$(BUILD_DIR)" clean
	rm -rf "$(BUILD_DIR)"

endif