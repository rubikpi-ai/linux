KBUILD_OPTIONS += CAMERA_KERNEL_ROOT=$(shell pwd)
KBUILD_OPTIONS += KERNEL_ROOT=$(ROOT_DIR)/$(KERNEL_DIR)
KBUILD_OPTIONS += MODNAME=camera

.PHONY: clean all

all: modules

CAMERA_COMPILE_TIME = $(shell date)
CAMERA_COMPILE_HOST = $(shell uname -n)
CAMERA_CC_VERSION = $(shell LC_ALL=C $(CC) --version 2>/dev/null | head -n 1)

cam_generated_h: $(shell find . -iname "*.c") $(shell find . -iname "*.h") $(shell find . -iname "*.mk")
	echo '#define CAMERA_COMPILE_TIME "$(CAMERA_COMPILE_TIME)"' > cam_generated_h
	echo '#define CAMERA_COMPILE_HOST "$(CAMERA_COMPILE_HOST)"' >> cam_generated_h
	echo '#define CAMERA_CC_VERSION "$(CAMERA_CC_VERSION)"' >> cam_generated_h

SRC := $(shell pwd)

modules: cam_generated_h

headers_install:
	echo "Processing target $@"
	IN_DIR=$(HEADERS_DIR)/include/uapi/camera/media/ OUT_DIR=sanitized_headers/camera/media/ bash sanitize_uapi.sh

%:
	echo "Processing glob target $@"
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) $@ $(KBUILD_OPTIONS)

clean:
	rm -f *.o *.ko *.mod.c *.mod.o *~ .*.cmd Module.symvers
	rm -rf .tmp_versions

