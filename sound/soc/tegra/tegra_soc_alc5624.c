/*
 * tegra_soc_alc5624.c  --  SoC audio for tegra (glue logic)
 *
 * (c) 2010-2011 Nvidia Graphics Pvt. Ltd.
 *  http://www.nvidia.com
 * (C) 2011 Eduardo José Tagle <ejtagle@tutopia.com>
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
 
/* #define DEBUG */
 
#include "tegra_soc.h"
#include <linux/gpio.h>
#include <sound/soc-dapm.h>
#include <linux/regulator/consumer.h>
#include <sound/jack.h>
#include <mach/shuttle_audio.h>

#define DRV_NAME "tegra-snd-shuttle"

/* exported by the ALC5624 codec */
extern struct snd_soc_dai alc5624_dai;
extern struct snd_soc_codec_device soc_codec_dev_alc5624;

/* possible audio sources */
enum shuttle_audio_device {
	SHUTTLE_AUDIO_DEVICE_NONE	   = 0,		/* no device */
	SHUTTLE_AUDIO_DEVICE_BLUETOOTH = 0x01,	/* bluetooth */
	SHUTTLE_AUDIO_DEVICE_VOICE	   = 0x02,	/* cell phone audio */
	SHUTTLE_AUDIO_DEVICE_HIFI	   = 0x04,	/* normal speaker/headphone audio */
	
	SHUTTLE_AUDIO_DEVICE_MAX	   = 0x07	/* all audio sources */
};

struct shuttle_audio_priv {
	struct clk* 		  dap_mclk;			/* DAP clock */
	
	struct snd_soc_jack   tegra_jack;		/* jack detection */
	bool				  init_done;		/* init done */
	int 				  play_device;		/* Playback devices bitmask */
	int 				  capture_device;	/* Capture devices bitmask */
	bool				  is_call_mode;		/* if we are in a call mode */
	int					  gpio_hp_det;		/* GPIO used to detect Headphone plugged in */
};

/* mclk required for each sampling frequency */
static const struct {
	unsigned int mclk;
	unsigned short srate;
} clocktab[] = {
	/* 8k */
	{ 8192000,  8000},
	{12288000,  8000},
	{24576000,  8000},

	/* 11.025k */
	{11289600, 11025},
	{16934400, 11025},
	{22579200, 11025},

	/* 16k */
	{12288000, 16000},
	{16384000, 16000},
	{24576000, 16000},

	/* 22.05k */	
	{11289600, 22050},
	{16934400, 22050},
	{22579200, 22050},

	/* 32k */
	{12288000, 32000},
	{16384000, 32000},
	{24576000, 32000},

	/* 44.1k */
	{11289600, 44100},
	{22579200, 44100},

	/* 48k */
	{12288000, 48000},
	{24576000, 48000},
};


/* --------- Digital audio interfase ------------ */

static int tegra_hifi_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai 	= rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai 	= rtd->dai->cpu_dai;
	struct shuttle_audio_priv* ctx 	= rtd->socdev->codec_data;
	
	
	int dai_flag = 0, sys_clk;
	int err;
	int i;	

	/* Get the requested sampling rate */
	unsigned int srate = params_rate(params);
	
	/* I2S <-> DAC <-> DAS <-> DAP <-> CODEC
	   -If DAP is master, codec will be slave */
	int codec_is_master = !tegra_das_is_port_master(tegra_audio_codec_type_hifi);
	
	/* Get DAS dataformat - DAP is connecting to codec */
	enum dac_dap_data_format data_fmt = tegra_das_get_codec_data_fmt(tegra_audio_codec_type_hifi);

	/* We are supporting DSP and I2s format for now */
	if (data_fmt & dac_dap_data_format_i2s)
		dai_flag |= SND_SOC_DAIFMT_I2S;
	else
		dai_flag |= SND_SOC_DAIFMT_DSP_A;
	
	if (codec_is_master)
		dai_flag |= SND_SOC_DAIFMT_CBM_CFM; /* codec is master */
	else
		dai_flag |= SND_SOC_DAIFMT_CBS_CFS;
	
	
	pr_debug("%s(): format: 0x%08x\n", __FUNCTION__,params_format(params));

	/* Set the CPU dai format. This will also set the clock rate in master mode */
	err = snd_soc_dai_set_fmt(cpu_dai, dai_flag);
	if (err < 0) {
		pr_err("cpu_dai fmt not set \n");
		return err;
	}

	err = snd_soc_dai_set_fmt(codec_dai, dai_flag);
	if (err < 0) {
		pr_err("codec_dai fmt not set \n");
		return err;
	}
	
	/* Get system clock */
	sys_clk = clk_get_rate(ctx->dap_mclk);

	/* Set CPU sysclock as the same - in Tegra, seems to be a NOP */
	err = snd_soc_dai_set_sysclk(cpu_dai, 0, sys_clk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		pr_err("cpu_dai clock not set\n");
		return err;
	}
	
	if (codec_is_master) {
		pr_debug("%s(): codec in master mode\n",__FUNCTION__);
		
		/* If using port as slave (=codec as master), then we can use the
		   codec PLL to get the other sampling rates */
		
		/* Try each one until success */
		for (i = 0; i < ARRAY_SIZE(clocktab); i++) {
		
			if (clocktab[i].srate != srate) 
				continue;
				
			if (snd_soc_dai_set_pll(codec_dai, 0, 0, sys_clk, clocktab[i].mclk) >= 0) {
				/* Codec PLL is synthetizing this new clock */
				sys_clk = clocktab[i].mclk;
				break;
			}
		}
		
		if (i >= ARRAY_SIZE(clocktab)) {
			pr_err("%s(): unable to set required MCLK for SYSCLK of %d, sampling rate: %d\n",__FUNCTION__,sys_clk,srate);
			return -EINVAL;
		}
		
	} else {
		pr_debug("%s(): codec in slave mode\n",__FUNCTION__);

		/* Disable codec PLL */
		err = snd_soc_dai_set_pll(codec_dai, 0, 0, sys_clk, sys_clk);
		if (err < 0) {
			pr_err("%s(): unable to disable codec PLL\n",__FUNCTION__);
			return err;
		}
		
		/* Check this sampling rate can be achieved with this sysclk */
		for (i = 0; i < ARRAY_SIZE(clocktab); i++) {
		
			if (clocktab[i].srate != srate) 
				continue;
				
			if (sys_clk == clocktab[i].mclk)
				break;
		}
		
		if (i >= ARRAY_SIZE(clocktab)) {
			pr_err("%s(): unable to get required %d hz sampling rate of %d hz SYSCLK\n",__FUNCTION__,srate,sys_clk);
			return -EINVAL;
		}
	}

	/* Set CODEC sysclk */
	err = snd_soc_dai_set_sysclk(codec_dai, 0, sys_clk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		pr_err("codec_dai clock not set\n");
		return err;
	}
	
	return 0;
}

static int tegra_voice_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai 	= rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai 	= rtd->dai->cpu_dai;
	struct shuttle_audio_priv* ctx 	= rtd->socdev->codec_data;
	
	int dai_flag = 0, sys_clk;
	int err;

	/* Get DAS dataformat and master flag */
	int codec_is_master = !tegra_das_is_port_master(tegra_audio_codec_type_bluetooth);
	enum dac_dap_data_format data_fmt = tegra_das_get_codec_data_fmt(tegra_audio_codec_type_bluetooth);

	/* We are supporting DSP and I2s format for now */
	if (data_fmt & dac_dap_data_format_dsp)
		dai_flag |= SND_SOC_DAIFMT_DSP_A;
	else
		dai_flag |= SND_SOC_DAIFMT_I2S;

	if (codec_is_master)
		dai_flag |= SND_SOC_DAIFMT_CBM_CFM; /* codec is master */
	else
		dai_flag |= SND_SOC_DAIFMT_CBS_CFS;


	pr_debug("%s(): format: 0x%08x\n", __FUNCTION__,params_format(params));

	/* Set the CPU dai format. This will also set the clock rate in master mode */
	err = snd_soc_dai_set_fmt(cpu_dai, dai_flag);
	if (err < 0) {
		pr_err("cpu_dai fmt not set \n");
		return err;
	}

	/* Bluetooth Codec is always slave here */
	err = snd_soc_dai_set_fmt(codec_dai, dai_flag);
	if (err < 0) {
		pr_err("codec_dai fmt not set \n");
		return err;
	}
	
	/* Get system clock */
	sys_clk = clk_get_rate(ctx->dap_mclk);

	/* Set CPU sysclock as the same - in Tegra, seems to be a NOP */
	err = snd_soc_dai_set_sysclk(cpu_dai, 0, sys_clk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		pr_err("cpu_dai clock not set\n");
		return err;
	}
	
	/* Set CODEC sysclk */
	err = snd_soc_dai_set_sysclk(codec_dai, 0, sys_clk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		pr_err("cpu_dai clock not set\n");
		return err;
	}
	
	return 0;
}

static int tegra_spdif_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	pr_debug("%s(): format: 0x%08x\n", __FUNCTION__,params_format(params));
	return 0;
}

static int tegra_codec_startup(struct snd_pcm_substream *substream)
{
	tegra_das_power_mode(true);

	return 0;
}

static void tegra_codec_shutdown(struct snd_pcm_substream *substream)
{
	tegra_das_power_mode(false);
}

static int tegra_soc_suspend_pre(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int tegra_soc_suspend_post(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct shuttle_audio_priv* ctx = socdev->codec_data;

	clk_disable(ctx->dap_mclk);

	return 0;
}

static int tegra_soc_resume_pre(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct shuttle_audio_priv* ctx = socdev->codec_data;

	clk_enable(ctx->dap_mclk);

	return 0;
}

static int tegra_soc_resume_post(struct platform_device *pdev)
{
	return 0;
}

static struct snd_soc_ops tegra_hifi_ops = {
	.hw_params = tegra_hifi_hw_params,
	.startup = tegra_codec_startup,
	.shutdown = tegra_codec_shutdown,
};

static struct snd_soc_ops tegra_voice_ops = {
	.hw_params = tegra_voice_hw_params,
	.startup = tegra_codec_startup,
	.shutdown = tegra_codec_shutdown,
};

static struct snd_soc_ops tegra_spdif_ops = {
	.hw_params = tegra_spdif_hw_params,
};

/* ------- Tegra audio routing using DAS -------- */

static void tegra_audio_route(struct shuttle_audio_priv* ctx,
			      int play_device, int capture_device)
{
	
	int is_bt_sco_mode = (play_device    & SHUTTLE_AUDIO_DEVICE_BLUETOOTH) ||
						 (capture_device & SHUTTLE_AUDIO_DEVICE_BLUETOOTH);
	int is_call_mode   = (play_device    & SHUTTLE_AUDIO_DEVICE_VOICE) ||
						 (capture_device & SHUTTLE_AUDIO_DEVICE_VOICE);

	pr_debug("%s(): is_bt_sco_mode: %d, is_call_mode: %d\n", __FUNCTION__, is_bt_sco_mode, is_call_mode);
	
	if (is_call_mode && is_bt_sco_mode) {
		tegra_das_set_connection(tegra_das_port_con_id_voicecall_with_bt);
	}
	else if (is_call_mode && !is_bt_sco_mode) {
		tegra_das_set_connection(tegra_das_port_con_id_voicecall_no_bt);
	}
	else if (!is_call_mode && is_bt_sco_mode) {
		tegra_das_set_connection(tegra_das_port_con_id_bt_codec);
	}
	else {
		tegra_das_set_connection(tegra_das_port_con_id_hifi);
	}
	ctx->play_device = play_device;
	ctx->capture_device = capture_device;
}

static int tegra_play_route_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = SHUTTLE_AUDIO_DEVICE_NONE;
	uinfo->value.integer.max = SHUTTLE_AUDIO_DEVICE_MAX;
	return 0;
}

static int tegra_play_route_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct shuttle_audio_priv* ctx = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = SHUTTLE_AUDIO_DEVICE_NONE;
	if (ctx) {
		ucontrol->value.integer.value[0] = ctx->play_device;
		return 0;
	}
	return -EINVAL;
}

static int tegra_play_route_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct shuttle_audio_priv* ctx = snd_kcontrol_chip(kcontrol);

	if (ctx) {
		int play_device_new = ucontrol->value.integer.value[0];

		if (ctx->play_device != play_device_new) {
			tegra_audio_route(ctx, play_device_new, ctx->capture_device);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

struct snd_kcontrol_new tegra_play_route_control = {
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
	uinfo->value.integer.min = SHUTTLE_AUDIO_DEVICE_NONE;
	uinfo->value.integer.max = SHUTTLE_AUDIO_DEVICE_MAX;
	return 0;
}

static int tegra_capture_route_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct shuttle_audio_priv* ctx = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = TEGRA_AUDIO_DEVICE_NONE;
	if (ctx) {
		ucontrol->value.integer.value[0] = ctx->capture_device;
		return 0;
	}
	return -EINVAL;
}

static int tegra_capture_route_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct shuttle_audio_priv* ctx = snd_kcontrol_chip(kcontrol);

	if (ctx) {
		int capture_device_new = ucontrol->value.integer.value[0];

		if (ctx->capture_device != capture_device_new) {
			tegra_audio_route(ctx,
				ctx->play_device , capture_device_new);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

static struct snd_kcontrol_new tegra_capture_route_control = {
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
	struct shuttle_audio_priv* ctx = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = 0;
	if (ctx) {
		int is_call_mode   = (ctx->play_device    & SHUTTLE_AUDIO_DEVICE_VOICE) ||
							 (ctx->capture_device & SHUTTLE_AUDIO_DEVICE_VOICE);
	
		ucontrol->value.integer.value[0] = is_call_mode ? 1 : 0;
		return 0;
	}
	return -EINVAL;
}

static int tegra_call_mode_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct shuttle_audio_priv* ctx = snd_kcontrol_chip(kcontrol);

	if (ctx) {
		int is_call_mode   = (ctx->play_device    & SHUTTLE_AUDIO_DEVICE_VOICE) ||
							 (ctx->capture_device & SHUTTLE_AUDIO_DEVICE_VOICE);
	
		int is_call_mode_new = ucontrol->value.integer.value[0];

		if (is_call_mode != is_call_mode_new) {
			if (is_call_mode_new) {
				ctx->play_device 	|= SHUTTLE_AUDIO_DEVICE_VOICE;
				ctx->capture_device |= SHUTTLE_AUDIO_DEVICE_VOICE;
				ctx->play_device 	&= ~SHUTTLE_AUDIO_DEVICE_HIFI;
				ctx->capture_device &= ~SHUTTLE_AUDIO_DEVICE_HIFI;
			} else {
				ctx->play_device 	&= ~SHUTTLE_AUDIO_DEVICE_VOICE;
				ctx->capture_device &= ~SHUTTLE_AUDIO_DEVICE_VOICE;
				ctx->play_device 	|= SHUTTLE_AUDIO_DEVICE_HIFI;
				ctx->capture_device |= SHUTTLE_AUDIO_DEVICE_HIFI;
			}
			tegra_audio_route(ctx,
				ctx->play_device,
				ctx->capture_device);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

static struct snd_kcontrol_new tegra_call_mode_control = {
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Call Mode Switch",
	.private_value = 0xffff,
	.info = tegra_call_mode_info,
	.get = tegra_call_mode_get,
	.put = tegra_call_mode_put
};

static int tegra_das_controls_init(struct snd_soc_codec *codec)
{
	struct shuttle_audio_priv* ctx = codec->socdev->codec_data;
	int err;

	/* Add play route control */
	err = snd_ctl_add(codec->card,
		snd_ctl_new1(&tegra_play_route_control, ctx));
	if (err < 0)
		return err;

	/* Add capture route control */
	err = snd_ctl_add(codec->card,
		snd_ctl_new1(&tegra_capture_route_control, ctx));
	if (err < 0)
		return err;

	/* Add call mode switch control */
	err = snd_ctl_add(codec->card,
		snd_ctl_new1(&tegra_call_mode_control, ctx));
	if (err < 0)
		return err;

	return 0;
}

#ifndef SHUTTLE_MANUAL_CONTROL_OF_OUTPUTDEVICE

/* ------- Headphone jack autodetection  -------- */
static struct snd_soc_jack_pin tegra_jack_pins[] = {
	/* Disable speaker when headphone is plugged in */
	{
		.pin = "Internal Speaker",
		.mask = SND_JACK_HEADPHONE,
		.invert = 1, /* Enable pin when status is not reported */
	},
	/* Enable headphone when status is reported */
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
		.invert = 0, /* Enable pin when status is reported */
	},
};

static struct snd_soc_jack_gpio tegra_jack_gpios[] = {
	{
		.name = "headphone detect",
		.report = SND_JACK_HEADPHONE,
		.debounce_time = 150,
		/*.gpio is filled in initialization from platform data */
	}
};

#endif

/*tegra machine dapm widgets */
static const struct snd_soc_dapm_widget tegra_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Internal Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),
};

/* Tegra machine audio map (connections to the codec pins) */
static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "HPR"},
	{"Headphone Jack", NULL, "HPL"},
	{"Internal Speaker", NULL, "SPKL"},
	{"Internal Speaker", NULL, "SPKLN"},
	{"Internal Speaker", NULL, "SPKR"},
	{"Internal Speaker", NULL, "SPKRN"},
	{"Mic Bias1", NULL, "Internal Mic"},
	{"MIC1", NULL, "Mic Bias1"},
};

#ifdef SHUTTLE_MANUAL_CONTROL_OF_OUTPUTDEVICE
static const struct snd_kcontrol_new tegra_controls[] = {
	SOC_DAPM_PIN_SWITCH("Internal Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};
#endif

/* Called from the i2s driver when resuming play. No need to do anything in this case */
void tegra_jack_resume(void)
{
}

static int tegra_codec_init(struct snd_soc_codec *codec)
{
	struct shuttle_audio_priv* ctx = codec->socdev->codec_data;
	int ret = 0;

	if (!ctx->init_done) {
		
		/* Get and enable the DAP clock */
		ctx->dap_mclk = tegra_das_get_dap_mclk();
		if (!ctx->dap_mclk) {
			pr_err("Failed to get dap mclk \n");
			return -ENODEV;
		}
		clk_enable(ctx->dap_mclk);

		/* Add the controls used to route audio to bluetooth/voice */
		tegra_das_controls_init(codec);
		
		/* Store the GPIO used to detect headphone */
		tegra_jack_gpios[0].gpio = ctx->gpio_hp_det;

		/* Add tegra specific widgets */
		snd_soc_dapm_new_controls(codec, tegra_dapm_widgets,
					ARRAY_SIZE(tegra_dapm_widgets));

#ifdef SHUTTLE_MANUAL_CONTROL_OF_OUTPUTDEVICE
		/* Add specific shuttle controls */
		ret = snd_soc_add_controls(codec, tegra_controls,
					ARRAY_SIZE(tegra_controls));
		if (ret < 0) {
			pr_err("Failed to register controls\n");
			return ret;
		}
#endif
					
		/* Set up tegra specific audio path audio_map */
		snd_soc_dapm_add_routes(codec, audio_map,
					ARRAY_SIZE(audio_map));

		/* make sure to register the dapm widgets */
		snd_soc_dapm_new_widgets(codec);
					
		/* Set endpoints to not connected */
		snd_soc_dapm_nc_pin(codec, "LINEL");
		snd_soc_dapm_nc_pin(codec, "LINER");
		snd_soc_dapm_nc_pin(codec, "PHONEIN");
		snd_soc_dapm_nc_pin(codec, "MIC2");
		snd_soc_dapm_nc_pin(codec, "MONO");

		/* Set endpoints to default off mode */
		snd_soc_dapm_enable_pin(codec, "Internal Speaker");
		snd_soc_dapm_enable_pin(codec, "Internal Mic");
		snd_soc_dapm_disable_pin(codec, "Headphone Jack");
	
		ret = snd_soc_dapm_sync(codec);
		if (ret) {
			pr_err("Failed to sync\n");
			return ret;
		}

#ifndef SHUTTLE_MANUAL_CONTROL_OF_OUTPUTDEVICE
		/* Headphone jack detection */		
		ret = snd_soc_jack_new(codec->socdev->card, "Headphone Jack", SND_JACK_HEADPHONE,
				 &ctx->tegra_jack);
		if (ret)
			return ret;
			 
		ret = snd_soc_jack_add_pins(&ctx->tegra_jack,
			      ARRAY_SIZE(tegra_jack_pins),
			      tegra_jack_pins);
		if (ret)
			return ret;
				  
		ret = snd_soc_jack_add_gpios(&ctx->tegra_jack,
			       ARRAY_SIZE(tegra_jack_gpios),
			       tegra_jack_gpios);
		if (ret)
			return ret;
#endif

		ctx->init_done = 1;
	}

	return ret;
}

extern struct snd_soc_dai tegra_generic_codec_dai[];
extern struct snd_soc_dai tegra_i2s_dai[];
extern struct snd_soc_dai tegra_spdif_dai;

static struct snd_soc_dai_link tegra_soc_dai[] = {
	{
		.name = "ALC5624",
		.stream_name = "ALC5624 HiFi",
		.cpu_dai = &tegra_i2s_dai[0],
		.codec_dai = &alc5624_dai,
		.init = tegra_codec_init,
		.ops = &tegra_hifi_ops,
	},
	{
		.name = "Tegra-generic",
		.stream_name = "Tegra Generic Voice",
		.cpu_dai = &tegra_i2s_dai[1],
		.codec_dai = &tegra_generic_codec_dai[0],
		.init = tegra_codec_init,
		.ops = &tegra_voice_ops,
	},
	{
		.name = "Tegra-spdif",
		.stream_name = "Tegra Spdif",
		.cpu_dai = &tegra_spdif_dai,
		.codec_dai = &tegra_generic_codec_dai[1],
		.init = tegra_codec_init,
		.ops = &tegra_spdif_ops,
	},
};

/* The Tegra card definition */
extern struct snd_soc_platform tegra_soc_platform;
static struct snd_soc_card tegra_snd_soc = {
	.name = "tegra",
	.platform 	= &tegra_soc_platform,
	.dai_link 	= tegra_soc_dai,
	.num_links 	= ARRAY_SIZE(tegra_soc_dai),
	.suspend_pre = tegra_soc_suspend_pre,
	.suspend_post = tegra_soc_suspend_post,
	.resume_pre = tegra_soc_resume_pre,
	.resume_post = tegra_soc_resume_post,
};

/* A sound device is composed of a card (in this case, Tegra, and
   a codec, in this case ALC5624 */
static struct snd_soc_device tegra_snd_devdata = {
	.card = &tegra_snd_soc,
	.codec_dev = &soc_codec_dev_alc5624,
};

/* initialization */

static struct platform_device *tegra_snd_device;


static __devinit int tegra_snd_shuttle_probe(struct platform_device *pdev)
{
	struct shuttle_audio_platform_data* pdata;
	struct shuttle_audio_priv *ctx;
	int ret = 0;
	
	/* Get platform data */
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data supplied\n");
		return -EINVAL;
	}

	/* Allocate private context */
	ctx = kzalloc(sizeof(struct shuttle_audio_priv), GFP_KERNEL);
	if (!ctx) {
		dev_err(&pdev->dev, "Can't allocate tegra_shuttle\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, ctx); /* Store it as platform data */
	tegra_snd_devdata.codec_data = ctx;
	
	/* Fill in the GPIO used to detect the headphone */
	ctx->gpio_hp_det = pdata->gpio_hp_det;
	
	/* Reserve space for the soc-audio device */
	tegra_snd_device = platform_device_alloc("soc-audio", -1);
	if (!tegra_snd_device) {
		dev_err(&pdev->dev, "failed to allocate soc-audio \n");
		kfree(ctx);
		return -ENOMEM;
	}

	/* Set soc-audio platform data to our descriptor */
	platform_set_drvdata(tegra_snd_device, &tegra_snd_devdata);
	tegra_snd_devdata.dev = &tegra_snd_device->dev;

	/* Add the device */
	ret = platform_device_add(tegra_snd_device);
	if (ret) {
		dev_err(&pdev->dev, "audio device could not be added \n");
		kfree(ctx);
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

static int __devexit tegra_snd_shuttle_remove(struct platform_device *pdev)
{
	struct shuttle_audio_priv *ctx = platform_get_drvdata(pdev);
	
	platform_device_unregister(tegra_snd_device);
	
	kfree(ctx);
	platform_set_drvdata(pdev,NULL);
	
	return 0;
}

static struct platform_driver tegra_snd_shuttle_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = tegra_snd_shuttle_probe,
	.remove = __devexit_p(tegra_snd_shuttle_remove),
};

static int __init snd_tegra_shuttle_init(void)
{
	return platform_driver_register(&tegra_snd_shuttle_driver);
}
module_init(snd_tegra_shuttle_init);

static void __exit snd_tegra_shuttle_exit(void)
{
	platform_driver_unregister(&tegra_snd_shuttle_driver);
}
module_exit(snd_tegra_shuttle_exit);

MODULE_AUTHOR("Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_DESCRIPTION("Shuttle machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
