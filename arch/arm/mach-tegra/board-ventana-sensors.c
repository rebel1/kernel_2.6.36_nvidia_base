/*
 * arch/arm/mach-tegra/board-ventana-sensors.c
 *
 * Copyright (c) 2011, NVIDIA, All Rights Reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/i2c.h>
#include <linux/akm8975.h>
#include <linux/mpu.h>
#include <linux/i2c/pca954x.h>
#include <linux/i2c/pca953x.h>
#include <linux/nct1008.h>

#include <mach/gpio.h>

#include <media/ov5650.h>
#include <media/ov2710.h>
#include <generated/mach-types.h>

#include "gpio-names.h"
#include "board.h"
#include "board-ventana.h"

#define ISL29018_IRQ_GPIO	TEGRA_GPIO_PZ2
#define AKM8975_IRQ_GPIO	TEGRA_GPIO_PN5
#define CAMERA_POWER_GPIO	TEGRA_GPIO_PV4
#define CAMERA_CSI_MUX_SEL_GPIO	TEGRA_GPIO_PBB4
#define AC_PRESENT_GPIO		TEGRA_GPIO_PV3
#define NCT1008_THERM2_GPIO	TEGRA_GPIO_PN6

extern void tegra_throttling_enable(bool enable);

static int ventana_camera_init(void)
{
	tegra_gpio_enable(CAMERA_POWER_GPIO);
	gpio_request(CAMERA_POWER_GPIO, "camera_power_en");
	gpio_direction_output(CAMERA_POWER_GPIO, 1);
	gpio_export(CAMERA_POWER_GPIO, false);

	tegra_gpio_enable(CAMERA_CSI_MUX_SEL_GPIO);
	gpio_request(CAMERA_CSI_MUX_SEL_GPIO, "camera_csi_sel");
	gpio_direction_output(CAMERA_CSI_MUX_SEL_GPIO, 0);
	gpio_export(CAMERA_CSI_MUX_SEL_GPIO, false);

	return 0;
}

static int ventana_ov5650_power_on(void)
{
	gpio_direction_output(CAMERA_CSI_MUX_SEL_GPIO, 0);
	return 0;
}

static int ventana_ov5650_power_off(void)
{
	return 0;
}

struct ov5650_platform_data ventana_ov5650_data = {
	.power_on = ventana_ov5650_power_on,
	.power_off = ventana_ov5650_power_off,
};

static int ventana_ov2710_power_on(void)
{
	gpio_direction_output(CAMERA_CSI_MUX_SEL_GPIO, 1);
	return 0;
}

static int ventana_ov2710_power_off(void)
{
	return 0;
}

struct ov2710_platform_data ventana_ov2710_data = {
	.power_on = ventana_ov2710_power_on,
	.power_off = ventana_ov2710_power_off,
};

static void ventana_isl29018_init(void)
{
	tegra_gpio_enable(ISL29018_IRQ_GPIO);
	gpio_request(ISL29018_IRQ_GPIO, "isl29018");
	gpio_direction_input(ISL29018_IRQ_GPIO);
}

#ifdef CONFIG_SENSORS_AK8975
static void ventana_akm8975_init(void)
{
	tegra_gpio_enable(AKM8975_IRQ_GPIO);
	gpio_request(AKM8975_IRQ_GPIO, "akm8975");
	gpio_direction_input(AKM8975_IRQ_GPIO);
}
#endif

static void ventana_bq20z75_init(void)
{
	tegra_gpio_enable(AC_PRESENT_GPIO);
	gpio_request(AC_PRESENT_GPIO, "ac_present");
	gpio_direction_input(AC_PRESENT_GPIO);
}

static void ventana_nct1008_init(void)
{
	tegra_gpio_enable(NCT1008_THERM2_GPIO);
	gpio_request(NCT1008_THERM2_GPIO, "temp_alert");
	gpio_direction_input(NCT1008_THERM2_GPIO);
}

static struct nct1008_platform_data ventana_nct1008_pdata = {
	.supported_hwrev = true,
	.ext_range = false,
	.conv_rate = 0x08,
	.offset = 0,
	.hysteresis = 0,
	.shutdown_ext_limit = 115,
	.shutdown_local_limit = 120,
	.throttling_ext_limit = 90,
	.alarm_fn = tegra_throttling_enable,
};

static const struct i2c_board_info ventana_i2c0_board_info[] = {
	{
		I2C_BOARD_INFO("isl29018", 0x44),
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PZ2),
	},
};

static const struct i2c_board_info ventana_i2c2_board_info[] = {
	{
		I2C_BOARD_INFO("bq20z75-battery", 0x0B),
		.irq = TEGRA_GPIO_TO_IRQ(AC_PRESENT_GPIO),
	},
};

static struct pca953x_platform_data ventana_tca6416_data = {
	.gpio_base      = TEGRA_NR_GPIOS + 4, /* 4 gpios are already requested by tps6586x */
};

static struct pca954x_platform_mode ventana_pca9546_modes[] = {
	{ .adap_id = 6, }, /* REAR CAM1 */
	{ .adap_id = 7, }, /* REAR CAM2 */
	{ .adap_id = 8, }, /* FRONT CAM3 */
};

static struct pca954x_platform_data ventana_pca9546_data = {
	.modes	  = ventana_pca9546_modes,
	.num_modes      = ARRAY_SIZE(ventana_pca9546_modes),
};

static const struct i2c_board_info ventana_i2c3_board_info_tca6416[] = {
	{
		I2C_BOARD_INFO("tca6416", 0x20),
		.platform_data = &ventana_tca6416_data,
	},
};

static const struct i2c_board_info ventana_i2c3_board_info_pca9546[] = {
	{
		I2C_BOARD_INFO("pca9546", 0x70),
		.platform_data = &ventana_pca9546_data,
	},
};

static struct i2c_board_info ventana_i2c4_board_info[] = {
	{
		I2C_BOARD_INFO("nct1008", 0x4C),
		.irq = TEGRA_GPIO_TO_IRQ(NCT1008_THERM2_GPIO),
		.platform_data = &ventana_nct1008_pdata,
	},

#ifdef CONFIG_SENSORS_AK8975
	{
		I2C_BOARD_INFO("akm8975", 0x0C),
		.irq = TEGRA_GPIO_TO_IRQ(AKM8975_IRQ_GPIO),
	},
#endif
};

static struct i2c_board_info ventana_i2c7_board_info[] = {
	{
		I2C_BOARD_INFO("ov5650", 0x36),
		.platform_data = &ventana_ov5650_data,
	},
};

static struct i2c_board_info ventana_i2c8_board_info[] = {
	{
		I2C_BOARD_INFO("ov2710", 0x36),
		.platform_data = &ventana_ov2710_data,
	},
};

#ifdef CONFIG_SENSORS_MPU3050
#define SENSOR_MPU_NAME "mpu3050"
static struct mpu3050_platform_data mpu3050_data = {
	.int_config  = 0x10,
	.orientation = { 0, -1, 0, -1, 0, 0, 0, 0, -1 },  /* Orientation matrix for MPU on ventana */
	.level_shifter = 0,
	.accel = {
#ifdef CONFIG_SENSORS_KXTF9_MPU
	.get_slave_descr = get_accel_slave_descr,
#else
	.get_slave_descr = NULL,
#endif
	.adapt_num   = 0,
	.bus         = EXT_SLAVE_BUS_SECONDARY,
	.address     = 0x0F,
	.orientation = { 0, -1, 0, -1, 0, 0, 0, 0, -1 },  /* Orientation matrix for Kionix on ventana */
	},

	.compass = {
#ifdef CONFIG_SENSORS_AK8975_MPU
	.get_slave_descr = get_compass_slave_descr,
#else
	.get_slave_descr = NULL,
#endif
	.adapt_num   = 4,            /* bus number 4 on ventana */
	.bus         = EXT_SLAVE_BUS_PRIMARY,
	.address     = 0x0C,
	.orientation = { 1, 0, 0, 0, -1, 0, 0, 0, -1 },  /* Orientation matrix for AKM on ventana */
	},
};

static struct i2c_board_info __initdata mpu3050_i2c0_boardinfo[] = {
	{
		I2C_BOARD_INFO(SENSOR_MPU_NAME, 0x68),
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PZ4),
		.platform_data = &mpu3050_data,
	},
};

static void ventana_mpuirq_init(void)
{
	pr_info("*** MPU START *** ventana_mpuirq_init...\n");
	tegra_gpio_enable(TEGRA_GPIO_PZ4);
	gpio_request(TEGRA_GPIO_PZ4, SENSOR_MPU_NAME);
	gpio_direction_input(TEGRA_GPIO_PZ4);
	pr_info("*** MPU END *** ventana_mpuirq_init...\n");
}
#endif

int __init ventana_sensors_init(void)
{
	struct board_info BoardInfo;

	ventana_isl29018_init();
#ifdef CONFIG_SENSORS_AK8975
	ventana_akm8975_init();
#endif
#ifdef CONFIG_SENSORS_MPU3050
	ventana_mpuirq_init();
#endif
	ventana_camera_init();
	ventana_nct1008_init();

	i2c_register_board_info(0, ventana_i2c0_board_info,
		ARRAY_SIZE(ventana_i2c0_board_info));

	tegra_get_board_info(&BoardInfo);

	/*
	 * battery driver is supported on FAB.D boards and above only,
	 * since they have the necessary hardware rework
	 */
	if (BoardInfo.sku > 0) {
		ventana_bq20z75_init();
		i2c_register_board_info(2, ventana_i2c2_board_info,
			ARRAY_SIZE(ventana_i2c2_board_info));
	}

	i2c_register_board_info(4, ventana_i2c4_board_info,
		ARRAY_SIZE(ventana_i2c4_board_info));

	i2c_register_board_info(7, ventana_i2c7_board_info,
		ARRAY_SIZE(ventana_i2c7_board_info));

	i2c_register_board_info(8, ventana_i2c8_board_info,
		ARRAY_SIZE(ventana_i2c8_board_info));


#ifdef CONFIG_SENSORS_MPU3050
	i2c_register_board_info(0, mpu3050_i2c0_boardinfo,
		ARRAY_SIZE(mpu3050_i2c0_boardinfo));
#endif

	return 0;
}

#ifdef CONFIG_VIDEO_OV5650

struct ov5650_gpios {
	const char *name;
	int gpio;
	int enabled;
};

#define OV5650_GPIO(_name, _gpio, _enabled)		\
	{						\
		.name = _name,				\
		.gpio = _gpio,				\
		.enabled = _enabled,			\
	}

static struct ov5650_gpios ov5650_gpio_keys[] = {
	[0] = OV5650_GPIO("en_avdd_csi", AVDD_DSI_CSI_ENB_GPIO, 1),
	[1] = OV5650_GPIO("cam_i2c_mux_rst_lo", CAM_I2C_MUX_RST_GPIO, 1),

	[2] = OV5650_GPIO("cam2_pwdn", CAM2_PWR_DN_GPIO, 0),
	[3] = OV5650_GPIO("cam2_rst_lo", CAM2_RST_L_GPIO, 1),
	[4] = OV5650_GPIO("cam2_af_pwdn_lo", CAM2_AF_PWR_DN_L_GPIO, 1),
	[5] = OV5650_GPIO("cam2_ldo_shdn_lo", CAM2_LDO_SHUTDN_L_GPIO, 1),

	[6] = OV5650_GPIO("cam1_pwdn", CAM1_PWR_DN_GPIO, 0),
	[7] = OV5650_GPIO("cam1_rst_lo", CAM1_RST_L_GPIO, 1),
	[8] = OV5650_GPIO("cam1_af_pwdn_lo", CAM1_AF_PWR_DN_L_GPIO, 0),
	[9] = OV5650_GPIO("cam1_ldo_shdn_lo", CAM1_LDO_SHUTDN_L_GPIO, 1),

	[10] = OV5650_GPIO("cam3_pwdn", CAM3_PWR_DN_GPIO, 0),
	[11] = OV5650_GPIO("cam3_rst_lo", CAM3_RST_L_GPIO, 1),
	[12] = OV5650_GPIO("cam3_af_pwdn_lo", CAM3_AF_PWR_DN_L_GPIO, 0),
	[13] = OV5650_GPIO("cam3_ldo_shdn_lo", CAM3_LDO_SHUTDN_L_GPIO, 1),

};

int __init ventana_ov5650_late_init(void)
{
	int ret;
	int i;

	if (!machine_is_ventana())
		return 0;

	i2c_new_device(i2c_get_adapter(3), ventana_i2c3_board_info_tca6416);

	for (i = 0; i < ARRAY_SIZE(ov5650_gpio_keys); i++) {
		ret = gpio_request(ov5650_gpio_keys[i].gpio,
			ov5650_gpio_keys[i].name);
		if (ret < 0) {
			pr_err("%s: gpio_request failed for gpio #%d\n",
				__func__, i);
			goto fail;
		}
		gpio_direction_output(ov5650_gpio_keys[i].gpio,
			ov5650_gpio_keys[i].enabled);
		gpio_export(ov5650_gpio_keys[i].gpio, false);
	}

	i2c_new_device(i2c_get_adapter(3), ventana_i2c3_board_info_pca9546);

	return 0;

fail:
	while (i--)
		gpio_free(ov5650_gpio_keys[i].gpio);
	return ret;
}

late_initcall(ventana_ov5650_late_init);

#endif /* CONFIG_VIDEO_OV5650 */
