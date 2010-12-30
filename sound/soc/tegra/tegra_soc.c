/*
 * tegra_soc.c  --  SoC audio for tegra
 *
 * (c) 2010 Nvidia Graphics Pvt. Ltd.
 *  http://www.nvidia.com
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include "tegra_soc.h"
#include <mach/audio.h>
#include "../codecs/wm8903.h"

static struct platform_device *tegra_snd_device;
static struct tegra_audio_data *audio_data;
static int tegra_jack_func;
static int tegra_spk_func;

#define TEGRA_HP	0
#define TEGRA_MIC	1
#define TEGRA_LINE	2
#define TEGRA_HEADSET	3
#define TEGRA_HP_OFF	4
#define TEGRA_SPK_ON	0
#define TEGRA_SPK_OFF	1

/* codec register values */
#define B07_INEMUTE		7
#define B06_VOL_M3DB		6
#define B00_IN_VOL		0
#define B00_INR_ENA		0
#define B01_INL_ENA		1
#define R06_MICBIAS_CTRL_0	6
#define B07_MICDET_HYST_ENA	7
#define B04_MICDET_THR		4
#define B02_MICSHORT_THR	2
#define B01_MICDET_ENA		1
#define B00_MICBIAS_ENA		0
#define B15_DRC_ENA		15
#define B03_DACL_ENA		3
#define B02_DACR_ENA		2
#define B01_ADCL_ENA		1
#define B00_ADCR_ENA		0
#define B06_IN_CM_ENA		6
#define B04_IP_SEL_N		4
#define B02_IP_SEL_P		2
#define B00_MODE 		0
#define B06_AIF_ADCL		7
#define B06_AIF_ADCR		6
#define B05_ADC_HPF_CUT		5
#define B04_ADC_HPF_ENA		4
#define B01_ADCL_DATINV		1
#define B00_ADCR_DATINV		0
#define R20_SIDETONE_CTRL	32
#define R29_DRC_1		41
#define SET_REG_VAL(r,m,l,v) (((r)&(~((m)<<(l))))|(((v)&(m))<<(l)))


static void tegra_ext_control(struct snd_soc_codec *codec)
{
	/* set up jack connection */
	switch (tegra_jack_func) {
	case TEGRA_HP:
		/* set = unmute headphone */
		snd_soc_dapm_enable_pin(codec, "Mic Jack");
		snd_soc_dapm_disable_pin(codec, "Line Jack");
		snd_soc_dapm_enable_pin(codec, "Headphone Jack");
		snd_soc_dapm_disable_pin(codec, "Headset Jack");
		break;
	case TEGRA_MIC:
		/* reset = mute headphone */
		snd_soc_dapm_enable_pin(codec, "Mic Jack");
		snd_soc_dapm_disable_pin(codec, "Line Jack");
		snd_soc_dapm_disable_pin(codec, "Headphone Jack");
		snd_soc_dapm_disable_pin(codec, "Headset Jack");
		break;
	case TEGRA_LINE:
		snd_soc_dapm_disable_pin(codec, "Mic Jack");
		snd_soc_dapm_enable_pin(codec, "Line Jack");
		snd_soc_dapm_disable_pin(codec, "Headphone Jack");
		snd_soc_dapm_disable_pin(codec, "Headset Jack");
		break;
	case TEGRA_HEADSET:
		snd_soc_dapm_enable_pin(codec, "Mic Jack");
		snd_soc_dapm_disable_pin(codec, "Line Jack");
		snd_soc_dapm_disable_pin(codec, "Headphone Jack");
		snd_soc_dapm_enable_pin(codec, "Headset Jack");
		break;
	}

	if (tegra_spk_func == TEGRA_SPK_ON) {
		snd_soc_dapm_enable_pin(codec, "Ext Spk");
	} else {
		snd_soc_dapm_disable_pin(codec, "Ext Spk");
	}
	/* signal a DAPM event */
	snd_soc_dapm_sync(codec);
}

static int tegra_hifi_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int err;
	struct snd_soc_codec *codec = codec_dai->codec;
	int CtrlReg = 0;
	int VolumeCtrlReg = 0;
	int SidetoneCtrlReg = 0;
	int SideToneAtenuation = 0;

	err = snd_soc_dai_set_fmt(codec_dai,
					SND_SOC_DAIFMT_I2S | \
					SND_SOC_DAIFMT_NB_NF | \
					SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		printk(KERN_ERR "codec_dai fmt not set \n");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai,
					SND_SOC_DAIFMT_I2S | \
					SND_SOC_DAIFMT_NB_NF | \
					SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		printk(KERN_ERR "cpu_dai fmt not set \n");
		return err;
	}
	err = snd_soc_dai_set_sysclk(codec_dai, 0, I2S_CLK, SND_SOC_CLOCK_IN);

	if (err<0) {
		printk(KERN_ERR "codec_dai clock not set\n");
		return err;
	}
	err = snd_soc_dai_set_sysclk(cpu_dai, 0, I2S_CLK, SND_SOC_CLOCK_IN);

	if (err<0) {
		printk(KERN_ERR "cpu_dai clock not set\n");
		return err;
	}

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_write(codec, WM8903_ANALOGUE_LEFT_INPUT_0, 0X7);
		snd_soc_write(codec, WM8903_ANALOGUE_RIGHT_INPUT_0, 0X7);
		// Mic Bias enable
		CtrlReg = (0x1<<B00_MICBIAS_ENA) | (0x1<<B01_MICDET_ENA);
		snd_soc_write(codec, WM8903_MIC_BIAS_CONTROL_0, CtrlReg);
		// Enable DRC
		CtrlReg = snd_soc_read(codec, WM8903_DRC_0);
		CtrlReg |= (1<<B15_DRC_ENA);
		snd_soc_write(codec, WM8903_DRC_0, CtrlReg);
		// Single Ended Mic
		CtrlReg = (0x0<<B06_IN_CM_ENA) |
			(0x0<<B00_MODE) | (0x0<<B04_IP_SEL_N)
					| (0x1<<B02_IP_SEL_P);
		VolumeCtrlReg = (0x5 << B00_IN_VOL);
		// Mic Setting
		snd_soc_write(codec, WM8903_ANALOGUE_LEFT_INPUT_1, CtrlReg);
		snd_soc_write(codec, WM8903_ANALOGUE_RIGHT_INPUT_1, CtrlReg);
		// voulme for single ended mic
		snd_soc_write(codec, WM8903_ANALOGUE_LEFT_INPUT_0,
				VolumeCtrlReg);
		snd_soc_write(codec, WM8903_ANALOGUE_RIGHT_INPUT_0,
				VolumeCtrlReg);
		// replicate mic setting on both channels
		CtrlReg = snd_soc_read(codec, WM8903_AUDIO_INTERFACE_0);
		CtrlReg  = SET_REG_VAL(CtrlReg, 0x1, B06_AIF_ADCR, 0x0);
		CtrlReg  = SET_REG_VAL(CtrlReg, 0x1, B06_AIF_ADCL, 0x0);
		snd_soc_write(codec, WM8903_AUDIO_INTERFACE_0, CtrlReg);
		// Enable analog inputs
		CtrlReg = (0x1<<B01_INL_ENA) | (0x1<<B00_INR_ENA);
		snd_soc_write(codec, WM8903_POWER_MANAGEMENT_0, CtrlReg);
		// ADC Settings
		CtrlReg = snd_soc_read(codec, WM8903_ADC_DIGITAL_0);
		CtrlReg |= (0x1<<B04_ADC_HPF_ENA);
		snd_soc_write(codec, WM8903_ADC_DIGITAL_0, CtrlReg);
		SidetoneCtrlReg = 0;
		snd_soc_write(codec, R20_SIDETONE_CTRL, SidetoneCtrlReg);
		// Enable ADC
		CtrlReg = snd_soc_read(codec, WM8903_POWER_MANAGEMENT_6);
		CtrlReg |= (0x1<<B00_ADCR_ENA)|(0x1<<B01_ADCL_ENA);
		snd_soc_write(codec, WM8903_POWER_MANAGEMENT_6, CtrlReg);
		// Enable Sidetone
		SidetoneCtrlReg = (0x1<<2) | (0x2<<0);
		SideToneAtenuation = 12 ; // sidetone 0 db
		SidetoneCtrlReg |= (SideToneAtenuation<<8)
				| (SideToneAtenuation<<4);
		snd_soc_write(codec, R20_SIDETONE_CTRL, SidetoneCtrlReg);
		CtrlReg = snd_soc_read(codec, R29_DRC_1);
		CtrlReg |= 0x3; //mic volume 18 db
		snd_soc_write(codec, R29_DRC_1, CtrlReg);
	}

	return 0;
}

static struct snd_soc_ops tegra_hifi_ops = {
	.hw_params = tegra_hifi_hw_params,
};


static int tegra_get_jack(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = tegra_jack_func;
	return 0;
}

static int tegra_set_jack(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (tegra_jack_func == ucontrol->value.integer.value[0])
		return 0;

	tegra_jack_func = ucontrol->value.integer.value[0];
	tegra_ext_control(codec);
	return 1;
}

static int tegra_get_spk(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = tegra_spk_func;
	return 0;
}

static int tegra_set_spk(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);


	if (tegra_spk_func == ucontrol->value.integer.value[0])
		return 0;

	tegra_spk_func = ucontrol->value.integer.value[0];
	tegra_ext_control(codec);
	return 1;
}

/*tegra machine dapm widgets */
static const struct snd_soc_dapm_widget wm8903_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_LINE("Line Jack", NULL),
	SND_SOC_DAPM_HP("Headset Jack", NULL),
};

/* Tegra machine audio map (connections to the codec pins) */
static const struct snd_soc_dapm_route audio_map[] = {

	/* headset Jack  - in = micin, out = LHPOUT*/
	{"Headset Jack", NULL, "HPOUTL"},

	/* headphone connected to LHPOUT1, RHPOUT1 */
	{"Headphone Jack", NULL, "HPOUTR"}, {"Headphone Jack", NULL, "HPOUTL"},

	/* speaker connected to LOUT, ROUT */
	{"Ext Spk", NULL, "LINEOUTR"}, {"Ext Spk", NULL, "LINEOUTL"},

	/* mic is connected to MICIN (via right channel of headphone jack) */
	{"IN1L", NULL, "Mic Jack"},

	/* Same as the above but no mic bias for line signals */
	{"IN2L", NULL, "Line Jack"},
};

static const char *jack_function[] = {"Headphone", "Mic", "Line", "Headset",
					"Off"
					};
static const char *spk_function[] = {"On", "Off"};
static const struct soc_enum tegra_enum[] = {
	SOC_ENUM_SINGLE_EXT(5, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new wm8903_tegra_controls[] = {
	SOC_ENUM_EXT("Jack Function", tegra_enum[0], tegra_get_jack,
			tegra_set_jack),
	SOC_ENUM_EXT("Speaker Function", tegra_enum[1], tegra_get_spk,
			tegra_set_spk),
};

static void tegra_audio_route(int device_new, int is_call_mode_new)
{
	int play_device_new = device_new & TEGRA_AUDIO_DEVICE_OUT_ALL;
	int capture_device_new = device_new & TEGRA_AUDIO_DEVICE_IN_ALL;
	int is_bt_sco_mode =
		(play_device_new & TEGRA_AUDIO_DEVICE_OUT_BT_SCO) ||
		(capture_device_new & TEGRA_AUDIO_DEVICE_OUT_BT_SCO);
	int was_bt_sco_mode =
		(audio_data->play_device & TEGRA_AUDIO_DEVICE_OUT_BT_SCO) ||
		(audio_data->capture_device & TEGRA_AUDIO_DEVICE_OUT_BT_SCO);

	if (play_device_new != audio_data->play_device) {
		if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_HEADPHONE) {
			tegra_jack_func = TEGRA_HP;
		}
		else if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_HEADSET) {
			tegra_jack_func = TEGRA_HEADSET;
		}
		else if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_LINE) {
			tegra_jack_func = TEGRA_LINE;
		}

		if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_SPEAKER) {
			tegra_spk_func = TEGRA_SPK_ON;
		}
		else if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_EAR_SPEAKER) {
			tegra_spk_func = TEGRA_SPK_ON;
		}
		else {
			tegra_spk_func = TEGRA_SPK_OFF;
		}
		tegra_ext_control(audio_data->codec);
		audio_data->play_device = play_device_new;
	}

	if (capture_device_new != audio_data->capture_device) {
		if (capture_device_new & (TEGRA_AUDIO_DEVICE_IN_BUILTIN_MIC |
					TEGRA_AUDIO_DEVICE_IN_MIC |
					TEGRA_AUDIO_DEVICE_IN_BACK_MIC)) {
			if ((tegra_jack_func != TEGRA_HP) &&
				(tegra_jack_func != TEGRA_HEADSET)) {
				tegra_jack_func = TEGRA_MIC;
			}
		}
		else if (capture_device_new & TEGRA_AUDIO_DEVICE_IN_HEADSET) {
			tegra_jack_func = TEGRA_HEADSET;
		}
		else if (capture_device_new & TEGRA_AUDIO_DEVICE_IN_LINE) {
			tegra_jack_func = TEGRA_LINE;
		}
		tegra_ext_control(audio_data->codec);
		audio_data->capture_device = capture_device_new;
	}

	if ((is_call_mode_new != audio_data->is_call_mode) ||
		(is_bt_sco_mode != was_bt_sco_mode)) {
		if (is_call_mode_new && is_bt_sco_mode) {
			tegra_das_set_connection
				(tegra_das_port_con_id_voicecall_with_bt);
		}
		else if (is_call_mode_new && !is_bt_sco_mode) {
			tegra_das_set_connection
				(tegra_das_port_con_id_voicecall_no_bt);
		}
		else if (!is_call_mode_new && is_bt_sco_mode) {
			tegra_das_set_connection
				(tegra_das_port_con_id_bt_codec);
		}
		else {
			tegra_das_set_connection
				(tegra_das_port_con_id_hifi);
		}
		audio_data->is_call_mode = is_call_mode_new;
	}
}

static int tegra_play_route_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TEGRA_AUDIO_DEVICE_NONE;
	uinfo->value.integer.max = TEGRA_AUDIO_DEVICE_MAX;
	return 0;
}

static int tegra_play_route_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = TEGRA_AUDIO_DEVICE_NONE;
	if (audio_data) {
		ucontrol->value.integer.value[0] = audio_data->play_device;
		return 0;
	}
	return -EINVAL;
}

static int tegra_play_route_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	if (audio_data) {
		int play_device_new = ucontrol->value.integer.value[0] &
				TEGRA_AUDIO_DEVICE_OUT_ALL;

		if (audio_data->play_device != play_device_new) {
			tegra_audio_route(
				play_device_new | audio_data->capture_device,
				audio_data->is_call_mode);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

struct snd_kcontrol_new tegra_play_route_control =
{
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Pcm Playback Route",
	.private_value = 0xffff,
	.info = tegra_play_route_info,
	.get = tegra_play_route_get,
	.put = tegra_play_route_put
};

static int tegra_capture_route_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TEGRA_AUDIO_DEVICE_NONE;
	uinfo->value.integer.max = TEGRA_AUDIO_DEVICE_MAX;
	return 0;
}

static int tegra_capture_route_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = TEGRA_AUDIO_DEVICE_NONE;
	if (audio_data) {
		ucontrol->value.integer.value[0] = audio_data->capture_device;
		return 0;
	}
	return -EINVAL;
}

static int tegra_capture_route_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	if (audio_data) {
		int capture_device_new = ucontrol->value.integer.value[0] &
				TEGRA_AUDIO_DEVICE_IN_ALL;

		if (audio_data->capture_device != capture_device_new) {
			tegra_audio_route(
				audio_data->play_device | capture_device_new,
				audio_data->is_call_mode);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

struct snd_kcontrol_new tegra_capture_route_control =
{
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Pcm Capture Route",
	.private_value = 0xffff,
	.info = tegra_capture_route_info,
	.get = tegra_capture_route_get,
	.put = tegra_capture_route_put
};

static int tegra_call_mode_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int tegra_call_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = TEGRA_AUDIO_DEVICE_NONE;
	if (audio_data) {
		ucontrol->value.integer.value[0] = audio_data->is_call_mode;
		return 0;
	}
	return -EINVAL;
}

static int tegra_call_mode_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	if (audio_data) {
		int is_call_mode_new = ucontrol->value.integer.value[0];

		if (audio_data->is_call_mode != is_call_mode_new) {
			tegra_audio_route(
				audio_data->play_device |
				audio_data->capture_device,
				is_call_mode_new);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

struct snd_kcontrol_new tegra_call_mode_control =
{
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Call Mode Switch",
	.private_value = 0xffff,
	.info = tegra_call_mode_info,
	.get = tegra_call_mode_get,
	.put = tegra_call_mode_put
};

static int tegra_codec_init(struct snd_soc_codec *codec)
{
	int err;

	/* Add tegra specific controls */
	err = snd_soc_add_controls(codec, wm8903_tegra_controls,
					ARRAY_SIZE(wm8903_tegra_controls));
	if (err < 0)
		return err;

	/* Add tegra specific widgets */
	snd_soc_dapm_new_controls(codec, wm8903_dapm_widgets,
					ARRAY_SIZE(wm8903_dapm_widgets));

	/* Set up tegra specific audio path audio_map */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	audio_data->codec = codec;
	/* Add play route control */
	err = snd_ctl_add(codec->card,
			snd_ctl_new1(&tegra_play_route_control, NULL));
	if (err < 0)
		return err;

	/* Add capture route control */
	err = snd_ctl_add(codec->card,
			snd_ctl_new1(&tegra_capture_route_control, NULL));
	if (err < 0)
		return err;

	/* Add call mode switch control */
	err = snd_ctl_add(codec->card,
			snd_ctl_new1(&tegra_call_mode_control, NULL));
	if (err < 0)
		return err;

	/* Default to HP output */
	tegra_jack_func = TEGRA_HP;
	tegra_spk_func = TEGRA_SPK_ON;
	tegra_ext_control(codec);

	snd_soc_dapm_sync(codec);

	return 0;
}

extern struct snd_soc_dai tegra_i2s_dai;
extern struct snd_soc_platform tegra_soc_platform;

static struct snd_soc_dai_link tegra_soc_dai = {
	.name = "WM8903",
	.stream_name = "WM8903 HiFi",
	.cpu_dai = &tegra_i2s_dai,
	.codec_dai = &wm8903_dai,
	.init = tegra_codec_init,
	.ops = &tegra_hifi_ops,
};

static struct snd_soc_card tegra_snd_soc = {
	.name = "tegra",
	.platform = &tegra_soc_platform,
	.dai_link = &tegra_soc_dai,
	.num_links = 1,
};

struct tegra_setup_data {
	int i2c_bus;
	unsigned short i2c_address;
};

static struct snd_soc_device tegra_snd_devdata = {
	.card = &tegra_snd_soc,
	.codec_dev = &soc_codec_dev_wm8903,
};

static int __init tegra_init(void)
{
	int ret = 0;
	struct tegra_setup_data tegra_setup;

	tegra_snd_device = platform_device_alloc("soc-audio", -1);
	if (!tegra_snd_device) {
		pr_err("failed to allocate soc-audio \n");
		ret = -ENOMEM;
		goto fail;
	}

	audio_data = kzalloc(sizeof(*audio_data), GFP_KERNEL);
	if (!audio_data) {
		pr_err("failed to allocate tegra_audio_data \n");
		ret = -ENOMEM;
		goto fail;
	}

	memset(&tegra_setup,0,sizeof(struct tegra_setup_data));
	platform_set_drvdata(tegra_snd_device, &tegra_snd_devdata);
	tegra_snd_devdata.dev = &tegra_snd_device->dev;
	ret = platform_device_add(tegra_snd_device);
	if (ret) {
		pr_err("audio device could not be added \n");
		goto fail;
	}

	return 0;

fail:
	if (audio_data) {
		kfree(audio_data);
		audio_data = 0;
	}

	if (tegra_snd_device) {
		platform_device_put(tegra_snd_device);
		tegra_snd_device = 0;
	}

	return ret;
}

static void __exit tegra_exit(void)
{
	if (audio_data) {
		kfree(audio_data);
		audio_data = 0;
	}
	platform_device_unregister(tegra_snd_device);
}

module_init(tegra_init);
module_exit(tegra_exit);

/* Module information */
MODULE_DESCRIPTION("Tegra ALSA SoC");
MODULE_LICENSE("GPL");
