ATHEROS_TOOL_PATH := $(call my-dir)/android_athr_tools
ifeq ($(BOARD_HAVE_BLUETOOTH),true)
include $(ATHEROS_TOOL_PATH)/athbtfilter/Android.mk
endif
include $(ATHEROS_TOOL_PATH)/wmiconfig/Android.mk
