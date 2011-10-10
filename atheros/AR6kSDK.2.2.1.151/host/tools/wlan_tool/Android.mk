#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation;
#
# Software distributed under the License is distributed on an "AS
# IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
# implied. See the License for the specific language governing
# rights and limitations under the License.
#
#
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================

ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH:= $(call my-dir)

USE_LEGACY_WLAN_TOOL:=false

ifeq ($(USE_LEGACY_WLAN_TOOL),true)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= wlan_tool_legacy.c
LOCAL_MODULE := wlan_tool
LOCAL_C_INCLUDES := \
        $(LOCAL_PATH)/../../include \
    $(LOCAL_PATH)/../../os/linux/include \
    $(LOCAL_PATH)/../../../include \
    $(LOCAL_PATH)/../../wlan/include
LOCAL_SHARED_LIBRARIES := libcutils
include $(BUILD_EXECUTABLE)

else

include $(CLEAR_VARS)

ALL_PREBUILT += $(TARGET_OUT)/bin/wlan_tool

$(TARGET_OUT)/bin/wlan_tool : $(LOCAL_PATH)/wlan_tool | $(ACP)
	$(transform-prebuilt-to-target)

endif

endif  # TARGET_SIMULATOR != true
