/*
 * tegra_soc_wm8753.c
 *
 * Author: Sachin Nikam
 *         snikam@nvidia.com
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "tegra_soc.h"
#include "../codecs/wm8753.h"

#define WM8753_PWR1_VMIDSEL_EN	0x80 /* Vmid divider enable and select */
#define WM8753_PWR1_VREF_EN		0x40 /* VREF Enable */
#define WM8753_PWR1_DACL_EN		0x08 /* DAC Left Enable */
#define WM8753_PWR1_DACR_EN		0x04 /* VREF Enable */

#define WM8753_PWR3_LOU1_EN		0x100 /* LOUT1 Enable */
#define WM8753_PWR3_ROU1_EN		0x70 /* ROUT1 Enable */

#define WM8753_PWR4_RIGHTMIX_EN	0x02 /* Right mixer enable */
#define WM8753_PWR4_LEFTMIX_EN	0x01 /* Left mixer enable */

#define WM8753_LOUTM1_LD2LO		0x100 /* Left DAC to Left Mixer */
#define WM8753_LOUTM1_LM2LO		0x80
#define WM8753_LOUTM1_LM2LOVOL	0x70 /* LM Signal to Left Mixer Volume */

#define WM8753_ROUTM1_RD2RO		0x100 /* Right DAC to Right Mixer */
#define WM8753_ROUTM1_RM2RO		0x80
#define WM8753_ROUTM1_RM2ROVOL	0x70 /* RM Signal to Right Mixer Volume */

static struct platform_device *tegra_snd_device;

extern struct snd_soc_dai tegra_i2s_dai;
extern struct snd_soc_platform tegra_soc_platform;

static int tegra_hifi_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret = 0, value = 0;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		printk(KERN_ERR "codec_dai fmt not set\n");
		return ret;
	}

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		printk(KERN_ERR "cpu_dai fmt not set\n");
		return ret;
	}

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, I2S_CLK,
		SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "codec_dai clock not set\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, I2S_CLK,
		SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "cpu_dai clock not set\n");
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		value = snd_soc_read(codec_dai->codec, WM8753_PWR1);
		value |= WM8753_PWR1_VMIDSEL_EN | WM8753_PWR1_VREF_EN |
					WM8753_PWR1_DACL_EN |WM8753_PWR1_DACR_EN;
		snd_soc_write(codec_dai->codec, WM8753_PWR1, value);

		value = snd_soc_read(codec_dai->codec, WM8753_PWR3);
		value |= WM8753_PWR3_LOU1_EN | WM8753_PWR3_ROU1_EN;
		snd_soc_write(codec_dai->codec, WM8753_PWR3, value);

		value = snd_soc_read(codec_dai->codec, WM8753_PWR4);
		value |= WM8753_PWR4_RIGHTMIX_EN | WM8753_PWR4_LEFTMIX_EN;
		snd_soc_write(codec_dai->codec, WM8753_PWR4, value);

		value = snd_soc_read(codec_dai->codec, WM8753_LOUTM1);
		value |= WM8753_LOUTM1_LD2LO | WM8753_LOUTM1_LM2LO |
				WM8753_LOUTM1_LM2LOVOL;
		snd_soc_write(codec_dai->codec, WM8753_LOUTM1, value);

		value = snd_soc_read(codec_dai->codec, WM8753_ROUTM1);
		value |= WM8753_ROUTM1_RD2RO | WM8753_ROUTM1_RM2RO |
				WM8753_ROUTM1_RM2ROVOL;
		snd_soc_write(codec_dai->codec, WM8753_ROUTM1, value);
	}

	return 0;
}


static int tegra_hifi_hw_free(struct snd_pcm_substream *substream)
{
	return 0;
}

static struct snd_soc_ops tegra_hifi_ops = {
	.hw_params = tegra_hifi_hw_params,
	.hw_free = tegra_hifi_hw_free,
};

static int tegra_voice_hw_params(
	struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	return 0;
}

static int tegra_voice_hw_free(struct snd_pcm_substream *substream)
{
	return 0;
}

static struct snd_soc_ops tegra_voice_ops = {
	.hw_params = tegra_voice_hw_params,
	.hw_free = tegra_voice_hw_free,
};

static int tegra_codec_init(struct snd_soc_codec *codec)
{
	return tegra_controls_init(codec);
}

static struct snd_soc_dai bt_dai = {
	.name = "Bluetooth",
	.id = 0,
	.playback = {
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
};

static struct snd_soc_dai_link tegra_soc_dai[] = {
	{ /* Hifi Playback - for similatious use with voice below */
		.name = "WM8753",
		.stream_name = "WM8753 HiFi",
		.cpu_dai = &tegra_i2s_dai,
		.codec_dai = &wm8753_dai[WM8753_DAI_HIFI],
		.init = tegra_codec_init,
		.ops = &tegra_hifi_ops,
	},
	{ /* Voice via BT */
		.name = "Bluetooth",
		.stream_name = "Voice",
		.cpu_dai = &bt_dai,
		.codec_dai = &wm8753_dai[WM8753_DAI_VOICE],
		.ops = &tegra_voice_ops,
	},
};

static struct snd_soc_card tegra_snd_soc = {
	.name = "tegra",
	.platform = &tegra_soc_platform,
	.dai_link = tegra_soc_dai,
	.num_links = 1,
};

static struct snd_soc_device tegra_snd_devdata = {
	.card = &tegra_snd_soc,
	.codec_dev = &soc_codec_dev_wm8753,
};

static int __init tegra_init(void)
{
	int ret = 0;

	tegra_snd_device = platform_device_alloc("soc-audio", -1);
	if (!tegra_snd_device) {
		pr_err("failed to allocate soc-audio \n");
		return ENOMEM;
	}

	platform_set_drvdata(tegra_snd_device, &tegra_snd_devdata);
	tegra_snd_devdata.dev = &tegra_snd_device->dev;
	ret = platform_device_add(tegra_snd_device);
	if (ret) {
		pr_err("audio device could not be added \n");
		goto fail;
	}

	return 0;

fail:
	if (tegra_snd_device) {
		platform_device_put(tegra_snd_device);
		tegra_snd_device = 0;
	}

	return ret;
}

static void __exit tegra_exit(void)
{
	tegra_controls_exit();
	platform_device_unregister(tegra_snd_device);
}

module_init(tegra_init);
module_exit(tegra_exit);

/* Module information */
MODULE_DESCRIPTION("Tegra ALSA SoC");
MODULE_LICENSE("GPL");
