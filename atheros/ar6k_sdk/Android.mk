#------------------------------------------------------------------------------
# <copyright file="Android.mk" company="Atheros">
#    Copyright (c) 2010 Atheros Corporation.  All rights reserved.
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

LOCAL_PATH := $(call my-dir)
ath6k_firmware_files := athwlan.bin.z77 data.patch.hw2_0.bin eeprom.bin eeprom.data

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/host/os/linux/ar6000.ko:system/lib/hw/wlan_ar6002/ar6000.ko
copy_file_list := \
        $(foreach f, $(ath6k_firmware_files),\
            $(LOCAL_PATH)/target/$(f):system/lib/hw/wlan_ar6002/$(f))
PRODUCT_COPY_FILES += $(copy_file_list)


# make it default driver
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/host/os/linux/ar6000.ko:system/lib/hw/wlan/ar6000.ko
copy_file_list := \
        $(foreach f,$(ath6k_firmware_files),\
            $(LOCAL_PATH)/target/$(f):system/lib/hw/wlan/$(f))
PRODUCT_COPY_FILES += $(copy_file_list)
