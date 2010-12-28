/*
 * tegra_i2s.c  --  ALSA Soc Audio Layer
 *
 * (c) 2010 Nvidia Graphics Pvt. Ltd.
 *  http://www.nvidia.com
 *
 * (c) 2006 Wolfson Microelectronics PLC.
 * Graeme Gregory graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 * (c) 2004-2005 Simtec Electronics
 *    http://armlinux.simtec.co.uk/
 *    Ben Dooks <ben@simtec.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include "tegra_soc.h"

static void *das_base = IO_ADDRESS(TEGRA_APB_MISC_BASE);
struct snd_soc_dai tegra_i2s_dai;

static inline unsigned long das_readl(unsigned long offset)
{
	return readl(das_base + offset);
}

static inline void das_writel(unsigned long value, unsigned long offset)
{
	writel(value, das_base + offset);
}

static int tegra_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct tegra_runtime_data *prtd = runtime->private_data;
	int ret=0;
	int val;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = I2S_BIT_SIZE_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = I2S_BIT_SIZE_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val = I2S_BIT_SIZE_32;
		break;
	default:
		ret =-EINVAL;
		goto err;
	}

	i2s_set_bit_size(I2S_IFC, val);

	switch (params_rate(params)) {
	case 8000:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
		val = params_rate(params);
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	i2s_set_channel_bit_count(I2S_IFC, val, clk_get_rate(prtd->i2s_clk));
	tegra_i2s_dai.private_data = (void *)prtd;
	return 0;

err:
	return ret;
}


static int tegra_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
					unsigned int fmt)
{
	int val1;
	int val2;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		val1 = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		val1= 0;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
		/* Tegra does not support different combinations of
		 * master and slave for FSYNC and BCLK */
	default:
		return -EINVAL;
	}

	i2s_set_master(I2S_IFC, val1);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		val1 = I2S_BIT_FORMAT_DSP;
		val2 = 0;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		val1 = I2S_BIT_FORMAT_DSP;
		val2 = 1;
		break;
	case SND_SOC_DAIFMT_I2S:
		val1 = I2S_BIT_FORMAT_I2S;
		val2 = 0;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val1 = I2S_BIT_FORMAT_RJM;
		val2 = 0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val1 = I2S_BIT_FORMAT_LJM;
		val2 = 0;
		break;
	default:
		return -EINVAL;
	}

	i2s_set_bit_format(I2S_IFC,val1);
	i2s_set_left_right_control_polarity(I2S_IFC,val2);

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			/* aif1 |= WM8903_AIF_BCLK_INV; */
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			/* aif1 |= WM8903_AIF_BCLK_INV |
			 * WM8903_AIF_LRCLK_INV; */
			break;
		case SND_SOC_DAIFMT_IB_NF:
			/* aif1 |= WM8903_AIF_BCLK_INV; */
			break;
		case SND_SOC_DAIFMT_NB_IF:
			/* aif1 |= WM8903_AIF_LRCLK_INV; */
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra_i2s_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
					int clk_id, unsigned int freq, int dir)
{
	return 0;
}

static int tegra_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_PM
int tegra_i2s_suspend(struct snd_soc_dai *i2s_dai)
{
	struct tegra_runtime_data *prtd = (struct snd_pcm_runtime *)(tegra_i2s_dai.private_data);
	i2s_get_all_regs(I2S_IFC, &prtd->i2s_regs);
	return 0;
}

int tegra_i2s_resume(struct snd_soc_dai *i2s_dai)
{
	struct tegra_runtime_data *prtd = (struct snd_pcm_runtime *)(tegra_i2s_dai.private_data);
	i2s_set_all_regs(I2S_IFC, &prtd->i2s_regs);
	return 0;
}

#else
#define tegra_i2s_suspend	NULL
#define tegra_i2s_resume	NULL
#endif

static int tegra_i2s_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	return 0;
}

static void tegra_i2s_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
}

static int tegra_i2s_probe(struct platform_device *pdev,
				struct snd_soc_dai *dai)
{
	/* DAC1 -> DAP1, DAC1 master, DAP2 bypass */
	das_writel(0, APB_MISC_DAS_DAP_CTRL_SEL_0);
	das_writel(0, APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0);
	i2s_enable_fifos(I2S_IFC, 0);
	i2s_set_left_right_control_polarity(I2S_IFC, 0); /* default */
	i2s_set_master(I2S_IFC, 1); /* set as master */
	i2s_set_fifo_mode(I2S_IFC, FIFO1, 1); /* FIFO1 is TX */
	i2s_set_fifo_mode(I2S_IFC, FIFO2, 0); /* FIFO2 is RX */
	i2s_set_bit_format(I2S_IFC, I2S_BIT_FORMAT_I2S);
	i2s_set_bit_size(I2S_IFC, I2S_BIT_SIZE_16);
	i2s_set_fifo_format(I2S_IFC, I2S_FIFO_PACKED);
	return 0;
}

static int tegra_i2s_driver_probe(struct platform_device *dev)
{
	int ret;

	tegra_i2s_dai.dev = &dev->dev;
	tegra_i2s_dai.private_data = NULL;
	ret = snd_soc_register_dai(&tegra_i2s_dai);
	return ret;
}


static int __devexit tegra_i2s_driver_remove(struct platform_device *dev)
{
	snd_soc_unregister_dai(&tegra_i2s_dai);
	return 0;
}

static struct snd_soc_dai_ops tegra_i2s_dai_ops = {
	.startup	= tegra_i2s_startup,
	.shutdown	= tegra_i2s_shutdown,
	.trigger	= tegra_i2s_trigger,
	.hw_params  = tegra_i2s_hw_params,
	.set_fmt	= tegra_i2s_set_dai_fmt,
	.set_sysclk	= tegra_i2s_set_dai_sysclk,
};

struct snd_soc_dai tegra_i2s_dai = {
	.name = "tegra-i2s",
	.id = 0,
	.probe = tegra_i2s_probe,
	.suspend = tegra_i2s_suspend,
	.resume = tegra_i2s_resume,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = TEGRA_SAMPLE_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = TEGRA_SAMPLE_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tegra_i2s_dai_ops,
};
EXPORT_SYMBOL_GPL(tegra_i2s_dai);

static struct platform_driver tegra_i2s_driver = {
	.probe = tegra_i2s_driver_probe,
	.remove = __devexit_p(tegra_i2s_driver_remove),
	.driver = {
		.name = "i2s",
		.owner = THIS_MODULE,
	},
};

static int __init tegra_i2s_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&tegra_i2s_driver);
	return ret;
}
module_init(tegra_i2s_init);

static void __exit tegra_i2s_exit(void)
{
	platform_driver_unregister(&tegra_i2s_driver);
}
module_exit(tegra_i2s_exit);

/* Module information */
MODULE_DESCRIPTION("Tegra I2S SoC interface");
MODULE_LICENSE("GPL");
