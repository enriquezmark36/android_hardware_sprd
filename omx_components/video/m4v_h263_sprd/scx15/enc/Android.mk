LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	SPRDMPEG4Encoder.cpp

LOCAL_C_INCLUDES := \
	frameworks/av/media/libstagefright/include \
	frameworks/native/include/media/openmax \
	frameworks/native/include/media/hardware \
	frameworks/native/include \

LOCAL_HEADER_LIBRARIES := generated_kernel_headers

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

LOCAL_MODULE := libstagefright_sprd_mpeg4enc
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_TAGS := optional

ifeq ($(strip $(TARGET_BOARD_CAMERA_ANTI_SHAKE)),true)
LOCAL_CFLAGS += -DANTI_SHAKE
endif

ifeq ($(strip $(SOC_SCX35)),true)
LOCAL_CFLAGS += -DSOC_SCX35
endif

include $(BUILD_SHARED_LIBRARY)
