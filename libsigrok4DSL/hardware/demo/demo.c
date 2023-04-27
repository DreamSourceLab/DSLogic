/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2011 Olivier Fauchon <olivier@aixmarseille.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "demo.h"
#include <math.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <inttypes.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#endif

#include "../../log.h"
#include "demo_file.c"

/* Message logging helpers with subsystem-specific prefix string. */

#undef LOG_PREFIX 
#define LOG_PREFIX "demo: "

/* The size of chunks to send through the session bus. */
/* TODO: Should be configurable. */
#define BUFSIZE                512*1024
#define DSO_BUFSIZE            10*1024

static struct DEMO_channels channel_modes[] = {
    // LA Stream
    {DEMO_LOGIC100x16,  LOGIC,  SR_CHANNEL_LOGIC,  16, 1, SR_MHZ(1), SR_Mn(1),
     SR_KHZ(10), SR_MHZ(100), "Use 16 Channels (Max 20MHz)"},

    // DAQ
    {DEMO_ANALOG10x2,   ANALOG, SR_CHANNEL_ANALOG,  2,  8, SR_MHZ(1), SR_Mn(1),
     SR_HZ(10),  SR_MHZ(10), "Use Channels 0~1 (Max 10MHz)"},

    // OSC
    {DEMO_DSO200x2,     DSO,    SR_CHANNEL_DSO,     2,  8, SR_MHZ(100), SR_Kn(10),
     SR_HZ(100), SR_MHZ(200), "Use Channels 0~1 (Max 200MHz)"},
};


/* Private, per-device-instance driver context. */
/* TODO: struct context as with the other drivers. */

/* List of struct sr_dev_inst, maintained by dev_open()/dev_close(). */
SR_PRIV struct sr_dev_driver demo_driver_info;
static struct sr_dev_driver *di = &demo_driver_info;

extern struct ds_trigger *trigger;

static int hw_init(struct sr_context *sr_ctx)
{
    return std_hw_init(sr_ctx, di, LOG_PREFIX);
}

static void adjust_samplerate(struct demo_context *devc)
{
    devc->samplerates_max_index = ARRAY_SIZE(samplerates) - 1;
    while (samplerates[devc->samplerates_max_index] >
           channel_modes[devc->ch_mode].max_samplerate)
        devc->samplerates_max_index--;

    devc->samplerates_min_index = 0;
    while (samplerates[devc->samplerates_min_index] <
           channel_modes[devc->ch_mode].min_samplerate)
        devc->samplerates_min_index++;

    assert(devc->samplerates_max_index >= devc->samplerates_min_index);

    if (devc->cur_samplerate > samplerates[devc->samplerates_max_index])
        devc->cur_samplerate = samplerates[devc->samplerates_max_index];

    if (devc->cur_samplerate < samplerates[devc->samplerates_min_index])
        devc->cur_samplerate = samplerates[devc->samplerates_min_index];
}

static void probe_init(struct sr_dev_inst *sdi)
{
    GSList *l;
    struct demo_context *devc = sdi->priv;

   for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        probe->bits = channel_modes[devc->ch_mode].unit_bits;
        probe->vdiv = 1000;
        probe->vfactor = 1;
        probe->coupling = SR_AC_COUPLING;
        probe->trig_value = (1 << (probe->bits - 1));
        probe->hw_offset = (1 << (probe->bits - 1));
        probe->offset = probe->hw_offset +
                        (probe->index - (channel_modes[devc->ch_mode].num - 1) /2.0) * (1 << (probe->bits - 2));

        probe->map_default = TRUE;
        probe->map_unit = probeMapUnits[0];
        probe->map_min = -(probe->vdiv * probe->vfactor * DS_CONF_DSO_VDIVS / 2000.0);
        probe->map_max = probe->vdiv * probe->vfactor * DS_CONF_DSO_VDIVS / 2000.0;
    }
}

static int setup_probes(struct sr_dev_inst *sdi, int num_probes)
{
    uint16_t j;
    struct sr_channel *probe;
    struct demo_context *devc = sdi->priv;

    for (j = 0; j < num_probes; j++) {
        if (!(probe = sr_channel_new(j, channel_modes[devc->ch_mode].type,
                                   TRUE, probe_names[j])))
            return SR_ERR;
        sdi->channels = g_slist_append(sdi->channels, probe);
    }
    probe_init(sdi);
    return SR_OK;
}

static int get_file_mode(const char *filename)
{
    char **sections, **keys, *metafile, *val;
    int mode = LOGIC;
    GKeyFile *kf;
    unzFile archive;
    unz_file_info64 fileInfo;
    char szFilePath[15];
    int i, j;

    archive = NULL;

    if (!filename)
    {
        sr_err("%s: filename was NULL", __func__);
        return SR_ERR_ARG;
    }

    archive = unzOpen64(filename);
    if (NULL == archive)
    {
        sr_err("load zip file error:%s", filename);
        return SR_ERR;
    }
    if (unzLocateFile(archive, "header", 0) != UNZ_OK)
    {
        unzClose(archive);
        sr_err("unzLocateFile error:'header', %s", filename);
        return SR_ERR;
    }
    if (unzGetCurrentFileInfo64(archive, &fileInfo, szFilePath,
                                sizeof(szFilePath), NULL, 0, NULL, 0) != UNZ_OK)
    {
        unzClose(archive);
        sr_err("unzGetCurrentFileInfo64 error,'header', %s", filename);
        return SR_ERR;
    }
    if (unzOpenCurrentFile(archive) != UNZ_OK)
    {
        sr_err("cant't open zip inner file:'header',%s", filename);
        unzClose(archive);
        return SR_ERR;
    }

    if (!(metafile = g_try_malloc(fileInfo.uncompressed_size)))
    {
        sr_err("%s: metafile malloc failed", __func__);
        return SR_ERR_MALLOC;
    }

    unzReadCurrentFile(archive, metafile, fileInfo.uncompressed_size);
    unzCloseCurrentFile(archive);

    if (unzClose(archive) != UNZ_OK)
    {
        sr_err("close zip archive error:%s", filename);
        return SR_ERR;
    }
    archive = NULL;

    kf = g_key_file_new();
    if (!g_key_file_load_from_data(kf, metafile, fileInfo.uncompressed_size, 0, NULL))
    {
        sr_err("Failed to parse metadata.");
        return SR_ERR;
    }

    // Get mode value.
    sections = g_key_file_get_groups(kf, NULL);
    for (i = 0; sections[i]; i++)
    {
        if (strncmp(sections[i], "header", 6) == 0)
        {
            keys = g_key_file_get_keys(kf, sections[i], NULL, NULL);
            for (j = 0; keys[j]; j++)
            {
                if (strcmp(keys[j], "device mode") == 0)
                {
                    val = g_key_file_get_string(kf, sections[i], keys[j], NULL);
                    mode = strtoull(val, NULL, 10);
                    break;
                }
            }
            g_strfreev(keys);
        }
    }
    g_strfreev(sections);
    g_key_file_free(kf);
    g_free(metafile);

    return mode;
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	// struct drv_context *drvc;
    // struct demo_context *devc;
    struct session_vdev *vdev;
	GSList *devices;
    char str[500];

	(void)options;
	// drvc = di->priv;
	devices = NULL;

    sr_info("%s", "Scan demo device.");

    // if (!(devc = g_try_malloc(sizeof(struct demo_context)))) {
    //     sr_err("Device context malloc failed.");
    //     return NULL;
    // }
    // devc->profile = &supported_Demo[0];
    // devc->ch_mode = devc->profile->dev_caps.default_channelmode;
    // devc->cur_samplerate = channel_modes[devc->ch_mode].default_samplerate;
    // devc->limit_samples = channel_modes[devc->ch_mode].default_samplelimit;
    // devc->limit_samples_show = devc->limit_samples;
    // devc->limit_msec = 0;
    // devc->sample_generator = devc->profile->dev_caps.default_pattern;
    // devc->timebase = devc->profile->dev_caps.default_timebase;
    // devc->max_height = 0;
    // adjust_samplerate(devc);

    // sdi = sr_dev_inst_new(channel_modes[devc->ch_mode].mode, SR_ST_INITIALIZING,
    //                       devc->profile->vendor,
    //                       devc->profile->model,
    //                       devc->profile->model_version);

    vdev = g_try_malloc0(sizeof(struct session_vdev));
    if (vdev == NULL)
    {
        sr_err("%s: sdi->priv malloc failed", __func__);
        return SR_ERR_MALLOC;
    }
    sdi = sr_dev_inst_new(LOGIC, SR_ST_INACTIVE, 
                          supported_Demo[0].vendor,
                          supported_Demo[0].model, 
                          supported_Demo[0].model_version);
	if (!sdi) {
        g_free(vdev);
        sr_err("Device instance creation failed.");
		return NULL;
	}
    sdi->priv = vdev;
	sdi->driver = di;
    sdi->dev_type = DEV_TYPE_DEMO;

    strcpy(str,DS_RES_PATH);
    sdi->path = g_strdup(strcat(str,"demo/demo_uart.dsl"));
    
    vdev->max_height = 0;

    packet_interval = g_timer_new();

    devices = g_slist_append(devices, sdi);

	return devices;
}

static const GSList *hw_dev_mode_list(const struct sr_dev_inst *sdi)
{
    // struct demo_context *devc;
    GSList *l = NULL;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(sr_mode_list); i++) {
        // if (devc->profile->dev_caps.mode_caps & (1 << i))
        if (supported_Demo[0].dev_caps.mode_caps & (1 << i))
            l = g_slist_append(l, &sr_mode_list[i]);
    }

    return l;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
    return dev_open_file(sdi);

    //(void)sdi;
    // struct demo_context *const devc = sdi->priv;

    // sdi->status = SR_ST_ACTIVE;

    // if (pipe(devc->pipe_fds)) {
    //     /* TODO: Better error message. */
    //     sr_err("%s: pipe() failed", __func__);
    //     return SR_ERR;
    // }
    // devc->channel = g_io_channel_unix_new(devc->pipe_fds[0]);
    // g_io_channel_set_flags(devc->channel, G_IO_FLAG_NONBLOCK, NULL);
    // /* Set channel encoding to binary (default is UTF-8). */
    // g_io_channel_set_encoding(devc->channel, NULL, NULL);
    // /* Make channels to unbuffered. */
    // g_io_channel_set_buffered(devc->channel, FALSE);

	// return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
    return dev_close_file(sdi);

    // //(void)sdi;
    // struct demo_context *const devc = sdi->priv;

    // if (sdi->status == SR_ST_ACTIVE && devc->channel) {
    //     g_io_channel_shutdown(devc->channel, FALSE, NULL);
    //     g_io_channel_unref(devc->channel);
    //     devc->channel = NULL;
    // }
    // sdi->status = SR_ST_INACTIVE;

    // return SR_OK;
}

static int dev_destroy(struct sr_dev_inst *sdi)
{
    assert(sdi);
    dev_close_file(sdi);
    sdi->path = NULL; 
    sr_dev_inst_free(sdi);
}

static int hw_cleanup(void)
{ 
	return 0;
}

static unsigned int en_ch_num(const struct sr_dev_inst *sdi)
{
    
    GSList *l;
    unsigned int channel_en_cnt = 0;

    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        channel_en_cnt += probe->enabled;
    }

    return channel_en_cnt;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
                      const struct sr_channel *ch,
                      const struct sr_channel_group *cg)
{
    return config_get_file(id,data,sdi,ch,cg);

    // (void) cg;

    // struct demo_context *devc;

    // assert(sdi);
    // assert(sdi->priv);

    // devc = sdi->priv;

	// switch (id) {
	// case SR_CONF_SAMPLERATE:
	// 	*data = g_variant_new_uint64(devc->cur_samplerate);
	// 	break;
	// case SR_CONF_LIMIT_SAMPLES:
    //     *data = g_variant_new_uint64(devc->limit_samples_show);
	// 	break;
	// case SR_CONF_LIMIT_MSEC:
	// 	*data = g_variant_new_uint64(devc->limit_msec);
	// 	break;
    // case SR_CONF_DEVICE_MODE:
    //     *data = g_variant_new_int16(sdi->mode);
    //     break;
    // case SR_CONF_TEST:
    //     *data = g_variant_new_boolean(FALSE);
    //     break;
    // case SR_CONF_INSTANT:
    //     *data = g_variant_new_boolean(devc->instant);
    //     break;
    // case SR_CONF_PATTERN_MODE:
    //     *data = g_variant_new_string(pattern_strings[devc->sample_generator]);
	// 	break;
    // case SR_CONF_MAX_HEIGHT:
    //     *data = g_variant_new_string(maxHeights[devc->max_height]);
    //     break;
    // case SR_CONF_MAX_HEIGHT_VALUE:
    //     *data = g_variant_new_byte(devc->max_height);
    //     break;
    // case SR_CONF_PROBE_OFFSET:
    //     *data = g_variant_new_uint16(ch->offset);
    //     break;
    // case SR_CONF_PROBE_HW_OFFSET:
    //     *data = g_variant_new_uint16(ch->hw_offset);
    //     break;
    // case SR_CONF_PROBE_VDIV:
    //     *data = g_variant_new_uint64(ch->vdiv);
    //     break;
    // case SR_CONF_PROBE_FACTOR:
    //     *data = g_variant_new_uint64(ch->vfactor);
    //     break;
    // case SR_CONF_TIMEBASE:
    //     *data = g_variant_new_uint64(devc->timebase);
    //     break;
    // case SR_CONF_MAX_TIMEBASE:
    //     if (!sdi)
    //         return SR_ERR;
    //     *data = g_variant_new_uint64(MAX_TIMEBASE);
    //     break;
    // case SR_CONF_MIN_TIMEBASE:
    //     if (!sdi)
    //         return SR_ERR;
    //     *data = g_variant_new_uint64(MIN_TIMEBASE);
    //     break;
    // case SR_CONF_PROBE_COUPLING:
    //     *data = g_variant_new_byte(ch->coupling);
    //     break;
    // case SR_CONF_TRIGGER_VALUE:
    //     *data = g_variant_new_byte(ch->trig_value);
    //     break;
    // case SR_CONF_PROBE_EN:
    //     *data = g_variant_new_boolean(ch->enabled);
    //     break;
    // case SR_CONF_MAX_DSO_SAMPLERATE:
    //     *data = g_variant_new_uint64(channel_modes[devc->ch_mode].max_samplerate);
    //     break;
    // case SR_CONF_MAX_DSO_SAMPLELIMITS:
    //     *data = g_variant_new_uint64(devc->profile->dev_caps.dso_depth);
    //     break;
    // case SR_CONF_HW_DEPTH:
    //     *data = g_variant_new_uint64(devc->profile->dev_caps.hw_depth / channel_modes[devc->ch_mode].unit_bits);
    //     break;
    // case SR_CONF_UNIT_BITS:
    //     *data = g_variant_new_byte(channel_modes[devc->ch_mode].unit_bits);
    //     break;
    // case SR_CONF_PROBE_MAP_DEFAULT:
    //     if (!sdi || !ch)
    //         return SR_ERR;
    //     *data = g_variant_new_boolean(ch->map_default);
    //     break;
    // case SR_CONF_PROBE_MAP_UNIT:
    //     if (!sdi || !ch)
    //         return SR_ERR;
    //     *data = g_variant_new_string(ch->map_unit);
    //     break;
    // case SR_CONF_PROBE_MAP_MIN:
    //     if (!sdi || !ch)
    //         return SR_ERR;
    //     *data = g_variant_new_double(ch->map_min);
    //     break;
    // case SR_CONF_PROBE_MAP_MAX:
    //     if (!sdi || !ch)
    //         return SR_ERR;
    //     *data = g_variant_new_double(ch->map_max);
    //     break;
    // case SR_CONF_VLD_CH_NUM:
    //     *data = g_variant_new_int16(channel_modes[devc->ch_mode].num);
    //     break;
    // case SR_CONF_HAVE_ZERO:
    //     if (!sdi)
    //         return SR_ERR;
    //     *data = g_variant_new_boolean(devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ZERO);
    //     break;
    // default:
	// 	return SR_ERR_NA;
	// }

	// return SR_OK;
}

static int config_set(int id, GVariant *data, struct sr_dev_inst *sdi,
                      struct sr_channel *ch,
                      struct sr_channel_group *cg)
{
    return config_set_file(id,data,sdi,ch,cg);

    // (void) cg;

    // uint16_t i;
    // int ret, num_probes;
	// const char *stropt;
    // uint64_t tmp_u64;
    // struct demo_context *devc;

    // assert(sdi);
    // assert(sdi->priv);
    
    // devc = sdi->priv;

	// if (sdi->status != SR_ST_ACTIVE)
	// 	return SR_ERR_DEV_CLOSED;

	// if (id == SR_CONF_SAMPLERATE) {
	// 	devc->cur_samplerate = g_variant_get_uint64(data);
    //     devc->samples_counter = 0;
    //     devc->pre_index = 0;
	// 	sr_dbg("%s: setting samplerate to %llu", __func__,
	// 	       devc->cur_samplerate);
	// 	ret = SR_OK;
	// }
    // else if (id == SR_CONF_LIMIT_SAMPLES) {
    //     devc->limit_msec = 0;
    //     devc->limit_samples = g_variant_get_uint64(data);
    //     devc->limit_samples = (devc->limit_samples + 63) & ~63;
    //     devc->limit_samples_show = devc->limit_samples;
    //     if (sdi->mode == DSO && en_ch_num(sdi) == 1) {
    //         devc->limit_samples /= 2;
    //     }
	// 	sr_dbg("%s: setting limit_samples to %llu", __func__,
	// 	       devc->limit_samples);
	// 	ret = SR_OK;
	// } 
    // else if (id == SR_CONF_LIMIT_MSEC) {
	// 	devc->limit_msec = g_variant_get_uint64(data);
	// 	devc->limit_samples = 0;
    //     devc->limit_samples_show = devc->limit_samples;
	// 	sr_dbg("%s: setting limit_msec to %llu", __func__,
	// 	       devc->limit_msec);
    //     ret = SR_OK;
    // } 
    // else if (id == SR_CONF_DEVICE_MODE) {
    //     sdi->mode = g_variant_get_int16(data);
    //     ret = SR_OK;
    //     for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
    //         if ((int)channel_modes[i].mode == sdi->mode &&
    //             devc->profile->dev_caps.channels & (1 << i)) {
    //             devc->ch_mode = channel_modes[i].id;
    //             break;
    //         }
    //     }
    //     num_probes = channel_modes[devc->ch_mode].num;
    //     devc->cur_samplerate = channel_modes[devc->ch_mode].default_samplerate;
    //     devc->limit_samples = channel_modes[devc->ch_mode].default_samplelimit;
    //     devc->limit_samples_show = devc->limit_samples;
    //     devc->timebase = devc->profile->dev_caps.default_timebase;
    //     sr_dev_probes_free(sdi);
    //     setup_probes(sdi, num_probes);
    //     adjust_samplerate(devc);
    //     sr_info("%s: setting mode to %d", __func__, sdi->mode);
    // }
    // else if (id == SR_CONF_PATTERN_MODE) {
    //     stropt = g_variant_get_string(data, NULL);
    //     ret = SR_OK;
    //     if (!strcmp(stropt, pattern_strings[PATTERN_SINE])) {
    //         devc->sample_generator = PATTERN_SINE;
    //     } else if (!strcmp(stropt, pattern_strings[PATTERN_SQUARE])) {
    //         devc->sample_generator = PATTERN_SQUARE;
    //     } else if (!strcmp(stropt, pattern_strings[PATTERN_TRIANGLE])) {
    //         devc->sample_generator = PATTERN_TRIANGLE;
    //     } else if (!strcmp(stropt, pattern_strings[PATTERN_SAWTOOTH])) {
    //         devc->sample_generator = PATTERN_SAWTOOTH;
    //     } else if (!strcmp(stropt, pattern_strings[PATTERN_RANDOM])) {
    //         devc->sample_generator = PATTERN_RANDOM;
	// 	} else {
    //         ret = SR_ERR;
	// 	}
    //     sr_dbg("%s: setting pattern to %d",
	// 		__func__, devc->sample_generator);
    // }
    // else if (id == SR_CONF_MAX_HEIGHT) {
    //     stropt = g_variant_get_string(data, NULL);
    //     ret = SR_OK;
    //     for (i = 0; i < ARRAY_SIZE(maxHeights); i++) {
    //         if (!strcmp(stropt, maxHeights[i])) {
    //             devc->max_height = i;
    //             break;
    //         }
    //     }
    //     sr_dbg("%s: setting Signal Max Height to %d",
    //         __func__, devc->max_height);
    // }
    // else if (id == SR_CONF_INSTANT) {
    //     devc->instant = g_variant_get_boolean(data);
    //     sr_dbg("%s: setting INSTANT mode to %d", __func__,
    //            devc->instant);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_HORIZ_TRIGGERPOS) {
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_TRIGGER_HOLDOFF) {
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_TRIGGER_MARGIN) {
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_EN) {
    //     ch->enabled = g_variant_get_boolean(data);
    //     if (en_ch_num(sdi) != 0) {
    //         devc->limit_samples_show = devc->profile->dev_caps.dso_depth / en_ch_num(sdi);
    //     }
    //     sr_dbg("%s: setting ENABLE of channel %d to %d", __func__,
    //            ch->index, ch->enabled);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_VDIV) {
    //     tmp_u64 = g_variant_get_uint64(data);
    //     ch->vdiv = tmp_u64;
    //     sr_dbg("%s: setting VDIV of channel %d to %llu", __func__,
    //            ch->index, ch->vdiv);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_FACTOR) {
    //     ch->vfactor = g_variant_get_uint64(data);
    //     sr_dbg("%s: setting FACTOR of channel %d to %llu", __func__,
    //            ch->index, ch->vfactor);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_OFFSET) {
    //     ch->offset = g_variant_get_uint16(data);
    //     sr_dbg("%s: setting OFFSET of channel %d to %d", __func__,
    //            ch->index, ch->offset);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_TIMEBASE) {
    //     devc->timebase = g_variant_get_uint64(data);
    //     sr_dbg("%s: setting TIMEBASE to %llu", __func__,
    //            devc->timebase);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_COUPLING) {
    //     ch->coupling = g_variant_get_byte(data);
    //     sr_dbg("%s: setting AC COUPLING of channel %d to %d", __func__,
    //            ch->index, ch->coupling);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_TRIGGER_SOURCE) {
    //     devc->trigger_source = g_variant_get_byte(data);
    //     sr_dbg("%s: setting Trigger Source to %d",
    //         __func__, devc->trigger_source);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_TRIGGER_SLOPE) {
    //     devc->trigger_slope = g_variant_get_byte(data);
    //     sr_dbg("%s: setting Trigger Slope to %d",
    //         __func__, devc->trigger_slope);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_TRIGGER_VALUE) {
    //     ch->trig_value = g_variant_get_byte(data);
    //     sr_dbg("%s: setting channel %d Trigger Value to %d",
    //         __func__, ch->index, ch->trig_value);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_MAP_DEFAULT) {
    //     ch->map_default = g_variant_get_boolean(data);
    //     if (ch->map_default) {
    //         ch->map_unit = probeMapUnits[0];
    //         ch->map_min = -(ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0);
    //         ch->map_max = ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0;
    //     }
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_MAP_UNIT) {
    //     if (ch->map_default)
    //         ch->map_unit = probeMapUnits[0];
    //     else
    //         ch->map_unit = g_variant_get_string(data, NULL);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_MAP_MIN) {
    //     if (ch->map_default)
    //         ch->map_min = -(ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0);
    //     else
    //         ch->map_min = g_variant_get_double(data);
    //     ret = SR_OK;
    // }
    // else if (id == SR_CONF_PROBE_MAP_MAX) {
    //     if (ch->map_default)
    //         ch->map_max = ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0;
    //     else
    //         ch->map_max = g_variant_get_double(data);
    //     ret = SR_OK;
    // }
    // else {
    //     ret = SR_ERR_NA;
	// }

	// return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
                       const struct sr_channel_group *cg)
{
    return config_list_file(key,data,sdi,cg);

//     struct demo_context *devc;
// 	GVariant *gvar;
// 	GVariantBuilder gvb;
//     int i;

//     (void)cg;
//     devc = sdi->priv;

// 	switch (key) {
//     case SR_CONF_DEVICE_OPTIONS:
// //		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
// //				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
//         *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
//                 hwoptions, ARRAY_SIZE(hwoptions)*sizeof(int32_t), TRUE, NULL, NULL);
//         break;
//     case SR_CONF_DEVICE_SESSIONS:
//         *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
//                 sessions, ARRAY_SIZE(sessions)*sizeof(int32_t), TRUE, NULL, NULL);
//         break;
//     case SR_CONF_SAMPLERATE:
// 		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
//         gvar = g_variant_new_from_data(G_VARIANT_TYPE("at"),
//                                        samplerates + devc->samplerates_min_index,
//                                        (devc->samplerates_max_index - devc->samplerates_min_index + 1) * sizeof(uint64_t), TRUE, NULL, NULL);
//         g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
// 		*data = g_variant_builder_end(&gvb);
// 		break;
//     case SR_CONF_PATTERN_MODE:
// 		*data = g_variant_new_strv(pattern_strings, ARRAY_SIZE(pattern_strings));
// 		break;
//     case SR_CONF_MAX_HEIGHT:
//         *data = g_variant_new_strv(maxHeights, ARRAY_SIZE(maxHeights));
//         break;

//     case SR_CONF_PROBE_CONFIGS:
//         *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
//                 probeOptions, ARRAY_SIZE(probeOptions)*sizeof(int32_t), TRUE, NULL, NULL);
//         break;

//     case SR_CONF_PROBE_VDIV:
//         g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
//         for (i = 0; devc->profile->dev_caps.vdivs[i]; i++);
//         gvar = g_variant_new_from_data(G_VARIANT_TYPE("at"),
//                 devc->profile->dev_caps.vdivs, i*sizeof(uint64_t), TRUE, NULL, NULL);
//         g_variant_builder_add(&gvb, "{sv}", "vdivs", gvar);
//         *data = g_variant_builder_end(&gvb);
//         break;
//     case SR_CONF_PROBE_COUPLING:
//         g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
//         gvar = g_variant_new_from_data(G_VARIANT_TYPE("ay"),
//                 probeCoupling, ARRAY_SIZE(probeCoupling)*sizeof(uint8_t), TRUE, NULL, NULL);
//         g_variant_builder_add(&gvb, "{sv}", "coupling", gvar);
//         *data = g_variant_builder_end(&gvb);
//         break;
//     case SR_CONF_PROBE_MAP_UNIT:
//         *data = g_variant_new_strv(probeMapUnits, ARRAY_SIZE(probeMapUnits));
//         break;
// 	default:
//         return SR_ERR_NA;
// 	}

//     return SR_OK;
}

static void samples_generator(uint16_t *buf, uint64_t size,
                              const struct sr_dev_inst *sdi,
                              struct demo_context *devc)
{
    uint64_t i, pre0_i, pre1_i;
    GSList *l;
    struct sr_channel *probe;
    unsigned int start_rand;
    double span = 1;
    const uint64_t len = ARRAY_SIZE(sinx) - 1;
    const int *pre_buf;
    uint16_t tmp_u16 = 0;
    unsigned int ch_num = en_ch_num(sdi) ? en_ch_num(sdi) : 1;

    switch (devc->sample_generator) {
    case PATTERN_SINE: /* Sine */
        pre_buf = sinx;
        break;
    case PATTERN_SQUARE:
        pre_buf = sqrx;
        break;
    case PATTERN_TRIANGLE:
        pre_buf = trix;
        break;
    case PATTERN_SAWTOOTH:
        pre_buf = sawx;
        break;
    case PATTERN_RANDOM:
        pre_buf = ranx;
        break;
    default:
        pre_buf = sinx;
        break;
    }

    if (sdi->mode == LOGIC) {
        for (i = 0; i < size; i++) {
            //index = (i/10/g_slist_length(sdi->channels)+start_rand)%len;
            //*(buf + i) = (uint16_t)(((const_dc+pre_buf[index]) << 8) + (const_dc+pre_buf[index]));
            tmp_u16 = 0;
            if (i < ch_num*4)
                *(buf + i) = tmp_u16;
            else if (i % 4 == 0) {
                start_rand = rand() % (ch_num * 4);
                if (start_rand == (i/4 % ch_num))
                    tmp_u16 = 0xffff;
                *(buf + i) = tmp_u16 ? ~*(buf + i - ch_num*4) : *(buf + i - ch_num*4);
            } else {
                *(buf + i) = *(buf + i - 1);
            }
        }
    } else {
        if (sdi->mode == DSO) {
            if (ch_num == 1)
                span = 2 * channel_modes[devc->ch_mode].max_samplerate / devc->cur_samplerate;
            else
                span = channel_modes[devc->ch_mode].max_samplerate / devc->cur_samplerate;
        } else if (sdi->mode == ANALOG) {
            span = len * 20.0 / devc->limit_samples;
        }

        if (devc->pre_index == 0) {
            devc->mstatus.ch0_max = 0;
            devc->mstatus.ch0_min = 255;
            devc->mstatus.ch1_max = 0;
            devc->mstatus.ch1_min = 255;
            devc->mstatus.ch0_cyc_tlen = 0;
            devc->mstatus.ch0_cyc_cnt = 1;
            devc->mstatus.ch1_cyc_tlen = 0;
            devc->mstatus.ch1_cyc_cnt = 1;
            devc->mstatus.ch0_level_valid = TRUE;
            devc->mstatus.ch0_plevel = TRUE;
            devc->mstatus.ch1_level_valid = TRUE;
            devc->mstatus.ch1_plevel = TRUE;
        }

        if (sdi->mode == DSO)
            memset(buf, 0, size*sizeof(uint16_t));
        else if (sdi->mode == ANALOG)
            memset(buf, 0, size*sizeof(uint16_t));

        for (l = sdi->channels; l; l = l->next) {
            start_rand = devc->pre_index * span;
            probe = (struct sr_channel *)l->data;
            pre0_i = devc->pre_index;
            pre1_i = devc->pre_index;
            for (i = 0; i < size; i++) {
                if (probe->coupling == SR_DC_COUPLING) {
                    *(buf + i) += (uint8_t)(probe->hw_offset + (1000.0/probe->vdiv) * (pre_buf[(uint64_t)(i*span+start_rand)%len] - const_dc)) << (probe->index * 8);
                } else if (probe->coupling == SR_AC_COUPLING) {
                    *(buf + i) += (uint8_t)(probe->hw_offset + (1000.0/probe->vdiv) * pre_buf[(uint64_t)(i*span+start_rand)%len]) << (probe->index * 8);
                } else {
                    *(buf + i) += probe->hw_offset << (probe->index * 8);
                }

                if (probe->index == 0) {
                    devc->mstatus.ch0_max = MAX(devc->mstatus.ch0_max, (*(buf + i) & 0x00ff));
                    devc->mstatus.ch0_min = MIN(devc->mstatus.ch0_min, (*(buf + i) & 0x00ff));
                    if (pre_buf[(uint64_t)(i*span+start_rand)%len] < 0 &&
                        pre_buf[(uint64_t)((i-1)*span+start_rand)%len] > 0) {
                        devc->mstatus.ch0_cyc_tlen = 2*(i - pre0_i)*pow(10, 8)/channel_modes[devc->ch_mode].max_samplerate;
                        devc->mstatus.ch0_cyc_cnt++;
                        pre0_i = i;
                    }
                } else {
                    devc->mstatus.ch1_max = MAX(devc->mstatus.ch1_max, ((*(buf + i) & 0xff00) >> 8));
                    devc->mstatus.ch1_min = MIN(devc->mstatus.ch1_min, ((*(buf + i) & 0xff00) >> 8));
                    if (pre_buf[(uint64_t)(i*span+start_rand)%len] < 0 &&
                        pre_buf[(uint64_t)((i-1)*span+start_rand)%len] > 0) {
                        devc->mstatus.ch1_cyc_tlen = 2*(i - pre1_i)*pow(10, 8)/channel_modes[devc->ch_mode].max_samplerate;
                        devc->mstatus.ch1_cyc_cnt++;
                        pre1_i = i;
                    }
                }
            }
        }

        for (l = sdi->channels; l; l = l->next) {
            probe = (struct sr_channel *)l->data;
            if (!probe->enabled) {
                devc->mstatus.ch1_max = MAX(devc->mstatus.ch0_max, devc->mstatus.ch1_max);
                devc->mstatus.ch1_min = MIN(devc->mstatus.ch0_min, devc->mstatus.ch1_min);
                devc->mstatus.ch0_max = MAX(devc->mstatus.ch0_max, devc->mstatus.ch1_max);
                devc->mstatus.ch0_min = MIN(devc->mstatus.ch0_min, devc->mstatus.ch1_min);
                break;
            }
        }

        devc->mstatus.ch0_cyc_tlen *= devc->mstatus.ch0_cyc_cnt;
        devc->mstatus.ch1_cyc_tlen *= devc->mstatus.ch1_cyc_cnt;

        devc->mstatus.ch0_high_level = devc->mstatus.ch0_max;
        devc->mstatus.ch0_low_level = devc->mstatus.ch0_min;
        devc->mstatus.ch1_high_level = devc->mstatus.ch1_max;
        devc->mstatus.ch1_low_level = devc->mstatus.ch1_min;
        devc->mstatus.ch0_cyc_llen = 0;
        devc->mstatus.ch1_cyc_llen = 0;
        devc->mstatus.ch0_cyc_plen = devc->mstatus.ch0_cyc_tlen / 2;
        devc->mstatus.ch1_cyc_plen = devc->mstatus.ch1_cyc_tlen / 2;
        devc->mstatus.ch0_cyc_rlen = devc->mstatus.ch0_cyc_tlen / 4;
        devc->mstatus.ch0_cyc_flen = devc->mstatus.ch0_cyc_tlen / 4;
        devc->mstatus.ch1_cyc_rlen = devc->mstatus.ch1_cyc_tlen / 4;
        devc->mstatus.ch1_cyc_flen = devc->mstatus.ch1_cyc_tlen / 4;

        for (l = sdi->channels; l; l = l->next) {
            probe = (struct sr_channel *)l->data;
            if (probe->index == 0) {
                devc->mstatus.ch0_acc_mean = (probe->coupling == SR_AC_COUPLING) ? probe->hw_offset * devc->limit_samples_show :
                                             (devc->mstatus.ch0_max + devc->mstatus.ch0_min) / 2.0 * devc->limit_samples_show;
                devc->mstatus.ch0_acc_square = (probe->coupling == SR_AC_COUPLING) ? pow((devc->mstatus.ch0_max - probe->hw_offset) * 0.707, 2) * devc->limit_samples_show :
                                               pow((devc->mstatus.ch0_max - devc->mstatus.ch0_min) * 0.707, 2) * devc->limit_samples_show;
            } else {
                devc->mstatus.ch1_acc_mean = (probe->coupling == SR_AC_COUPLING) ? probe->hw_offset * devc->limit_samples_show :
                                             (devc->mstatus.ch1_max + devc->mstatus.ch1_min) / 2.0 * devc->limit_samples_show;
                devc->mstatus.ch1_acc_square = (probe->coupling == SR_AC_COUPLING) ? pow((devc->mstatus.ch1_max - probe->hw_offset) * 0.707, 2) * devc->limit_samples_show :
                                               pow((devc->mstatus.ch1_max - devc->mstatus.ch1_min) * 0.707, 2) * devc->limit_samples_show;
            }
        }

        devc->mstatus.measure_valid = TRUE;
    }
}

/* Callback handling data */
static int receive_data(int fd, int revents, const struct sr_dev_inst *sdi)
{
    struct demo_context *devc = sdi->priv;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    struct sr_datafeed_dso dso;
    struct sr_datafeed_analog analog;
    double samples_elaspsed;
    uint64_t samples_to_send = 0, sending_now;
	int64_t time, elapsed;
    static uint16_t last_sample = 0;
    uint16_t cur_sample;
    uint64_t i;

	(void)fd;
	(void)revents;

    packet.status = SR_PKT_OK;
	/* How many "virtual" samples should we have collected by now? */
	time = g_get_monotonic_time();
	elapsed = time - devc->starttime;
    devc->starttime = time;
    //expected_samplenum = ceil(elapsed / 1000000.0 * devc->cur_samplerate);
	/* Of those, how many do we still have to send? */
    samples_elaspsed = elapsed / 1000000.0 * devc->cur_samplerate;

    if (devc->limit_samples) {
        if (sdi->mode == DSO && !devc->instant) {
            samples_to_send = ceil(samples_elaspsed);
        } else if (sdi->mode == ANALOG) {
            samples_to_send = ceil(samples_elaspsed);
        } else {
            samples_to_send = ceil(samples_elaspsed);
            samples_to_send += devc->samples_not_sent;
            if (samples_to_send < 64) {
                devc->samples_not_sent = samples_to_send;
                return TRUE;
            } else
                devc->samples_not_sent = samples_to_send & 63;
            samples_to_send = samples_to_send & ~63;
            samples_to_send = MIN(samples_to_send,
                     devc->limit_samples - devc->samples_counter);
        }
    }

    if (samples_to_send > 0 && !devc->stop) {
        sending_now = MIN(samples_to_send, (sdi->mode == DSO ) ? DSO_BUFSIZE : BUFSIZE);
        if (sdi->mode == DSO && !devc->instant) {
            if (en_ch_num(sdi) == 1) {
                devc->samples_counter += sending_now / 2;
                devc->samples_counter = min(devc->samples_counter, devc->limit_samples_show / 2);
            } else {
                devc->samples_counter += sending_now;
                devc->samples_counter = min(devc->samples_counter, devc->limit_samples_show);
            }
        } else {
            devc->samples_counter += sending_now;
        }

        if (sdi->mode == ANALOG)
            samples_generator(devc->buf, sending_now*2, sdi, devc);
        else if (sdi->mode == DSO)
            samples_generator(devc->buf, devc->samples_counter, sdi, devc);
        else
            samples_generator(devc->buf, sending_now, sdi, devc);

        if (devc->trigger_stage != 0) {
            for (i = 0; i < sending_now; i++) {
                if (devc->trigger_edge == 0) {
                    if ((*(devc->buf + i) | devc->trigger_mask) ==
                            (devc->trigger_value | devc->trigger_mask)) {
                        devc->trigger_stage = 0;
                        break;
                    }
                } else {
                    cur_sample = *(devc->buf + i);
                    if (((last_sample & devc->trigger_edge) ==
                         (~devc->trigger_value & devc->trigger_edge)) &&
                        ((cur_sample | devc->trigger_mask) ==
                         (devc->trigger_value | devc->trigger_mask)) &&
                        ((cur_sample & devc->trigger_edge) ==
                         (devc->trigger_value & devc->trigger_edge))) {
                        devc->trigger_stage = 0;
                        break;
                    }
                    last_sample = cur_sample;
                }
            }
            if (devc->trigger_stage == 0) {
                struct ds_trigger_pos demo_trigger_pos;
                demo_trigger_pos.real_pos = i;
                packet.type = SR_DF_TRIGGER;
                packet.payload = &demo_trigger_pos;
                ds_data_forward(sdi, &packet);
            }
        }

        if (devc->trigger_stage == 0){
            //samples_to_send -= sending_now;
            if (sdi->mode == LOGIC) {
                packet.type = SR_DF_LOGIC;
                packet.payload = &logic;
                logic.length = sending_now * (channel_modes[devc->ch_mode].num >> 3);
                logic.format = LA_CROSS_DATA;
                logic.data = devc->buf;
            } else if (sdi->mode == DSO) {
                packet.type = SR_DF_DSO;
                packet.payload = &dso;
                dso.probes = sdi->channels;
                if (devc->instant)
                    dso.num_samples = sending_now;
                else
                    dso.num_samples = devc->samples_counter;
                if (en_ch_num(sdi) == 1)
                    dso.num_samples *= 2;
                dso.mq = SR_MQ_VOLTAGE;
                dso.unit = SR_UNIT_VOLT;
                dso.mqflags = SR_MQFLAG_AC;
                dso.data = devc->buf;
            }else {
                packet.type = SR_DF_ANALOG;
                packet.payload = &analog;
                analog.probes = sdi->channels;
                analog.num_samples = sending_now;
                analog.unit_bits = channel_modes[devc->ch_mode].unit_bits;;
                analog.mq = SR_MQ_VOLTAGE;
                analog.unit = SR_UNIT_VOLT;
                analog.mqflags = SR_MQFLAG_AC;
                analog.data = devc->buf;
            }

            if (sdi->mode == DSO && !devc->instant) {
                if ((uint64_t)dso.num_samples < devc->limit_samples_show)
                    devc->pre_index = 0;
                else
                    devc->pre_index += sending_now;
            } else if (sdi->mode == ANALOG) {
                devc->pre_index += sending_now;
            }

            ds_data_forward(sdi, &packet);

            devc->mstatus.trig_hit = (devc->trigger_stage == 0);
            devc->mstatus.captured_cnt0 = devc->samples_counter;
            devc->mstatus.captured_cnt1 = devc->samples_counter >> 8;
            devc->mstatus.captured_cnt2 = devc->samples_counter >> 16;
            devc->mstatus.captured_cnt3 = devc->samples_counter >> 32;
        }
	}

    if ((sdi->mode == LOGIC || devc->instant) && devc->limit_samples &&
        devc->samples_counter >= devc->limit_samples) {
        sr_dbg("Requested number of samples reached.");
        hw_dev_acquisition_stop(sdi, NULL);
        return TRUE;
    }

    return TRUE;
}

static int hw_dev_acquisition_start(struct sr_dev_inst *sdi,
		void *cb_data)
{
    return dev_acquisition_start_file(sdi,cb_data);

    // struct demo_context *const devc = sdi->priv;

    // (void)cb_data;

    // if (sdi->status != SR_ST_ACTIVE)
    //     return SR_ERR_DEV_CLOSED;

    // //devc->cb_data = cb_data;
	// devc->samples_counter = 0;
    // devc->pre_index = 0;
    // devc->mstatus.captured_cnt0 = 0;
    // devc->mstatus.captured_cnt1 = 0;
    // devc->mstatus.captured_cnt2 = 0;
    // devc->mstatus.captured_cnt3 = 0;
    // devc->stop = FALSE;
    // devc->samples_not_sent = 0;

    // devc->trigger_stage = 0;

	// /*
	//  * Setting two channels connected by a pipe is a remnant from when the
	//  * demo driver generated data in a thread, and collected and sent the
	//  * data in the main program loop.
	//  * They are kept here because it provides a convenient way of setting
	//  * up a timeout-based polling mechanism.
	//  */

    // sr_session_source_add_channel(devc->channel, G_IO_IN | G_IO_ERR,
    //         50, receive_data, sdi);

	// /* Send header packet to the session bus. */
    // //std_session_send_df_header(cb_data, LOG_PREFIX);
    // std_session_send_df_header(sdi, LOG_PREFIX);

    // if (!(devc->buf = g_try_malloc(((sdi->mode == DSO ) ? DSO_BUFSIZE : (sdi->mode == ANALOG ) ? 2*BUFSIZE : BUFSIZE)*sizeof(uint16_t)))) {
    //     sr_err("buf for receive_data malloc failed.");
    //     return FALSE;
    // }

	// /* We use this timestamp to decide how many more samples to send. */
	// devc->starttime = g_get_monotonic_time();

	// return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi, void *cb_data)
{
    return dev_acquisition_stop_file(sdi,cb_data);

    // (void)cb_data;

    // struct demo_context *const devc = sdi->priv;
    // struct sr_datafeed_packet packet;
    // if (devc->stop)
    //     return SR_OK;

	// sr_dbg("Stopping acquisition.");

    // devc->stop = TRUE;

    // sr_session_source_remove_channel(devc->channel);

    // g_free(devc->buf);

	// /* Send last packet. */
    // packet.type = SR_DF_END;
    // packet.status = SR_PKT_OK;
    // ds_data_forward(sdi, &packet);

	// return SR_OK;
}

static int hw_dev_status_get(const struct sr_dev_inst *sdi, struct sr_status *status, gboolean prg)
{
    return dev_status_get(sdi,status,prg);

    // (void)prg;

    // if (sdi) {
    //     struct demo_context *const devc = sdi->priv;
    //     *status = devc->mstatus;
    //     return SR_OK;
    // } else {
    //     return SR_ERR;
    // }
}

SR_PRIV struct sr_dev_driver demo_driver_info = {
    .name = "virtual-demo",
	.longname = "Demo driver and pattern generator",
	.api_version = 1,
    .driver_type = DRIVER_TYPE_DEMO,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan, 
    .dev_mode_list = hw_dev_mode_list,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
    .dev_destroy = dev_destroy,
    .dev_status_get = hw_dev_status_get,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
