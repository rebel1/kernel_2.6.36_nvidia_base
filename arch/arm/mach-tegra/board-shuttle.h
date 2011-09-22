/*
 * arch/arm/mach-tegra/board-shuttle.h
 *
 * Copyright (C) 2011 Eduardo José Tagle <ejtagle@tutopia.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _MACH_TEGRA_BOARD_SHUTTLE_H
#define _MACH_TEGRA_BOARD_SHUTTLE_H


#define SHUTTLE_BT_RESET 		TEGRA_GPIO_PU0 	/* 0= reset asserted */

/* GPS and 3G cards share the same enabling IO line */
#define SHUTTLE_3GGPS_DISABLE  	TEGRA_GPIO_PJ7 	/* 1= disabled*/

#define SHUTTLE_KEY_VOLUMEUP 	TEGRA_GPIO_PB1 	/* 0=pressed */
#define SHUTTLE_KEY_VOLUMEDOWN 	TEGRA_GPIO_PK7 	/* 0=pressed */
#define SHUTTLE_KEY_POWER 		TEGRA_GPIO_PV2 	/* 0=pressed */
#define SHUTTLE_KEY_RESUME 		TEGRA_GPIO_PV6 	/* 0=pressed */
#define SHUTTLE_KEY_SUSPEND		TEGRA_GPIO_PAA4 /* 0=pressed */
#define SHUTTLE_KEY_BACK		TEGRA_GPIO_PH0	/* 0=pressed */

/* #define SHUTTLE_EMC_SAMSUNG		*/
/* #define SHUTTLE_EMC_ELPIDA50NM	*/
/* #define SHUTTLE_EMC_ELPIDA40NM	*/

#define SHUTTLE_CAMERA_POWER 	TEGRA_GPIO_PD4 /* 1=powered on */

#define SHUTTLE_NAND_WPN		TEGRA_GPIO_PC7	/* NAND flash write protect: 0=writeprotected */

#define SHUTTLE_BL_ENB			TEGRA_GPIO_PB5
#define SHUTTLE_LVDS_SHUTDOWN	TEGRA_GPIO_PB2
#define SHUTTLE_EN_VDD_PANEL	TEGRA_GPIO_PC6
#define SHUTTLE_BL_VDD			TEGRA_GPIO_PW0
#define SHUTTLE_BL_PWM			TEGRA_GPIO_PB4 /* PWM */
#define SHUTTLE_HDMI_ENB		TEGRA_GPIO_PV5 /* unconfirmed */
#define SHUTTLE_HDMI_HPD		TEGRA_GPIO_PN7 /* 1=HDMI plug detected */

#define SHUTTLE_BL_PWM_ID		0				/* PWM0 controls backlight */

#define SHUTTLE_FB_PAGES		2				/* At least, 2 video pages */
#define SHUTTLE_FB_HDMI_PAGES	2				/* At least, 2 video pages for HDMI */

#define SHUTTLE_MEM_SIZE 		SZ_512M			/* Total memory */

#define SHUTTLE_GPU_MEM_SIZE 		SZ_128M	/* Memory reserved for GPU */
/*#define SHUTTLE_GPU_MEM_SIZE 	SZ_64M*/			/* Memory reserved for GPU */

#define SHUTTLE_FB1_MEM_SIZE 	SZ_8M			/* Memory reserved for Framebuffer 1: LCD */
#define SHUTTLE_FB2_MEM_SIZE 	SZ_8M			/* Memory reserved for Framebuffer 2: HDMI out */

#define DYNAMIC_GPU_MEM 1						/* use dynamic memory for GPU */


/*#define SHUTTLE_48KHZ_AUDIO*/ /* <- define this if you want 48khz audio sampling rate instead of 44100Hz */

// TPS6586x GPIOs as registered 
#define PMU_GPIO_BASE		(TEGRA_NR_GPIOS) 
#define PMU_GPIO0 			(PMU_GPIO_BASE)
#define PMU_GPIO1 			(PMU_GPIO_BASE + 1) 
#define PMU_GPIO2 			(PMU_GPIO_BASE + 2)
#define PMU_GPIO3 			(PMU_GPIO_BASE + 3)

#define PMU_IRQ_BASE		(TEGRA_NR_IRQS)
#define PMU_IRQ_RTC_ALM1 	(PMU_IRQ_BASE + TPS6586X_INT_RTC_ALM1)

#define	SHUTTLE_ENABLE_VDD_VID	TEGRA_GPIO_PT2	/* 1=enabled.  Powers HDMI. Wait 500uS to let it stabilize before returning */

#define SHUTTLE_SDIO2_CD	TEGRA_GPIO_PI5
#define SHUTTLE_SDIO2_POWER	TEGRA_GPIO_PT3	/* SDIO0 and SDIO2 power */

#define SHUTTLE_SDHC_CD		TEGRA_GPIO_PH2
#define SHUTTLE_SDHC_WP		TEGRA_GPIO_PH3	/*1=Write Protected */
#define SHUTTLE_SDHC_POWER	TEGRA_GPIO_PI6

#define SHUTTLE_TS_IRQ		TEGRA_GPIO_PB6
#define SHUTTLE_TS_DISABLE	TEGRA_GPIO_PAA6 /* 0=enabled */

#define SHUTTLE_FB_NONROTATE TEGRA_GPIO_PH1 /*1 = screen rotation locked */

#define SHUTTLE_WLAN_POWER 	TEGRA_GPIO_PK5
#define SHUTTLE_WLAN_RESET 	TEGRA_GPIO_PK6

#define SHUTTLE_LOW_BATT	TEGRA_GPIO_PW3 /*(0=low battery)*/
#define SHUTTLE_IN_S3		TEGRA_GPIO_PAA7 /*1 = in S3 */

#define SHUTTLE_USB0_VBUS	TEGRA_GPIO_PB0		/* 1= VBUS usb0 */
#define SHUTTLE_USB1_RESET		TEGRA_GPIO_PV1	/* 0= reset */

#define SHUTTLE_HP_DETECT	TEGRA_GPIO_PW2 	/* HeadPhone detect for audio codec: 1=Hedphone plugged */

#define SHUTTLE_NVEC_REQ	TEGRA_GPIO_PD0	/* Set to 0 to send a command to the NVidia Embedded controller */
#define SHUTTLE_NVEC_I2C_ADDR 0x8a 			/* I2C address of Tegra, when acting as I2C slave */

#define SHUTTLE_WAKE_KEY_POWER  TEGRA_WAKE_GPIO_PV2
#define SHUTTLE_WAKE_KEY_RESUME TEGRA_WAKE_GPIO_PV6

/* The switch used to indicate rotation lock */
#define SW_ROTATION_LOCK 	(SW_MAX-1)

extern void shuttle_3g_gps_poweron(void);
extern void shuttle_3g_gps_poweroff(void);
extern int shuttle_3g_gps_init(void);
extern void shuttle_3g_gps_deinit(void);

extern void shuttle_wlan_bt_poweron(void);
extern void shuttle_wlan_bt_poweroff(void);
extern int shuttle_wlan_bt_init(void);
extern void shuttle_wlan_bt_deinit(void);

extern void shuttle_wifi_set_cd(int val);

extern void shuttle_init_emc(void);
extern void shuttle_pinmux_init(void);
extern void shuttle_clks_init(void);

extern int shuttle_usb_register_devices(void);
extern int shuttle_audio_register_devices(void);
extern int shuttle_gpu_register_devices(void);
extern int shuttle_uart_register_devices(void);
extern int shuttle_spi_register_devices(void);
extern int shuttle_aes_register_devices(void);
extern int shuttle_wdt_register_devices(void);
extern int shuttle_i2c_register_devices(void);
extern int shuttle_power_register_devices(void);
extern int shuttle_keyboard_register_devices(void);
extern int shuttle_touch_register_devices(void);
extern int shuttle_sdhci_register_devices(void);
extern int shuttle_sensors_register_devices(void);
extern int shuttle_wlan_pm_register_devices(void);
extern int shuttle_gps_pm_register_devices(void);
extern int shuttle_gsm_pm_register_devices(void);
extern int shuttle_bt_pm_register_devices(void);
extern int shuttle_nand_register_devices(void);
extern int shuttle_camera_pm_register_devices(void);

#endif

