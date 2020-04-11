LOCAL_PATH := $(call my-dir)

ifneq ($(filter $(TARGET_BOARD_PLATFORM), scx15) $(filter true, $(SOC_SCX35)),)
include $(call all-makefiles-under,$(LOCAL_PATH))
endif


