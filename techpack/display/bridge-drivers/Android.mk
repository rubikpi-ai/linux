# Android makefile for display bridge kernel modules
DISPLAY_SELECT := CONFIG_DRM_MSM=m

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# This makefile is only for DLKM
#ifneq ($(findstring vendor,$(LOCAL_PATH)),)

ifneq ($(findstring opensource,$(LOCAL_PATH)),)
	DISPLAY_BLD_DIR := $(TOP)/vendor/qcom/opensource/display-drivers
endif # opensource

DLKM_DIR := $(TOP)/device/qcom/common/dlkm

LOCAL_ADDITIONAL_DEPENDENCIES := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)

# Build lt9611uxc.ko
###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := DISPLAY_ROOT=$(DISPLAY_BLD_DIR)
KBUILD_OPTIONS += MODNAME=lt9611uxc
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
KBUILD_OPTIONS += $(DISPLAY_SELECT)

# Dependency module for lt9611
#KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS+=$(PWD)/$(call intermediates-dir-for,DLKM,msm-ext-disp-module-symvers)/Module.symvers

###########################################################
include $(CLEAR_VARS)
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := lt9611uxc.ko
LOCAL_MODULE_KBUILD_NAME  := lt9611uxc.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)

# Dependency module for lt9611
#LOCAL_REQUIRED_MODULES    += msm-ext-disp-module-symvers
#LOCAL_ADDITIONAL_DEPENDENCIES += $(call intermediates-dir-for,DLKM,msm-ext-disp-module-symvers)/Module.symvers

include $(DLKM_DIR)/Build_external_kernelmodule.mk
###########################################################
#endif # DLKM check
