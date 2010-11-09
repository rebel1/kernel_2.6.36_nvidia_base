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

#include "../codecs/wm8903.h"
#include "tegra_soc.h"
#include <mach/audio.h>

static struct platform_device *tegra_snd_device;
static int tegra_jack_func;
static int tegra_spk_func;

#define TEGRA_HP	0
#define TEGRA_MIC	1
#define TEGRA_LINE	2
#define TEGRA_HEADSET	3
#define TEGRA_HP_OFF	4
#define TEGRA_SPK_ON	0
#define TEGRA_SPK_OFF	1

static void tegra_ext_control(struct snd_soc_codec *codec)
{
	/* set up jack connection */
	switch (tegra_jack_func) {
	case TEGRA_HP:
		/* set = unmute headphone */
		snd_soc_dapm_disable_pin(codec, "Mic Jack");
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
		printk(KERN_ERR "codec_dai clock not set\n");
		return err;
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
	int ret;
	struct tegra_setup_data tegra_setup;

	tegra_snd_device = platform_device_alloc("soc-audio", -1);
	if (!tegra_snd_device)
		return -ENOMEM;

	memset(&tegra_setup,0,sizeof(struct tegra_setup_data));
	platform_set_drvdata(tegra_snd_device, &tegra_snd_devdata);
	tegra_snd_devdata.dev = &tegra_snd_device->dev;
	ret = platform_device_add(tegra_snd_device);
	if (ret) {
		printk(KERN_ERR "audio device could not be added \n");
		platform_device_put(tegra_snd_device);
		return ret;
	}

	return ret;
}

static void __exit tegra_exit(void)
{
	platform_device_unregister(tegra_snd_device);
}

module_init(tegra_init);
module_exit(tegra_exit);

/* Module information */
MODULE_DESCRIPTION("Tegra ALSA SoC");
MODULE_LICENSE("GPL");
