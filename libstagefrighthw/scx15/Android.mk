#
# Copyright (C) 2016 The Android Open Source Project
# Copyright (C) 2016 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	SprdOMXPlugin.cpp \
	SprdOMXComponent.cpp \
	SprdSimpleOMXComponent.cpp

LOCAL_CFLAGS := \
	$(PV_CFLAGS_MINUS_VISIBILITY) \

LOCAL_C_INCLUDES := \
	frameworks/native/include/media/openmax \
	frameworks/native/include/media/hardware \
	$(LOCAL_PATH)/include \

ifeq ($(strip $(SOC_SCX35)),true)
LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/../../gralloc/scx15
else
LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/../../gralloc/$(TARGET_BOARD_PLATFORM)
endif

LOCAL_C_INCLUDES += \
	$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/video/ \

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(LOCAL_PATH)/include \

LOCAL_SHARED_LIBRARIES := \
	libmemoryheapion \
	libutils \
	libcutils \
	libui \
	libdl \
	liblog \
	libstagefright_foundation \

LOCAL_MODULE := libstagefrighthw

LOCAL_PROPRIETARY_MODULE := true

LOCAL_CFLAGS:= -DLOG_TAG=\"$(TARGET_BOARD_PLATFORM).libstagefright\"

ifeq ($(strip $(SOC_SCX35)),true)
LOCAL_CFLAGS += -DSOC_SCX35
endif

include $(BUILD_SHARED_LIBRARY)
