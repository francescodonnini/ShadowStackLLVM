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
KDIR      := /lib/modules/$(shell uname -r)/build
KREL      := $(firstword $(subst -, ,$(shell uname -r)))


HAS_MO := $(shell printf '6.13\n%s\n' '$(KREL)' | sort -V -C && echo y)
ifeq ($(HAS_MO),y)
KBUILD := $(MAKE) -C $(KDIR) M="$(PWD)" MO="$(BUILD_DIR)"
SYNC := @mkdir -p "$(BUILD_DIR)"
COMPDB := cp "$(BUILD_DIR)/compile_commands.json" "$(PWD)/"
else
KBUILD := $(MAKE) -C $(KDIR) M="$(BUILD_DIR)"
SYNC := @mkdir -p "$(BUILD_DIR)" && @rsync -a --exclude=build/ "$(PWD)/" "$(BUILD_DIR)/"
COMPDB := sed 's|$(BUILD_DIR)|$(PWD)|g' "$(BUILD_DIR)/compile_commands.json" > "$(PWD)/compile_commands.json"
endif

all:
	$(SYNC)
	$(KBUILD) modules

compdb: all
	$(KBUILD) compile_commands.json
	$(COMPDB)

mount:
	@insmod "$(BUILD_DIR)/shadowstack_runtime.ko"

umount:
	@rmmod shadowstack_runtime

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M="$(BUILD_DIR)" clean
	rm -rf "$(BUILD_DIR)"

endif