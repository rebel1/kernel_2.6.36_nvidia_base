/*
 * Copyright (c) 2004-2009 Atheros Communications Inc.
 * All rights reserved.

 *
 * 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// Software distributed under the License is distributed on an "AS
// IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
// implied. See the License for the specific language governing
// rights and limitations under the License.
//
//
 *
 *
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <string.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include "../include/athtypes_linux.h"
#include "../include/athdrv_linux.h"

#define LOG_TAG             "wlan_tool"
#define WLAN_PROP_STATUS        "wlan.driver.status"
#define WLAN_IFNAME         "athwlan0"

static int chk_set_prop(char *prop_name, char *prop_val)
{
    char prop_status[PROPERTY_VALUE_MAX];
    int count;

    for (count = 10; count != 0; count--) {
        property_set(prop_name, prop_val);
        if( property_get(prop_name, prop_status, NULL) &&
            (strcmp(prop_status, prop_val) == 0) )
        break;
    }
    if (count) {
        LOGV("Succeed to set property [%s] to [%s]\n", prop_name, prop_val);
    } else {
        LOGE("Fail to set property [%s] to [%s]\n", prop_name, prop_val);
    }
    return( count );
}

int main (int argc, char **argv)
{
    int s, left_time;
    struct ifreq ifr;
    char buffer[16] = {0};
    int count = 300; /* 15 sec at most */

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        LOGE("Failed to open socket\n");
        chk_set_prop(WLAN_PROP_STATUS, "failed");
        return -1;
    }

    ((int *)buffer)[0] = AR6000_XIOCTL_BMI_TEST;
    strncpy(ifr.ifr_name, WLAN_IFNAME, sizeof(ifr.ifr_name));
    ifr.ifr_data = buffer;

    chk_set_prop(WLAN_PROP_STATUS, "loading");
    while (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {

        usleep(50000);
        count--;
        if (count < 0) {
            LOGE("Timeout to get status from AR6000!");
            chk_set_prop(WLAN_PROP_STATUS, "failed");
            close(s);
            return -1;
        }
    }

    left_time = ((int *)buffer)[0];

    if (left_time) {
        LOGV("Succeed to get OK status from AR6000");
        chk_set_prop(WLAN_PROP_STATUS, "ok");
    } else {
        LOGE("Fail to get OK status from AR6000");
        chk_set_prop(WLAN_PROP_STATUS, "failed");
    }

    return 0;
}

