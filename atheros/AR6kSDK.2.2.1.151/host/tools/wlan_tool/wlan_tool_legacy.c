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

#define LOG_TAG                         "wlan_tool"
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/wireless.h>
#include <string.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include <a_config.h>
#include <a_osapi.h>
#include <athdefs.h>
#include <a_types.h>
#include <wmi.h>
#include <ar6kap_common.h>
#include "athdrv_linux.h"
#include <ieee80211.h>
#include <ieee80211_ioctl.h>

#define WLAN_PROP_STATUS        "wlan.driver.status"
#define WLAN_IFNAME			"ath0"
#define WLAN_ATHFWLOADER_NAME "athfwloader"

static int chk_set_prop(char *prop_name, char *prop_val)
{
    char prop_status[PROPERTY_VALUE_MAX];
    int count;

    for (count = 100; count != 0; count--) {
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

int athfwloader(int argc, char **argv)
{
    pid_t pid;

    pid = fork();
    if (pid!=0) {
        int ret;
        int status;
        int cnt;      
        cnt = 20;
        while ( (ret=waitpid(pid, &status, WNOHANG)) == 0 && cnt-- > 0 ) {
            LOGD("still waiting...\n");
            sleep(1);
        }
       
        LOGD("waitpid finished ret %d\n", ret);
        if (ret>0) {
           if (WIFEXITED(status)) {
               LOGD("child process exited normally, with exit code %d\n", WEXITSTATUS(status));
           } else {
               LOGD("child process exited abnormally\n");
               goto err_exit;
           }
           return 0;
        }
        goto err_exit;
    } else {        
        char *argv[] = { "/system/wifi/loadfirmware.sh", NULL, };        
        LOGD("Ready to execte %s\n", "/system/wifi/loadfirmware.sh");
        execvp(argv[0], argv);
        _exit(127);
    }
err_exit:
    return -1;
}

int main (int argc, char **argv)
{
	int s, ret;
	struct ifreq ifr;
	char buffer[1756];
	int count = 300; /* 15 sec at most */
    char ifname[PROPERTY_VALUE_MAX];

    if (strcmp(argv[0], WLAN_ATHFWLOADER_NAME)==0) {
        return athfwloader(argc, argv);
    }

    if (!property_get("wifi.interface", ifname, WLAN_IFNAME)) {
        LOGE("Failed to get wlan ifname\n");
        return -1;
    }

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        LOGE("Failed to open socket\n");
        chk_set_prop(WLAN_PROP_STATUS, "failed");
        return -1;
    }

    chk_set_prop(WLAN_PROP_STATUS, "loading");
    do {
        /* Check wmi ready by AR6000_XIOCTL_AP_GET_RTS which is 
           allowed among Infra, Ad-hoc and ap-mode */
	    ((int *)buffer)[0] = AR6000_XIOCTL_AP_GET_RTS;
	    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    ifr.ifr_data = buffer;

		if ( (ret=ioctl(s, AR6000_IOCTL_EXTENDED, &ifr)) >= 0) {
            break;
        }
        usleep(50000);
        count--;
	} while (count > 0 );

	if (ret==0) {
        LOGV("Succeed to get OK status from AR6000");
        chk_set_prop(WLAN_PROP_STATUS, "ok");
    } else {
        LOGE("Fail to get OK status from AR6000");
        chk_set_prop(WLAN_PROP_STATUS, "failed");
    }
    close(s);
	return ret;
}

