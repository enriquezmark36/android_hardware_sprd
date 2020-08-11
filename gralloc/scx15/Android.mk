# 
# Copyright (C) 2010 ARM Limited. All rights reserved.
# 
# Copyright (C) 2008 The Android Open Source Project
#
# Copyright (C) 2016 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_PROPRIETARY_MODULE := true

LOCAL_MODULE := gralloc.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_TAGS := optional

SHARED_MEM_LIBS := \
	libion_sprd \
	libhardware

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libGLESv1_CM \
	$(SHARED_MEM_LIBS) \

LOCAL_C_INCLUDES := \
	$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/video/ \
	$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/ \
	$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/ \

LOCAL_ADDITIONAL_DEPENDENCIES += \
	$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr \

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(LOCAL_PATH) \
	$(LOCAL_C_INCLUDES) \

LOCAL_CFLAGS := \
	-DLOG_TAG=\"gralloc.$(TARGET_BOARD_PLATFORM)\" \

ifeq ($(strip $(USE_UI_OVERLAY)),true)
LOCAL_CFLAGS += -DUSE_UI_OVERLAY
endif

ifneq ($(strip $(TARGET_BUILD_VARIANT)),user)
LOCAL_CFLAGS += -DDUMP_FB
endif

ifeq ($(USE_SPRD_DITHER),true)
LOCAL_CFLAGS += -DSPRD_DITHER_ENABLE
LOCAL_SHARED_LIBRARIES += libdither
endif

##
## HIDL workarounds
##
ifeq ($(TARGET_USES_HIDL_WORKAROUNDS), true)
# Creates a dummy fd when travelling via hwbinder
LOCAL_CFLAGS += -DHIDL_INVALID_FD

# Prevent HIDL from "freeing" FB memory at all.
# Pun intended.
LOCAL_CFLAGS += -DHIDL_NO_FREE_FB
endif

##
## Optimizations and tweaks
##

# Forces HWC layer buffers to be contiguous as much as possible.
# This allows devices without IOMMU to use GSP in the Hardware Composer.
# Will fallback to virtual memory allocation if it fails.
ifeq ($(TARGET_FORCE_HWC_CONTIG), true)
LOCAL_CFLAGS += -DFORCE_HWC_CONTIG

# Indicates that the ION_HEAP_ID_MASK_OVERLAY is in a carveout
# and the ION_HEAP_ID_MASK_MM is in CMA.
# Should be safe to use even when this assumption is false.
#
# This tweaks the logic a bit and forces all GRALLOC_USAGE_OVERLAY_BUFFER
# to MM and all HWC buffer to the carveout.
#
# Will also enable TARGET_ION_MM_FALLBACK if not set so that the OVERLAY will act
# as a fallback buffer when an MM allocation fails.
#
# Lastly, when allocation for the HWC fail, it will be retried on MM.
# Only when it fails again, then it falls back to virtual memory allocation.
ifeq ($(TARGET_ION_OVERLAY_IS_CARVEOUT), true)
LOCAL_CFLAGS += -DION_OVERLAY_IS_CARVEOUT

	# Set TARGET_ION_MM_FALLBACK to true if not set
	ifeq ($(TARGET_ION_MM_FALLBACK), )
	TARGET_ION_MM_FALLBACK := true
	endif
endif
endif

# When allocation fails under ION_HEAP_ID_MASK_MM, retry again
# under ION_HEAP_ID_MASK_OVERLAY supposing the two are in two
# separate memory pools.
ifeq ($(TARGET_ION_MM_FALLBACK), true)
LOCAL_CFLAGS += -DALLOW_MM_FALLBACK
endif

# Indicates that the kernel has an extra ION carveout at mask 0x16
# and can be used to retry failed allocations.
# WARNING: The kernel MUST have that extra carveout or it may cause problems.
ifeq ($(TARGET_HAS_RESERVED_CARVEOUT), true)
LOCAL_CFLAGS += -DKERNEL_HAS_RESERVED_CARVEOUT
endif

# Make Gralloc assume the FB have 3 screen buffers (triple buffers)
# An FB IOCTL fails when gralloc uses double buffers. If that fails,
# Gralloc also fails to set the appropiate parameters like the xdpi/ydpi.
ifeq ($(TARGET_USE_3_FRAMEBUFFER), true)
LOCAL_CFLAGS += -DUSE_3_FRAMEBUFFER
endif

LOCAL_SRC_FILES := \
	gralloc_module.cpp \
	alloc_device.cpp \
	framebuffer_device.cpp \
	dump_bmp.cpp \

#LOCAL_CFLAGS+= -DMALI_VSYNC_EVENT_REPORT_ENABLE
include $(BUILD_SHARED_LIBRARY)

endif
