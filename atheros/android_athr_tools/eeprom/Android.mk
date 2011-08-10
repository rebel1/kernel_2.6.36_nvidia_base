LOCAL_PATH := $(my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	external/athwlan/include 

LOCAL_SRC_FILES:= eeprom-AR6002.c
LOCAL_MODULE := eeprom-AR6002
LOCAL_STATIC_LIBRARIES := libcutils libc
include $(BUILD_EXECUTABLE)


