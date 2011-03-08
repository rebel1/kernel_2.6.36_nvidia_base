/*
 * tegra_soc_controls.c -- alsa controls for tegra SoC
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

#include <linux/gpio.h>
#include <sound/soc-dapm.h>
#include <mach/audio.h>

#include "tegra_soc.h"

#define TEGRA_HP	0
#define TEGRA_MIC	1
#define TEGRA_LINE	2
#define TEGRA_HEADSET	3
#define TEGRA_HP_OFF	4

#define TEGRA_LINEOUT_ON	0
#define TEGRA_LINEOUT_OFF	1

#define TEGRA_INT_SPK_ON	0
#define TEGRA_INT_SPK_OFF	1

extern struct wired_jack_conf tegra_wired_jack_conf;

static struct tegra_audio_data *audio_data;

static int tegra_jack_func;
static int tegra_lineout_func;
static int tegra_spk_func;

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


	if (tegra_lineout_func == TEGRA_LINEOUT_ON) {
		snd_soc_dapm_enable_pin(codec, "Lineout");
	} else {
		snd_soc_dapm_disable_pin(codec, "Lineout");
	}

	if (tegra_spk_func == TEGRA_INT_SPK_ON) {
		snd_soc_dapm_enable_pin(codec, "Int Spk");
	} else {
		snd_soc_dapm_disable_pin(codec, "Int Spk");
	}

	/* signal a DAPM event */
	snd_soc_dapm_sync(codec);
}

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

static int tegra_get_lineout(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = tegra_lineout_func;
	return 0;
}

static int tegra_set_lineout(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (tegra_lineout_func == ucontrol->value.integer.value[0])
		return 0;

	tegra_lineout_func = ucontrol->value.integer.value[0];
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

static int tegra_dapm_event_int_spk(struct snd_soc_dapm_widget* w,
				    struct snd_kcontrol* k, int event)
{
	if (tegra_wired_jack_conf.en_spkr != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_spkr,
					SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static int tegra_dapm_event_int_mic(struct snd_soc_dapm_widget* w,
				    struct snd_kcontrol* k, int event)
{
	if (tegra_wired_jack_conf.en_mic_int != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_mic_int,
					SND_SOC_DAPM_EVENT_ON(event));

	if (tegra_wired_jack_conf.en_mic_ext != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_mic_ext,
					!(SND_SOC_DAPM_EVENT_ON(event)));

	return 0;
}

static int tegra_dapm_event_ext_mic(struct snd_soc_dapm_widget* w,
				    struct snd_kcontrol* k, int event)
{
	if (tegra_wired_jack_conf.en_mic_ext != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_mic_ext,
					SND_SOC_DAPM_EVENT_ON(event));

	if (tegra_wired_jack_conf.en_mic_int != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_mic_int,
					!(SND_SOC_DAPM_EVENT_ON(event)));

	return 0;
}

/*tegra machine dapm widgets */
static const struct snd_soc_dapm_widget tegra_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", tegra_dapm_event_ext_mic),
	SND_SOC_DAPM_MIC("Int Mic", tegra_dapm_event_int_mic),
	SND_SOC_DAPM_SPK("Lineout", NULL),
	SND_SOC_DAPM_SPK("Int Spk", tegra_dapm_event_int_spk),
	SND_SOC_DAPM_LINE("Line Jack", NULL),
	SND_SOC_DAPM_HP("Headset Jack", NULL),
};

/* Tegra machine audio map (connections to the codec pins) */
static const struct snd_soc_dapm_route audio_map[] = {

	/* headset Jack  - in = micin, out = LHPOUT*/
	{"Headset Jack", NULL, "HPOUTL"},

	/* headphone connected to LHPOUT1, RHPOUT1 */
	{"Headphone Jack", NULL, "HPOUTR"}, {"Headphone Jack", NULL, "HPOUTL"},

	/* build-in speaker connected to LON/P RON/P */
	{"Int Spk", NULL, "RON"},
	{"Int Spk", NULL, "ROP"},
	{"Int Spk", NULL, "LON"},
	{"Int Spk", NULL, "LOP"},

	/* lineout connected to LINEOUTR and LINEOUTL */
	{"Lineout", NULL, "LINEOUTR"},
	{"Lineout", NULL, "LINEOUTL"},

	/* external mic is stero */
	{"IN1L", NULL, "Mic Jack"},
	{"IN1R", NULL, "Mic Jack"},

	/* internal mic is mono */
	{"IN1R", NULL, "Int Mic"},

	{"IN3L", NULL, "Line Jack"},
	{"IN3R", NULL, "Line Jack"},
};

static const char *jack_function[] = {"Headphone", "Mic", "Line", "Headset",
					"Off"
					};
static const char *lineout_function[] = {"On", "Off"};
static const char *spk_function[] = {"On", "Off"};

static const struct soc_enum tegra_enum[] = {
	SOC_ENUM_SINGLE_EXT(5, jack_function),
	SOC_ENUM_SINGLE_EXT(2, lineout_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new tegra_controls[] = {
	SOC_ENUM_EXT("Jack Function", tegra_enum[0], tegra_get_jack,
			tegra_set_jack),
	SOC_ENUM_EXT("Lineout Function", tegra_enum[1], tegra_get_lineout,
			tegra_set_lineout),
	SOC_ENUM_EXT("Speaker Function", tegra_enum[2], tegra_get_spk,
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
			tegra_lineout_func = TEGRA_LINEOUT_OFF;
			tegra_spk_func = TEGRA_INT_SPK_ON;
		}
		else if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_EAR_SPEAKER) {
			tegra_spk_func = TEGRA_INT_SPK_OFF;
			tegra_lineout_func = TEGRA_LINEOUT_ON;
		}
		else {
			tegra_lineout_func = TEGRA_LINEOUT_OFF;
			tegra_spk_func = TEGRA_INT_SPK_OFF;
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

struct snd_kcontrol_new tegra_capture_route_control = {
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

struct snd_kcontrol_new tegra_call_mode_control = {
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Call Mode Switch",
	.private_value = 0xffff,
	.info = tegra_call_mode_info,
	.get = tegra_call_mode_get,
	.put = tegra_call_mode_put
};

int tegra_controls_init(struct snd_soc_codec *codec)
{
	int err;

	if (!audio_data) {
		audio_data = kzalloc(sizeof(*audio_data), GFP_KERNEL);
		if (!audio_data) {
			pr_err("failed to allocate tegra_audio_data \n");
			return -ENOMEM;
		}

		/* Add tegra specific controls */
		err = snd_soc_add_controls(codec, tegra_controls,
						ARRAY_SIZE(tegra_controls));
		if (err < 0)
			goto fail;

		/* Add tegra specific widgets */
		snd_soc_dapm_new_controls(codec, tegra_dapm_widgets,
						ARRAY_SIZE(tegra_dapm_widgets));

		/* Set up tegra specific audio path audio_map */
		snd_soc_dapm_add_routes(codec, audio_map,
				ARRAY_SIZE(audio_map));

		audio_data->codec = codec;
		/* Add play route control */
		err = snd_ctl_add(codec->card,
			snd_ctl_new1(&tegra_play_route_control, NULL));
		if (err < 0)
			goto fail;

		/* Add capture route control */
		err = snd_ctl_add(codec->card,
			snd_ctl_new1(&tegra_capture_route_control, NULL));
		if (err < 0)
			goto fail;

		/* Add call mode switch control */
		err = snd_ctl_add(codec->card,
			snd_ctl_new1(&tegra_call_mode_control, NULL));
		if (err < 0)
			goto fail;

		/* Add jack detection */
		err = tegra_jack_init(codec);
		if (err < 0)
			goto fail;

		/* Default to HP output */
		tegra_jack_func = TEGRA_HP;
		tegra_lineout_func = TEGRA_LINEOUT_ON;
		tegra_spk_func = TEGRA_INT_SPK_ON;
		tegra_ext_control(codec);

		snd_soc_dapm_sync(codec);
	}

	return 0;

fail:
	if (audio_data) {
		kfree(audio_data);
		audio_data = 0;
	}
	return err;
}

void tegra_controls_exit(void)
{
	tegra_jack_exit();

	if (audio_data) {
		kfree(audio_data);
		audio_data = 0;
	}
}
