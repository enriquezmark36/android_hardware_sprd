LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	SPRDVPXDecoder.cpp

LOCAL_C_INCLUDES := \
	frameworks/av/media/libstagefright/include \
	frameworks/native/include/media/openmax \
	frameworks/native/include/media/hardware \
	frameworks/native/include/ui \
	frameworks/native/include/utils \
	frameworks/native/include/media/hardware \

LOCAL_HEADER_LIBRARIES := generated_kernel_headers

ifeq ($(strip $(SOC_SCX35)),true)
LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/../../../../../gralloc/scx15
else
LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/../../../../../gralloc/$(TARGET_BOARD_PLATFORM)
endif

LOCAL_CFLAGS := \
	-DOSCL_EXPORT_REF= \
	-DOSCL_IMPORT_REF=

LOCAL_ARM_MODE := arm

LOCAL_SHARED_LIBRARIES := \
	libstagefright \
	libstagefright_omx \
	libstagefright_foundation \
	libstagefrighthw \
	libmemoryheapion \
	libmedia \
	libutils \
	liblog \
	libui \
	libdl \
	liblog

LOCAL_STATIC_LIBRARIES := \
	libcolorformat_switcher

LOCAL_MODULE := libstagefright_sprd_vpxdec
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
