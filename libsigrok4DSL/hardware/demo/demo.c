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
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#endif

#include "../../log.h"

extern char DS_USR_PATH[500];

static uint64_t samplerates_file[1];
static uint64_t samplecounts_file[1];
static GTimer* packet_interval = NULL;
static GTimer* run_time = NULL;
static int max_probe_num = 0;
static uint64_t packet_num;
static void* logic_post_buf = NULL;

/* Message logging helpers with subsystem-specific prefix string. */

#undef LOG_PREFIX
#define LOG_PREFIX "demo: "

/* The size of chunks to send through the session bus. */
/* TODO: Should be configurable. */
#define BUFSIZE 512 * 1024
#define DSO_BUFSIZE 10 * 1024

#define PATTERN_COUNT 20
#define RANDOM_NAME "random"

struct demo_mode_pattern {
    char* patterns[PATTERN_COUNT + 1];
    int count;
};

static struct demo_mode_pattern demo_pattern_array[3];
static int b_load_directory = 0;
static char* demo_mode_names[3] = { "logic", "dso", "analog" };

static const struct DEMO_channels logic_channel_modes[] = {
    { (enum DEMO_CHANNEL_ID)DEMO_LOGIC125x16, LOGIC, SR_CHANNEL_LOGIC, 16, 1, SR_MHZ(1), SR_Mn(1),
        SR_KHZ(50), SR_MHZ(125), "Use 16 Channels (Max 125MHz)" },
    { (enum DEMO_CHANNEL_ID)DEMO_LOGIC250x12, LOGIC, SR_CHANNEL_LOGIC, 12, 1, SR_MHZ(1), SR_Mn(1),
        SR_KHZ(50), SR_MHZ(250), "Use 12 Channels (Max 250MHz)" },
    { (enum DEMO_CHANNEL_ID)DEMO_LOGIC500x6, LOGIC, SR_CHANNEL_LOGIC, 6, 1, SR_MHZ(1), SR_Mn(1),
        SR_KHZ(50), SR_MHZ(500), "Use 6 Channels (Max 500MHz)" },
    { (enum DEMO_CHANNEL_ID)DEMO_LOGIC1000x3, LOGIC, SR_CHANNEL_LOGIC, 3, 1, SR_MHZ(1), SR_Mn(1),
        SR_KHZ(50), SR_GHZ(1), "Use 3 Channels (Max 1GHz)" },
};

static struct sr_list_item logic_channel_mode_list[ARRAY_SIZE(logic_channel_modes) + 1];

static const struct DEMO_channels channel_modes[] = {
    // LA Stream
    { DEMO_LOGIC100x16, LOGIC, SR_CHANNEL_LOGIC, 16, 1, SR_MHZ(1), SR_Mn(1),
        SR_KHZ(50), SR_GHZ(1), "Use 16 Channels (Max 20MHz)" },

    // DAQ
    { DEMO_ANALOG10x2, ANALOG, SR_CHANNEL_ANALOG, 2, 8, SR_MHZ(1), SR_Mn(1),
        SR_HZ(10), SR_MHZ(10), "Use Channels 0~1 (Max 10MHz)" },

    // OSC
    { DEMO_DSO200x2, DSO, SR_CHANNEL_DSO, 2, 8, SR_MHZ(100), SR_Kn(10),
        SR_HZ(100), SR_MHZ(200), "Use Channels 0~1 (Max 200MHz)" },
};

/* Private, per-device-instance driver context. */
/* TODO: struct context as with the other drivers. */

/* List of struct sr_dev_inst, maintained by dev_open()/dev_close(). */
SR_PRIV struct sr_dev_driver demo_driver_info;
static struct sr_dev_driver* di = &demo_driver_info;

extern struct ds_trigger* trigger;

static int get_pattern_mode_from_file(const char* sub_dir, struct demo_mode_pattern* info, int max_count);
static int get_pattern_mode_index_by_string(uint8_t device_mode, const char* pattern);
static const char* get_pattern_name(uint8_t device_mode, int index);

static int vdev_init(struct sr_dev_inst* sdi)
{
    struct session_vdev* vdev = sdi->priv;
    vdev->version = 0;
    vdev->archive = NULL;
    vdev->capfile = 0;

    vdev->samplerates_min_index = 0;
    vdev->samplerates_max_index = 0;

    vdev->data_buf = NULL;
    vdev->data_buf_len = 0;
    vdev->analog_read_pos = 0;

    vdev->cur_block = 0;
    vdev->num_blocks = 0;
    vdev->samplerate = 0;
    vdev->total_samples = 0;
    vdev->trig_time = 0;
    vdev->trig_pos = 0;
    vdev->num_probes = 0;
    vdev->enabled_probes = 0;
    vdev->timebase = SR_NS(500);
    vdev->max_timebase = MAX_TIMEBASE;
    vdev->min_timebase = MIN_TIMEBASE;
    vdev->ref_min = 1;
    vdev->ref_max = 255;
    vdev->max_height = 0;
    vdev->mstatus.measure_valid = TRUE;
    vdev->packet_buffer = NULL;

    vdev->packet_len = 0;
    vdev->packet_time = 0;
    vdev->instant = FALSE;
    vdev->load_data = FALSE;
    vdev->vdiv_change = FALSE;
    vdev->offset_change = FALSE;
    vdev->timebase_change = FALSE;
    vdev->channel_mode_change = FALSE;

    vdev->post_data_len = 0;

    memset(vdev->logic_sel_probe_list, 0, LOGIC_MAX_PROBE_NUM);
    vdev->logic_sel_probe_num = 0;
    vdev->logic_ch_mode = DEMO_LOGIC125x16;
    vdev->logic_ch_mode_index = LOGIC125x16;

    vdev->is_loop = FALSE;
    vdev->unit_bits = (sdi->mode == LOGIC) ? 1 : 8;

    return SR_OK;
}

static void dso_status_update(struct session_vdev* vdev)
{
    struct sr_status* status = (struct sr_status*)&vdev->mstatus;
    struct session_packet_buffer* pack_buffer = vdev->packet_buffer;

    uint8_t ch_max = DSO_MID_VAL;
    uint8_t ch_min = DSO_MID_VAL;
    uint32_t total_val = 0;

    gboolean temp_plevel;
    gboolean first_plevel = FALSE;
    uint32_t temp_llen = 0;

    for (int i = 0; i < DSO_PACKET_LEN; i += 2) {
        uint8_t val = *(uint8_t*)(pack_buffer->post_buf + i);
        if (val > ch_max) {
            ch_max = val;
        }
        if (val < ch_min) {
            ch_min = val;
        }
    }
    status->ch0_max = status->ch1_max = status->ch0_high_level = status->ch1_high_level = ch_max;
    status->ch0_min = status->ch1_min = status->ch0_low_level = status->ch1_low_level = ch_min;

    status->ch0_cyc_tlen = status->ch1_cyc_tlen = 0;
    status->ch0_cyc_llen = status->ch1_cyc_llen = 0;
    status->ch0_cyc_cnt = status->ch1_cyc_cnt = 0;
    status->ch0_cyc_rlen = status->ch1_cyc_rlen = 0;
    status->ch0_cyc_flen = status->ch1_cyc_flen = 0;
    status->ch0_cyc_plen = status->ch1_cyc_plen = 0;

    status->ch0_level_valid = status->ch1_level_valid = 1;
    status->ch0_acc_mean = status->ch1_acc_mean = 0;

    for (uint16_t i = 0; i < DSO_PACKET_LEN; i += 2) {
        if (i <= pack_buffer->post_len || vdev->instant) {
            uint8_t val = *(uint8_t*)(pack_buffer->post_buf + i);
            if (first_plevel) {
                if (val <= DSO_MID_VAL) {
                    status->ch0_cyc_plen++;
                    status->ch1_cyc_plen++;
                }
                temp_llen++;

                if (temp_plevel) {
                    status->ch0_cyc_rlen++;
                    status->ch1_cyc_rlen++;
                    if (val == ch_max) {
                        temp_plevel = !temp_plevel;
                        if (status->ch0_plevel == temp_plevel) {
                            status->ch0_cyc_cnt++;
                            status->ch1_cyc_cnt++;
                            status->ch0_cyc_tlen = status->ch1_cyc_tlen += temp_llen;
                            status->ch0_cyc_llen = status->ch1_cyc_llen = 0;
                            temp_llen = 0;
                        } else {
                            status->ch0_cyc_llen = status->ch1_cyc_llen = temp_llen;
                        }
                    }
                } else {
                    status->ch0_cyc_flen++;
                    status->ch1_cyc_flen++;
                    if (val == ch_min) {
                        temp_plevel = !temp_plevel;
                        if (status->ch0_plevel == temp_plevel) {
                            status->ch0_cyc_cnt++;
                            status->ch1_cyc_cnt++;
                            status->ch0_cyc_tlen = status->ch1_cyc_tlen += temp_llen;
                            status->ch0_cyc_llen = status->ch1_cyc_llen = 0;
                            temp_llen = 0;
                        } else {
                            status->ch0_cyc_llen = status->ch1_cyc_llen = temp_llen;
                        }
                    }
                }
            } else {
                if (val == ch_max || val == ch_min) {
                    if (val == ch_max) {
                        temp_plevel = status->ch0_plevel = status->ch1_plevel = FALSE;
                    } else {
                        temp_plevel = status->ch0_plevel = status->ch1_plevel = TRUE;
                    }
                    first_plevel = TRUE;
                }
            }
            total_val += val;
        } else
            break;
    }
    status->ch0_cyc_rlen = status->ch1_cyc_rlen /= 2;
    status->ch0_cyc_flen = status->ch1_cyc_flen /= 2;

    if (vdev->sample_generator != PATTERN_RANDOM) {
        status->ch0_acc_mean = status->ch1_acc_mean = DSO_MID_VAL * pack_buffer->post_len / 2;
    } else {
        status->ch0_acc_mean = status->ch1_acc_mean = total_val;
    }
    status->ch0_acc_square = status->ch1_acc_square = ch_max * pack_buffer->post_len / 2 * 7;
}

static void logic_adjust_samplerate(struct session_vdev* vdev)
{
    vdev->samplerates_max_index = ARRAY_SIZE(samplerates) - 1;
    while (samplerates[vdev->samplerates_max_index] > logic_channel_modes[vdev->logic_ch_mode_index].max_samplerate) {
        vdev->samplerates_max_index--;
    }

    vdev->samplerates_min_index = 0;
    while (samplerates[vdev->samplerates_min_index] < logic_channel_modes[vdev->logic_ch_mode_index].min_samplerate) {
        vdev->samplerates_min_index++;
    }

    assert(vdev->samplerates_max_index >= vdev->samplerates_min_index);

    if (vdev->samplerate > samplerates[vdev->samplerates_max_index]) {
        vdev->samplerate = samplerates[vdev->samplerates_max_index];
    }

    if (vdev->samplerate < samplerates[vdev->samplerates_min_index]) {
        vdev->samplerate = samplerates[vdev->samplerates_min_index];
    }
}

static int init_analog_random_data(struct session_vdev* vdev)
{
    safe_free(vdev->data_buf);

    vdev->data_buf = g_try_malloc0(DSO_BUF_LEN);
    if (vdev->data_buf == NULL) {
        sr_err("%s: vdev->data_buf malloc failed", __func__);
        return SR_ERR_MALLOC;
    }

    for (uint64_t i = 0; i < DSO_BUF_LEN; i++) {
        if (i % 2 == 0) {
            *(uint8_t*)(vdev->data_buf + i) = ANALOG_RANDOM_DATA;
        } else {
            *(uint8_t*)(vdev->data_buf + i) = *(uint8_t*)(vdev->data_buf + i - 1);
        }
    }

    vdev->data_buf_len = DSO_BUF_LEN;
    return SR_OK;
}

static int delay_time(struct session_vdev* vdev)
{
    gdouble packet_elapsed = g_timer_elapsed(packet_interval, NULL);
    gdouble waittime = vdev->packet_time - packet_elapsed;
    if (waittime > 0) {
        g_usleep(SR_MS(waittime));
    }
    return SR_OK;
}

static int logic_delay_time(struct session_vdev* vdev)
{
    gdouble ideal_time = vdev->samplerate / 8;
    ideal_time = vdev->packet_len / (gdouble)ideal_time;
    ideal_time = vdev->logci_cur_packet_num * ideal_time;
    gdouble packet_elapsed = g_timer_elapsed(run_time, NULL);
    gdouble waittime = ideal_time - packet_elapsed;
    if (waittime > 0) {
        g_usleep(SR_MS(waittime));
    }
    return SR_OK;
}

static int instant_delay_time(struct session_vdev* vdev)
{
    uint16_t cur_packet_num = vdev->post_data_len / vdev->packet_len;
    gdouble ideal_time = cur_packet_num * vdev->packet_time;
    gdouble totol_time = g_timer_elapsed(run_time, NULL);
    gdouble waittime = ideal_time - totol_time;
    if (waittime > 0) {
        g_usleep(SR_MS(waittime));
    }
    return SR_OK;
}

static void get_last_packet_len(struct sr_datafeed_logic* logic, struct session_vdev* vdev)
{
    assert(vdev);
    int last_packet_len = vdev->post_data_len - (logic->length / vdev->enabled_probes);
    last_packet_len = (vdev->total_samples / 8) - last_packet_len;
    logic->length = last_packet_len * vdev->enabled_probes;
    vdev->post_data_len = vdev->total_samples / 8;
}

static int get_pattern_mode_index_by_string(uint8_t device_mode, const char* pattern)
{
    int dex = 0;

    struct demo_mode_pattern* info = &demo_pattern_array[device_mode];

    while (dex < info->count) {
        if (strcmp(info->patterns[dex], pattern) == 0) {
            return dex;
        }
        dex++;
    }

    return -1;
}

static const char* get_pattern_name(uint8_t device_mode, int index)
{
    struct demo_mode_pattern* info = &demo_pattern_array[device_mode];
    if (index >= 0 && index < info->count) {
        return info->patterns[index];
    }

    assert(0);
    return NULL;
}

static int get_pattern_mode_from_file(const char* sub_dir, struct demo_mode_pattern* info, int max_count)
{
    const gchar* filename = NULL;
    char dir_path_buf[500];
    int str_len;
    char* dir_path = dir_path_buf;
    const char* file_path = NULL;
    char short_name[50];
    int i;
    int num;

    assert(sub_dir);
    assert(info);

    for (i = 0; i < max_count; i++) {
        info->patterns[i] = NULL;
    }
    info->patterns[max_count] = NULL;

    info->patterns[0] = RANDOM_NAME;
    num = 1;
    info->count = num;

    strcpy(dir_path, DS_USR_PATH);
    strcat(dir_path, "/demo/");
    strcat(dir_path, sub_dir);

    GDir* dir = NULL;
    dir = g_dir_open(dir_path, 0, NULL);
    if (dir == NULL) {
        sr_err("Faild to open dir:%s", dir_path);
        return SR_ERR;
    }

    while ((filename = g_dir_read_name(dir)) != NULL) {
        if (FALSE == g_file_test(filename, G_FILE_TEST_IS_DIR)) {
            file_path = filename;

            if (strstr(file_path, ".demo") != NULL) {
                str_len = strlen(file_path) - 5;
                strncpy(short_name, file_path, sizeof(short_name) - 1);
                short_name[str_len] = 0;
                info->patterns[num++] = g_strdup(short_name);

                if (num >= max_count) {
                    break;
                }
            }
        }
    }
    g_dir_close(dir);

    info->count = num;

    return SR_OK;
}

static void scan_dsl_file(struct sr_dev_inst* sdi)
{
    struct session_vdev* vdev = sdi->priv;
    int dex;

    if (b_load_directory == 0) {
        get_pattern_mode_from_file(demo_mode_names[LOGIC], &demo_pattern_array[LOGIC], PATTERN_COUNT);
        get_pattern_mode_from_file(demo_mode_names[DSO], &demo_pattern_array[DSO], PATTERN_COUNT);
        get_pattern_mode_from_file(demo_mode_names[ANALOG], &demo_pattern_array[ANALOG], PATTERN_COUNT);
        b_load_directory = 1;
    }

    dex = get_pattern_mode_index_by_string(LOGIC, DEFAULT_LOGIC_FILE);

    if (dex == -1) {
        dex = PATTERN_RANDOM;
    }

    vdev->sample_generator = dex;
    sdi->mode = LOGIC;
    reset_dsl_path(sdi, dex);
}

static int reset_dsl_path(struct sr_dev_inst* sdi, uint8_t pattern_mode)
{
    struct demo_mode_pattern* info = NULL;
    char file_path[500];

    safe_free(sdi->path);

    strcpy(file_path, DS_USR_PATH);
    strcat(file_path, "/demo/");

    if (pattern_mode != PATTERN_RANDOM) {
        info = &demo_pattern_array[sdi->mode];
        assert(pattern_mode < info->count);

        strcat(file_path, demo_mode_names[sdi->mode]);
        strcat(file_path, "/");
        strcat(file_path, info->patterns[pattern_mode]);
        strcat(file_path, ".demo");

        sdi->path = g_strdup(file_path);
    } else {
        sdi->path = g_strdup("");
    }

    return SR_OK;
}

static void adjust_samplerate(struct sr_dev_inst* sdi)
{
    struct session_vdev* vdev = sdi->priv;
    if (sdi->mode == LOGIC && vdev->sample_generator > PATTERN_RANDOM) {
        return;
    }

    int cur_mode = -1;
    if (sdi->mode == LOGIC) {
        cur_mode = 0;
    } else if (sdi->mode == DSO) {
        cur_mode = 2;
    } else if (sdi->mode == ANALOG) {
        cur_mode = 1;
    }

    if (cur_mode == -1) {
        return;
    }

    vdev->samplerates_max_index = ARRAY_SIZE(samplerates) - 1;

    while (samplerates[vdev->samplerates_max_index] > channel_modes[cur_mode].max_samplerate) {
        vdev->samplerates_max_index--;
    }

    vdev->samplerates_min_index = 0;
    while (samplerates[vdev->samplerates_min_index] < channel_modes[cur_mode].min_samplerate) {
        vdev->samplerates_min_index++;
    }

    assert(vdev->samplerates_max_index >= vdev->samplerates_min_index);

    if (vdev->samplerate > samplerates[vdev->samplerates_max_index]) {
        vdev->samplerate = samplerates[vdev->samplerates_max_index];
    }

    if (vdev->samplerate < samplerates[vdev->samplerates_min_index]) {
        vdev->samplerate = samplerates[vdev->samplerates_min_index];
    }
}

static int init_random_data(struct session_vdev* vdev)
{
    int cur_probe = 0;
    int probe_count[LOGIC_MAX_PROBE_NUM] = { 0 };
    uint8_t probe_status[LOGIC_MAX_PROBE_NUM] = { LOGIC_HIGH_LEVEL };
    uint64_t i;
    static int rand_k = 0;

    assert(vdev->data_buf);

    memset(probe_status, LOGIC_HIGH_LEVEL, 16);
    memset(vdev->data_buf, 0, LOGIC_BUF_LEN);

    srand((unsigned int)(time(NULL) + rand() + (rand_k++)));

    for (int i = 0; i < vdev->enabled_probes; i++) {
        probe_count[i] = rand() % SR_KB(1);
    }

    for (i = 0; i < vdev->data_buf_len; i++) {
        if (i % 8 == 0 && i != 0) {
            cur_probe++;
            if (cur_probe > vdev->enabled_probes - 1) {
                cur_probe = 0;
            }
        }

        if (probe_count[cur_probe] > 0) {
            memset(vdev->data_buf + i, probe_status[cur_probe], 1);
            probe_count[cur_probe] -= 1;
        } else {
            if (probe_status[cur_probe] == LOGIC_HIGH_LEVEL) {
                probe_status[cur_probe] = LOGIC_LOW_LEVEL;
            } else {
                probe_status[cur_probe] = LOGIC_HIGH_LEVEL;
            }
            probe_count[cur_probe] = rand() % SR_KB(1);
            memset(vdev->data_buf + i, probe_status[cur_probe], 1);
            probe_count[cur_probe] -= 1;
        }
    }
    return SR_OK;
}

static int hw_init(struct sr_context* sr_ctx)
{
    return std_hw_init(sr_ctx, di, LOG_PREFIX);
}

static int hw_clean_up()
{
    return SR_OK;
}

static GSList* hw_scan(GSList* options)
{
    struct sr_dev_inst* sdi;
    struct session_vdev* vdev;
    GSList* devices;

    (void)options;
    devices = NULL;

    vdev = g_try_malloc0(sizeof(struct session_vdev));
    if (vdev == NULL) {
        sr_err("%s: sdi->priv malloc failed", __func__);
        return devices;
    }
    memset(vdev, 0, sizeof(struct session_vdev));

    sdi = sr_dev_inst_new(LOGIC, SR_ST_INACTIVE,
        supported_Demo[0].vendor,
        supported_Demo[0].model,
        supported_Demo[0].model_version);
    if (!sdi) {
        safe_free(vdev);
        sr_err("Device instance creation failed.");
        return NULL;
    }

    sdi->priv = vdev;
    sdi->driver = di;
    sdi->dev_type = DEV_TYPE_DEMO;

    vdev->is_loop = 0;

    devices = g_slist_append(devices, sdi);

    return devices;
}

static const GSList* hw_dev_mode_list(const struct sr_dev_inst* sdi)
{
    (void)sdi;

    GSList* l = NULL;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(sr_mode_list); i++) {
        if (supported_Demo[0].dev_caps.mode_caps & (1 << i)) {
            l = g_slist_append(l, (gpointer)&sr_mode_list[i]);
        }
    }

    return l;
}

static int hw_dev_open(struct sr_dev_inst* sdi)
{
    int ret;
    assert(sdi);
    assert(sdi->priv);

    if (sdi->status == SR_ST_ACTIVE) {
        return SR_OK;
    }

    scan_dsl_file(sdi);

    ret = vdev_init(sdi);
    if (ret == SR_OK) {
        sdi->status = SR_ST_ACTIVE;
    }

    packet_interval = g_timer_new();
    run_time = g_timer_new();

    ret = load_virtual_device_session(sdi);
    if (ret != SR_OK) {
        sr_err("Error!Load session file failed.");
        return ret;
    }

    return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst* sdi)
{
    struct session_vdev* vdev;
    int i;
    struct session_packet_buffer* pack_buf;

    if (sdi && sdi->priv) {
        vdev = sdi->priv;

        if (vdev->packet_buffer != NULL) {
            pack_buf = vdev->packet_buffer;

            safe_free(pack_buf->post_buf);

            for (i = 0; i < SESSION_MAX_CHANNEL_COUNT; i++) {
                if (pack_buf->block_bufs[i] != NULL) {
                    safe_free(pack_buf->block_bufs[i]);
                    pack_buf->block_bufs[i] = NULL;
                } else {
                    break;
                }
            }
        }

        safe_free(vdev->packet_buffer);
        safe_free(vdev->data_buf);
        safe_free(vdev->data_buf);
        safe_free(sdi->path);
        safe_free(packet_interval);
        safe_free(run_time);
        safe_free(vdev->analog_post_buf);

        sdi->status = SR_ST_INACTIVE;
        return SR_OK;
    }
    return SR_ERR_CALL_STATUS;
}

static int dev_destroy(struct sr_dev_inst* sdi)
{
    assert(sdi);
    hw_dev_close(sdi);
    sr_dev_inst_free(sdi);

    return SR_OK;
}

static int config_get(int id, GVariant** data, const struct sr_dev_inst* sdi,
    const struct sr_channel* ch,
    const struct sr_channel_group* cg)
{
    (void)cg;

    const char* patter_name = NULL;

    assert(sdi);
    assert(sdi->priv);

    struct session_vdev* vdev = sdi->priv;

    switch (id) {
    case SR_CONF_SAMPLERATE:
        *data = g_variant_new_uint64(vdev->samplerate);
        break;
    case SR_CONF_LIMIT_SAMPLES:
        *data = g_variant_new_uint64(vdev->total_samples);
        break;
    case SR_CONF_DEVICE_MODE:
        *data = g_variant_new_int16(sdi->mode);
        break;
    case SR_CONF_INSTANT:
        *data = g_variant_new_boolean(vdev->instant);
        break;
    case SR_CONF_PATTERN_MODE:
        patter_name = get_pattern_name(sdi->mode, vdev->sample_generator);
        *data = g_variant_new_string(patter_name);
        break;
    case SR_CONF_MAX_HEIGHT:
        *data = g_variant_new_string(maxHeights[vdev->max_height]);
        break;
    case SR_CONF_MAX_HEIGHT_VALUE:
        *data = g_variant_new_byte(vdev->max_height);
        break;
    case SR_CONF_PROBE_OFFSET:
        if (ch) {
            *data = g_variant_new_uint16(ch->offset);
        }
        break;
    case SR_CONF_PROBE_HW_OFFSET:
        if (ch) {
            *data = g_variant_new_uint16(ch->hw_offset);
        }
        break;
    case SR_CONF_PROBE_VDIV:
        if (ch) {
            *data = g_variant_new_uint64(ch->vdiv);
        }
        break;
    case SR_CONF_PROBE_FACTOR:
        if (ch) {
            *data = g_variant_new_uint64(ch->vfactor);
        }
        break;
    case SR_CONF_TIMEBASE:
        *data = g_variant_new_uint64(vdev->timebase);
        break;
    case SR_CONF_MAX_TIMEBASE:
        *data = g_variant_new_uint64(MAX_TIMEBASE);
        break;
    case SR_CONF_MIN_TIMEBASE:
        *data = g_variant_new_uint64(MIN_TIMEBASE);
        break;
    case SR_CONF_PROBE_COUPLING:
        if (ch)
            *data = g_variant_new_byte(ch->coupling);
        break;
    case SR_CONF_TRIGGER_VALUE:
        if (ch) {
            *data = g_variant_new_byte(ch->trig_value);
        }
        break;
    case SR_CONF_PROBE_EN:
        if (ch) {
            *data = g_variant_new_boolean(ch->enabled);
        }
        break;
    case SR_CONF_MAX_DSO_SAMPLERATE:
        *data = g_variant_new_uint64(SR_MHZ(200));
        break;
    case SR_CONF_MAX_DSO_SAMPLELIMITS:
        *data = g_variant_new_uint64(SR_Kn(20));
        break;
    case SR_CONF_HW_DEPTH:
        switch (sdi->mode) {
        case LOGIC:
            if (vdev->sample_generator == PATTERN_RANDOM) {
                *data = g_variant_new_uint64(LOGIC_HW_DEPTH);
            } else {
                *data = g_variant_new_uint64(vdev->total_samples);
            }
            break;
        case DSO:
            *data = g_variant_new_uint64(vdev->total_samples);
            break;
        case ANALOG:
            *data = g_variant_new_uint64(ANALOG_HW_DEPTH);
            break;
        default:
            break;
        }
        break;
    case SR_CONF_UNIT_BITS:
        *data = g_variant_new_byte(vdev->unit_bits);
        break;
    case SR_CONF_PROBE_MAP_DEFAULT:
        if (!sdi || !ch) {
            return SR_ERR;
        }
        *data = g_variant_new_boolean(ch->map_default);
        break;
    case SR_CONF_PROBE_MAP_UNIT:
        if (!sdi || !ch) {
            return SR_ERR;
        }

        *data = g_variant_new_string(ch->map_unit);
        break;
    case SR_CONF_PROBE_MAP_MIN:
        if (!sdi || !ch) {
            return SR_ERR;
        }
        *data = g_variant_new_double(ch->map_min);
        break;
    case SR_CONF_PROBE_MAP_MAX:
        if (!sdi || !ch) {
            return SR_ERR;
        }
        *data = g_variant_new_double(ch->map_max);
        break;
    case SR_CONF_VLD_CH_NUM:
        *data = g_variant_new_int16(vdev->num_probes);
        break;
    case SR_CONF_HAVE_ZERO:
        *data = g_variant_new_boolean(FALSE);
        break;
    case SR_CONF_LOAD_DECODER:
        *data = g_variant_new_boolean(vdev->sample_generator != PATTERN_RANDOM);
        break;
    case SR_CONF_REF_MIN:
        *data = g_variant_new_uint32(vdev->ref_min);
        break;
    case SR_CONF_REF_MAX:
        *data = g_variant_new_uint32(vdev->ref_max);
        break;
    case SR_CONF_CHANNEL_MODE:
        *data = g_variant_new_int16(vdev->logic_ch_mode);
        break;
    case SR_CONF_HORIZ_TRIGGERPOS:
        *data = g_variant_new_byte(vdev->trigger_hrate);
        break;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

static int config_set(int id, GVariant* data, struct sr_dev_inst* sdi,
    struct sr_channel* ch,
    struct sr_channel_group* cg)
{
    (void)cg;

    struct session_vdev* vdev;
    const char* stropt;
    unsigned int i;
    int nv;
    uint8_t tmp_sample_generator;

    assert(sdi);
    assert(sdi->priv);

    vdev = sdi->priv;

    switch (id) {
    case SR_CONF_SAMPLERATE:
        vdev->samplerate = g_variant_get_uint64(data);
        sr_dbg("Setting samplerate to %llu.", (u64_t)vdev->samplerate);
        break;
    case SR_CONF_LIMIT_SAMPLES:
        vdev->total_samples = g_variant_get_uint64(data);
        sr_dbg("Setting limit samples to %llu.", (u64_t)vdev->total_samples);
        break;
    case SR_CONF_LIMIT_MSEC:
        break;
    case SR_CONF_DEVICE_MODE:
        sdi->mode = g_variant_get_int16(data);
        nv = get_pattern_mode_index_by_string(sdi->mode, (sdi->mode == LOGIC ? DEFAULT_LOGIC_FILE : sdi->mode == DSO ? DEFAULT_DSO_FILE
                                                                                                                     : DEFAULT_ANALOG_FILE));
        if (nv != -1) {
            vdev->sample_generator = nv;
        } else {
            vdev->sample_generator = PATTERN_RANDOM;
        }
        reset_dsl_path(sdi, vdev->sample_generator);
        load_virtual_device_session(sdi);
        break;
    case SR_CONF_PATTERN_MODE:
        stropt = g_variant_get_string(data, NULL);
        tmp_sample_generator = vdev->sample_generator;
        nv = get_pattern_mode_index_by_string(sdi->mode, stropt);
        if (nv == -1) {
            vdev->sample_generator = PATTERN_RANDOM;
        } else {
            vdev->sample_generator = nv;
        }
        reset_dsl_path(sdi, vdev->sample_generator);
        if (sdi->mode == LOGIC) {
            if (vdev->sample_generator == PATTERN_RANDOM) {
                if (!vdev->channel_mode_change && tmp_sample_generator != vdev->sample_generator) {
                    vdev->logic_ch_mode = DEMO_LOGIC125x16;
                    vdev->logic_ch_mode_index = LOGIC125x16;
                    load_virtual_device_session(sdi);
                }
                vdev->channel_mode_change = FALSE;
            } else {
                load_virtual_device_session(sdi);
            }
        }

        sr_dbg("%s: setting pattern to %d",
            __func__, vdev->sample_generator);
        break;

    case SR_CONF_MAX_HEIGHT:
        stropt = g_variant_get_string(data, NULL);
        for (i = 0; i < ARRAY_SIZE(maxHeights); i++) {
            if (!strcmp(stropt, maxHeights[i])) {
                vdev->max_height = i;
                break;
            }
        }
        sr_dbg("%s: setting Signal Max Height to %d",
            __func__, vdev->max_height);
        break;
    case SR_CONF_PROBE_EN:
        ch->enabled = g_variant_get_boolean(data);
        break;
    case SR_CONF_PROBE_VDIV:
        ch->vdiv = g_variant_get_uint64(data);
        if (sdi->mode == DSO) {
            if (vdev->packet_buffer) {
                vdev->packet_buffer->post_len = 0;
                vdev->packet_buffer->block_read_positions[0] = 0;
                vdev->packet_buffer->block_read_positions[1] = 0;
                vdev->packet_buffer->block_chan_read_pos = 0;
            }
            vdev->vdiv_change = TRUE;
        }
        break;
    case SR_CONF_PROBE_FACTOR:
        ch->vfactor = g_variant_get_uint64(data);
        break;
    case SR_CONF_PROBE_OFFSET:
        ch->offset = g_variant_get_uint16(data);
        if (sdi->mode == DSO && ch->vdiv <= SR_mV(200)) {
            ch->hw_offset = ch->offset;
            vdev->offset_change = TRUE;
        } else {
            if (ch->coupling == 0) {
                ch->hw_offset = ANALOG_DC_COUL_OFFSET;
            } else {
                ch->hw_offset = ANALOG_AC_COUL_OFFSET;
            }
        }
        break;
    case SR_CONF_PROBE_HW_OFFSET:
        ch->hw_offset = g_variant_get_uint16(data);
        break;
    case SR_CONF_TIMEBASE:
        vdev->timebase = g_variant_get_uint64(data);
        if (sdi->mode == DSO) {
            g_timer_start(run_time);
            vdev->timebase_change = TRUE;
        }
        sr_dbg("Setting timebase to %llu.", (u64_t)vdev->timebase);
        break;
    case SR_CONF_PROBE_COUPLING:
        if (sdi->mode != LOGIC) {
            if (sdi->mode == DSO && ch->vdiv <= SR_mV(200)) {
                ch->coupling = g_variant_get_byte(data);
                ch->hw_offset = ch->offset;
            } else {
                ch->coupling = g_variant_get_byte(data);
                if (ch->coupling == 0) {
                    ch->hw_offset = ANALOG_DC_COUL_OFFSET;
                } else {
                    ch->hw_offset = ANALOG_AC_COUL_OFFSET;
                }
            }
        }
        break;
    case SR_CONF_TRIGGER_SOURCE:
        break;
    case SR_CONF_TRIGGER_SLOPE:
        break;
    case SR_CONF_TRIGGER_VALUE:
        ch->trig_value = g_variant_get_byte(data);
        break;
    case SR_CONF_PROBE_MAP_DEFAULT:
        ch->map_default = g_variant_get_boolean(data);
        if (ch->map_default) {
            ch->map_unit = probeMapUnits[0];
            ch->map_min = -(ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0);
            ch->map_max = ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0;
        }
        break;
    case SR_CONF_PROBE_MAP_UNIT:
        ch->map_unit = g_variant_get_string(data, NULL);
        break;
    case SR_CONF_PROBE_MAP_MIN:
        ch->map_min = g_variant_get_double(data);
        break;
    case SR_CONF_PROBE_MAP_MAX:
        ch->map_max = g_variant_get_double(data);
        break;
    case SR_CONF_NUM_BLOCKS:
        vdev->num_blocks = g_variant_get_uint64(data);
        sr_dbg("Setting block number to %d.", vdev->num_blocks);
        break;
    case SR_CONF_CAPTURE_NUM_PROBES:
        vdev->num_probes = g_variant_get_uint64(data);
        break;
    case SR_CONF_INSTANT:
        vdev->instant = g_variant_get_boolean(data);
        break;
    case SR_CONF_LOOP_MODE:
        vdev->is_loop = g_variant_get_boolean(data);
        sr_info("Set demo loop mode:%d", vdev->is_loop);
        break;
    case SR_CONF_CHANNEL_MODE:
        nv = g_variant_get_int16(data);
        if (sdi->mode == LOGIC && vdev->sample_generator == PATTERN_RANDOM) {
            for (i = 0; i < ARRAY_SIZE(logic_channel_modes); i++) {
                if (logic_channel_modes[i].id == (enum DEMO_CHANNEL_ID)nv) {
                    vdev->logic_ch_mode_index = i;
                    vdev->logic_ch_mode = (enum DEMO_CHANNEL_ID)nv;
                    load_virtual_device_session(sdi);
                    vdev->channel_mode_change = TRUE;
                    break;
                }
            }
        }
        break;
    case SR_CONF_HORIZ_TRIGGERPOS:
        vdev->trigger_hrate = g_variant_get_byte(data);
        break;
    default:
        sr_err("Unknown capability: %d.", id);
        return SR_ERR_NA;
    }

    return SR_OK;
}

static int config_list(int key, GVariant** data, const struct sr_dev_inst* sdi,
    const struct sr_channel_group* cg)
{
    (void)cg;

    GVariant* gvar;
    GVariantBuilder gvb;
    struct demo_mode_pattern* info = NULL;
    unsigned int i;

    (void)sdi;
    struct session_vdev* vdev = sdi->priv;

    switch (key) {
    case SR_CONF_DEVICE_OPTIONS:
        *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
            hwoptions, ARRAY_SIZE(hwoptions) * sizeof(int32_t), TRUE, NULL, NULL);
        break;
    case SR_CONF_DEVICE_SESSIONS:
        *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
            sessions, ARRAY_SIZE(sessions) * sizeof(int32_t), TRUE, NULL, NULL);
        break;
    case SR_CONF_SAMPLERATE:
        g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
        if (sdi->mode == LOGIC && vdev->sample_generator != PATTERN_RANDOM) {
            gvar = g_variant_new_from_data(G_VARIANT_TYPE("at"),
                samplerates_file, ARRAY_SIZE(samplerates_file) * sizeof(uint64_t), TRUE, NULL, NULL);
        } else {
            gvar = g_variant_new_from_data(G_VARIANT_TYPE("at"),
                samplerates + vdev->samplerates_min_index, (vdev->samplerates_max_index - vdev->samplerates_min_index + 1) * sizeof(uint64_t), TRUE, NULL, NULL);
        }
        g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
        *data = g_variant_builder_end(&gvb);
        break;
    case SR_CONF_PATTERN_MODE:
        info = &demo_pattern_array[sdi->mode];
        *data = g_variant_new_strv((const char* const*)info->patterns, info->count);
        break;
    case SR_CONF_MAX_HEIGHT:
        *data = g_variant_new_strv(maxHeights, ARRAY_SIZE(maxHeights));
        break;
    case SR_CONF_PROBE_CONFIGS:
        *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
            probeOptions, ARRAY_SIZE(probeOptions) * sizeof(int32_t), TRUE, NULL, NULL);
        break;
    case SR_CONF_PROBE_VDIV:
        g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
        gvar = g_variant_new_from_data(G_VARIANT_TYPE("at"),
            vdivs10to2000, (ARRAY_SIZE(vdivs10to2000) - 1) * sizeof(uint64_t), TRUE, NULL, NULL);
        g_variant_builder_add(&gvb, "{sv}", "vdivs", gvar);
        *data = g_variant_builder_end(&gvb);
        break;
    case SR_CONF_PROBE_COUPLING:
        g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
        gvar = g_variant_new_from_data(G_VARIANT_TYPE("ay"),
            probeCoupling, ARRAY_SIZE(probeCoupling) * sizeof(uint8_t), TRUE, NULL, NULL);
        g_variant_builder_add(&gvb, "{sv}", "coupling", gvar);
        *data = g_variant_builder_end(&gvb);
        break;
    case SR_CONF_PROBE_MAP_UNIT:
        *data = g_variant_new_strv(probeMapUnits, ARRAY_SIZE(probeMapUnits));
        break;
    case SR_CONF_CHANNEL_MODE:
        if (sdi->mode == LOGIC && vdev->sample_generator == PATTERN_RANDOM) {
            for (i = 0; i < ARRAY_SIZE(logic_channel_modes); i++) {
                logic_channel_mode_list[i].id = (int)logic_channel_modes[i].id;
                logic_channel_mode_list[i].name = logic_channel_modes[i].descr;
            }
            logic_channel_mode_list[i].id = -1;
            logic_channel_mode_list[i].name = NULL;
            *data = g_variant_new_uint64((uint64_t)&logic_channel_mode_list);
        } else {
            return SR_ERR_ARG;
        }
        break;
    default:
        return SR_ERR_ARG;
    }

    return SR_OK;
}

static int hw_dev_acquisition_start(struct sr_dev_inst* sdi,
    void* cb_data)
{

    (void)cb_data;

    struct session_vdev* vdev;
    GSList* l;
    struct sr_channel* probe;

    assert(sdi);
    assert(sdi->priv);

    vdev = sdi->priv;
    vdev->enabled_probes = 0;
    vdev->cur_block = 0;

    sr_info("mode:%d, generator:%d", sdi->mode, vdev->sample_generator);

    safe_free(vdev->data_buf);

    if (sdi->mode == LOGIC) {
        vdev->data_buf = g_try_malloc0(LOGIC_BUF_LEN);
        vdev->data_buf_len = LOGIC_BUF_LEN;
    } else {
        vdev->data_buf = g_try_malloc0(DSO_PACKET_LEN);
        vdev->data_buf_len = DSO_PACKET_LEN;
    }

    if (vdev->data_buf == NULL) {
        sr_err("%s: vdev->data_buf malloc failed", __func__);
        return SR_ERR_MALLOC;
    }

    if (vdev->sample_generator != PATTERN_RANDOM) {
        if (vdev->archive != NULL) {
            sr_err("history archive is not closed.");
        }

        sr_dbg("Opening archive file %s", sdi->path);

        vdev->archive = unzOpen64(sdi->path);

        if (NULL == vdev->archive) {
            sr_err("Failed to open session file '%s': "
                   "zip error",
                sdi->path);
            return SR_ERR;
        }
    }

    for (l = sdi->channels; l; l = l->next) {
        probe = l->data;
        if (probe->enabled)
            vdev->enabled_probes++;
    }

    if (sdi->mode == LOGIC) {
        vdev->post_data_len = 0;
        vdev->packet_len = LOGIC_PACKET_LEN(vdev->samplerate);
        vdev->packet_time = LOGIC_PACKET_TIME(LOGIC_PACKET_NUM_PER_SEC);
        if (vdev->packet_len < LOGIC_MIN_PACKET_LEN) {
            vdev->packet_len = LOGIC_MIN_PACKET_LEN;
            vdev->packet_time = LOGIC_MIN_PACKET_TIME(vdev->samplerate);
        }

        if (vdev->samplerate >= SR_MHZ(100)) {
            uint64_t ideal_len = 2;
            while (1) {
                if (ideal_len >= vdev->packet_len) {
                    vdev->packet_len = ideal_len;
                    break;
                } else {
                    ideal_len *= 2;
                }
            }

            vdev->packet_time = vdev->samplerate / 8;
            packet_num = ceil(vdev->packet_time / (gdouble)vdev->packet_len);
            vdev->packet_time = 1 / (gdouble)packet_num;
            vdev->logic_sel_probe_num = 0;
        }

        if (vdev->sample_generator == PATTERN_RANDOM) {
            vdev->logci_cur_packet_num = 1;
            safe_free(logic_post_buf);

            logic_post_buf = g_try_malloc0(vdev->enabled_probes * vdev->packet_len);
            if (logic_post_buf == NULL) {
                sr_err("%s: logic_post_buf malloc error", __func__);
                return SR_ERR_MALLOC;
            }

            vdev->logic_mem_limit = (((vdev->total_samples / 8) * vdev->enabled_probes) >= LOGIC_MEMORY_LIMIT) ? TRUE : FALSE;

            assert(run_time);

            init_random_data(vdev);
            g_timer_start(run_time);
            sr_session_source_add(-1, 0, 0, receive_data_logic, sdi);
        } else {
            sr_session_source_add(-1, 0, 0, receive_data_logic_decoder, sdi);
        }
    } else if (sdi->mode == DSO) {
        vdev->vdiv_change = TRUE;
        if (vdev->instant) {
            vdev->post_data_len = 0;

            gdouble total_time = vdev->timebase / (gdouble)SR_SEC(1) * (gdouble)10;
            uint64_t post_data_per_sec = DSO_PACKET_LEN / total_time;
            vdev->packet_len = 2;
            uint64_t packet_num = post_data_per_sec / vdev->packet_len;
            vdev->packet_time = SEC / (gdouble)packet_num;
        } else {
            vdev->packet_time = DSO_PACKET_TIME;
        }

        vdev->load_data = TRUE;
        g_timer_start(run_time);
        sr_session_source_add(-1, 0, 0, receive_data_dso, sdi);
    } else if (sdi->mode == ANALOG) {
        vdev->load_data = TRUE;
        vdev->packet_len = ANALOG_PACKET_LEN(vdev->samplerate);

        if (vdev->packet_len < ANALOG_MIN_PACKET_LEN) {
            vdev->packet_len = ANALOG_MIN_PACKET_LEN;
            vdev->packet_time = ANALOG_PACKET_TIME(ANALOG_MIN_PACKET_NUM(vdev->samplerate));
        } else {
            if (vdev->packet_len % ANALOG_PACKET_ALIGN != 0) {
                vdev->packet_len += 1;
            }
            vdev->packet_time = ANALOG_PACKET_TIME(ANALOG_PACKET_NUM_PER_SEC);
        }
        if (vdev->sample_generator == PATTERN_RANDOM) {
            init_analog_random_data(vdev);
        }

        vdev->analog_read_pos = 0;
        vdev->analog_post_buf_len = 0;

        sr_session_source_add(-1, 0, 0, receive_data_analog, sdi);
    }

    return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst* sdi, void* cb_data)
{

    (void)cb_data;

    struct session_vdev* vdev = sdi->priv;
    struct sr_datafeed_packet packet;
    packet.status = SR_PKT_OK;

    if (sdi->mode != LOGIC) {
        packet.type = SR_DF_END;
        ds_data_forward(sdi, &packet);
        close_archive(vdev);
    }

    return SR_OK;
}

static int hw_dev_status_get(const struct sr_dev_inst* sdi, struct sr_status* status, gboolean prg)
{
    (void)prg;

    struct session_vdev* vdev;
    if (sdi) {
        vdev = sdi->priv;
        *status = vdev->mstatus;
        return SR_OK;
    } else {
        return SR_ERR;
    }
}

static int receive_data_logic(int fd, int revents, const struct sr_dev_inst* sdi)
{
    assert(sdi);
    assert(sdi->priv);

    (void)fd;
    (void)revents;

    struct session_vdev* vdev = sdi->priv;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;

    int bToEnd;
    int chan_num;

    bToEnd = 0;

    vdev = sdi->priv;

    chan_num = vdev->enabled_probes;

    if (chan_num < 1) {
        sr_err("%s: channel count < 1.", __func__);
        return SR_ERR_ARG;
    }
    if (chan_num > SESSION_MAX_CHANNEL_COUNT) {
        sr_err("%s: channel count is to big.", __func__);
        return SR_ERR_ARG;
    }

    g_timer_start(packet_interval);

    if (!vdev->is_loop) {
        if (vdev->post_data_len >= vdev->total_samples / 8) {
            bToEnd = 1;
        }
    }

    if (!bToEnd) {
        packet.status = SR_PKT_OK;
        packet.type = SR_DF_LOGIC;
        packet.payload = &logic;
        logic.format = LA_CROSS_DATA;
        logic.index = 0;
        logic.order = 0;
        logic.length = chan_num * vdev->packet_len;

        if (!vdev->is_loop) {
            vdev->post_data_len += logic.length / vdev->enabled_probes;
            if (vdev->post_data_len >= vdev->total_samples / 8) {
                get_last_packet_len(&logic, vdev);
            }
        }

        memset(logic_post_buf, LOGIC_LOW_LEVEL, chan_num * vdev->packet_len);
        if (vdev->logic_mem_limit) {
            for (int i = 0; i < vdev->logic_sel_probe_num; i++) {
                uint8_t probe_index = vdev->logic_sel_probe_list[i];
                for (uint16_t j = 0; j < vdev->packet_len / 8; j++) {
                    uint64_t cur_index = (probe_index * 8) + (j * vdev->enabled_probes * 8);
                    memset(logic_post_buf + cur_index, LOGIC_HIGH_LEVEL, 8);
                }
            }
        } else {
            uint64_t random = vdev->data_buf_len - logic.length;
            random = rand() % random;
            int index = vdev->enabled_probes * 8;
            random = floor(random / index) * index;
            memcpy(logic_post_buf, vdev->data_buf + random, logic.length);
        }
        logic.data = logic_post_buf;

        logic_delay_time(vdev);
        ds_data_forward(sdi, &packet);

        if (vdev->logic_mem_limit) {
            uint16_t target_packet = (LOGIC_BLOCK_LEN / vdev->packet_len);
            if (vdev->logci_cur_packet_num % target_packet == 0) {
                vdev->logic_sel_probe_num = rand() % vdev->enabled_probes + 1;
                memset(vdev->logic_sel_probe_list, 0, vdev->enabled_probes);
                for (int i = 0; i < vdev->logic_sel_probe_num; i++) {
                    vdev->logic_sel_probe_list[i] = rand() % vdev->enabled_probes;
                }
            }
        }
        vdev->logci_cur_packet_num++;
    }

    if (bToEnd || revents == -1) {
        packet.type = SR_DF_END;
        ds_data_forward(sdi, &packet);
        sr_session_source_remove(-1);
    }

    return TRUE;
}

static void free_temp_buffer(struct session_vdev* vdev)
{
    struct session_packet_buffer* pack_buf;
    int i;

    assert(vdev);

    pack_buf = vdev->packet_buffer;

    if (pack_buf != NULL) {
        safe_free(pack_buf->post_buf);

        for (i = 0; i < SESSION_MAX_CHANNEL_COUNT; i++) {
            if (pack_buf->block_bufs[i] != NULL) {
                g_free(pack_buf->block_bufs[i]);
                pack_buf->block_bufs[i] = NULL;
            } else {
                break;
            }
        }
    }

    safe_free(vdev->packet_buffer);
    safe_free(vdev->data_buf);
    safe_free(vdev->analog_post_buf);
}

static int receive_data_logic_decoder(int fd, int revents, const struct sr_dev_inst* sdi)
{
    struct session_vdev* vdev = NULL;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;

    int ret;
    char file_name[32];
    int ch_index, malloc_chan_index;
    struct session_packet_buffer* pack_buffer;
    unz_file_info64 fileInfo;
    char szFilePath[15];
    int bToEnd;
    int read_chan_index;
    int chan_num;
    uint8_t* p_wr;
    uint8_t* p_rd;
    uint8_t byte_align;

    assert(sdi);
    assert(sdi->priv);

    (void)fd;
    (void)revents;

    ret = 0;
    bToEnd = 0;
    packet.status = SR_PKT_OK;

    vdev = sdi->priv;

    assert(vdev->unit_bits > 0);
    assert(vdev->archive);

    chan_num = vdev->enabled_probes;
    byte_align = sdi->mode == LOGIC ? 8 : 1;

    if (chan_num < 1) {
        sr_err("%s: channel count < 1.", __func__);
        return SR_ERR_ARG;
    }
    if (chan_num > SESSION_MAX_CHANNEL_COUNT) {
        sr_err("%s: channel count is to big.", __func__);
        return SR_ERR_ARG;
    }

    g_timer_start(packet_interval);

    // Make buffer
    if (vdev->packet_buffer == NULL) {
        vdev->cur_block = 0;

        vdev->packet_buffer = g_try_malloc0(sizeof(struct session_packet_buffer));
        if (vdev->packet_buffer == NULL) {
            sr_err("%s: vdev->packet_buffer malloc failed", __func__);
            return SR_ERR_MALLOC;
        }
        memset(vdev->packet_buffer, 0, sizeof(struct session_packet_buffer));

        for (ch_index = 0; ch_index <= chan_num; ch_index++) {
            vdev->packet_buffer->block_bufs[ch_index] = NULL;
            vdev->packet_buffer->block_read_positions[ch_index] = 0;
        }

        vdev->packet_buffer->post_buf_len = chan_num * vdev->packet_len;

        vdev->packet_buffer->post_buf = g_try_malloc0(vdev->packet_buffer->post_buf_len + 1);
        if (vdev->packet_buffer->post_buf == NULL) {
            sr_err("%s: vdev->packet_buffer->post_buf malloc failed", __func__);
            return SR_ERR_MALLOC;
        }

        pack_buffer = vdev->packet_buffer;
        pack_buffer->block_buf_len = 0;
        pack_buffer->block_data_len = 0;
        pack_buffer->block_chan_read_pos = 0;

        max_probe_num = chan_num;
    }
    pack_buffer = vdev->packet_buffer;

    if (chan_num != max_probe_num) {
        for (ch_index = 0; ch_index < chan_num; ch_index++) {
            if (pack_buffer->block_bufs[ch_index] != NULL) {
                safe_free(pack_buffer->block_bufs[ch_index]);
            }
            pack_buffer->block_bufs[ch_index] = NULL;
            pack_buffer->block_read_positions[ch_index] = 0;
        }
        pack_buffer->block_buf_len = 0;
        pack_buffer->block_data_len = 0;
        pack_buffer->block_chan_read_pos = 0;
        max_probe_num = chan_num;
    }

    if (pack_buffer->post_buf_len != chan_num * vdev->packet_len) {
        pack_buffer->post_buf_len = chan_num * vdev->packet_len;
        safe_free(pack_buffer->post_buf);

        pack_buffer->post_buf = g_try_malloc0(pack_buffer->post_buf_len);
        if (pack_buffer->post_buf == NULL) {
            sr_err("%s: pack_buffer->post_buf malloc failed", __func__);
            return SR_ERR_MALLOC;
        }

        pack_buffer->post_len = 0;
    }

    // Make packet.
    read_chan_index = 0;

    while (pack_buffer->post_len < pack_buffer->post_buf_len) {
        if (pack_buffer->block_chan_read_pos >= pack_buffer->block_data_len) {
            if (vdev->cur_block >= vdev->num_blocks) {
                bToEnd = 1;
                break;
            }

            for (ch_index = 0; ch_index < chan_num; ch_index++) {
                snprintf(file_name, sizeof(file_name) - 1, "L-%d/%d", ch_index, vdev->cur_block);

                if (unzLocateFile(vdev->archive, file_name, 0) != UNZ_OK) {
                    sr_err("can't locate zip inner file:\"%s\"", file_name);
                    send_error_packet(sdi, vdev, &packet);
                    return FALSE;
                }

                if (unzGetCurrentFileInfo64(vdev->archive, &fileInfo, szFilePath,
                        sizeof(szFilePath), NULL, 0, NULL, 0)
                    != UNZ_OK) {
                    sr_err("%s: unzGetCurrentFileInfo64 error.", __func__);
                    send_error_packet(sdi, vdev, &packet);
                    return FALSE;
                }

                if (ch_index == 0) {
                    pack_buffer->block_data_len = fileInfo.uncompressed_size;

                    if (pack_buffer->block_data_len > pack_buffer->block_buf_len) {
                        for (malloc_chan_index = 0; malloc_chan_index < chan_num; malloc_chan_index++) {
                            // Release the old buffer.
                            if (pack_buffer->block_bufs[malloc_chan_index] != NULL) {
                                safe_free(pack_buffer->block_bufs[malloc_chan_index]);
                                pack_buffer->block_bufs[malloc_chan_index] = NULL;
                            }

                            pack_buffer->block_bufs[malloc_chan_index] = g_try_malloc0(pack_buffer->block_data_len + 1);
                            if (pack_buffer->block_bufs[malloc_chan_index] == NULL) {
                                sr_err("%s: block buffer malloc failed", __func__);
                                send_error_packet(sdi, vdev, &packet);
                                return FALSE;
                            }
                            pack_buffer->block_buf_len = pack_buffer->block_data_len;
                        }
                    }
                } else {
                    if (pack_buffer->block_data_len != fileInfo.uncompressed_size) {
                        sr_err("The block size is not coincident:%s", file_name);
                        send_error_packet(sdi, vdev, &packet);
                        return FALSE;
                    }
                }

                // Read the data to buffer.
                if (unzOpenCurrentFile(vdev->archive) != UNZ_OK) {
                    sr_err("can't open zip inner file:\"%s\"", file_name);
                    send_error_packet(sdi, vdev, &packet);
                    return FALSE;
                }

                ret = unzReadCurrentFile(vdev->archive, pack_buffer->block_bufs[ch_index], pack_buffer->block_data_len);
                if (-1 == ret) {
                    sr_err("read zip inner file error:\"%s\"", file_name);
                    send_error_packet(sdi, vdev, &packet);
                    return FALSE;
                }
                unzCloseCurrentFile(vdev->archive);
                pack_buffer->block_read_positions[ch_index] = 0; // Reset the read position.
            }
            vdev->cur_block++;
            pack_buffer->block_chan_read_pos = 0;
        }

        p_wr = (uint8_t*)pack_buffer->post_buf + pack_buffer->post_len;
        p_rd = (uint8_t*)pack_buffer->block_bufs[read_chan_index] + pack_buffer->block_read_positions[read_chan_index];
        *p_wr = *p_rd;

        pack_buffer->post_len++;
        pack_buffer->block_read_positions[read_chan_index]++;

        if (pack_buffer->block_read_positions[read_chan_index] % byte_align == 0
            || pack_buffer->block_read_positions[read_chan_index] == pack_buffer->block_data_len) {
            read_chan_index++;

            if (pack_buffer->block_read_positions[read_chan_index] == pack_buffer->block_data_len) {
                if (vdev->cur_block < vdev->num_blocks) {
                    break;
                }
            }

            // Each channel's data is ready.
            if (read_chan_index == chan_num) {
                read_chan_index = 0;
                pack_buffer->block_chan_read_pos += byte_align;
            }
        }
    }

    if (pack_buffer->post_len >= (uint64_t)(byte_align * chan_num)) {
        packet.type = SR_DF_LOGIC;
        packet.payload = &logic;
        logic.format = LA_CROSS_DATA;
        logic.index = 0;
        logic.order = 0;
        logic.length = pack_buffer->post_len;
        logic.data = pack_buffer->post_buf;

        delay_time(vdev);
        ds_data_forward(sdi, &packet);

        pack_buffer->post_len = 0;
    }

    if (bToEnd || revents == -1) {
        packet.type = SR_DF_END;
        ds_data_forward(sdi, &packet);
        sr_session_source_remove(-1);
        close_archive(vdev);
        free_temp_buffer(vdev);
    }

    return TRUE;
}

int get_bit(uint64_t timebase)
{
    if (timebase < SR_MS(1) && timebase >= SR_US(20)) {
        return SR_MS(1) / timebase * 2;
    } else if (timebase < SR_MS(200) && timebase >= SR_MS(1)) {
        return SR_MS(200) / timebase * 2;
    } else if (timebase <= SR_SEC(10) && timebase > SR_MS(200)) {
        return SR_SEC(40) / timebase * 2;
    } else {
        return 200;
    }
}

static int receive_data_dso(int fd, int revents, const struct sr_dev_inst* sdi)
{
    struct session_vdev* vdev = NULL;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_dso dso;
    struct sr_channel* probe;

    int ret;
    char file_name[32];
    int ch_index, malloc_chan_index;
    struct session_packet_buffer* pack_buffer;
    unz_file_info64 fileInfo;
    char szFilePath[15];
    int bToEnd;
    int read_chan_index;
    uint16_t chan_num;
    uint8_t* p_wr;
    uint8_t* p_rd;
    uint8_t byte_align;

    uint16_t tem;
    uint8_t val = 0;
    uint64_t vdiv;

    assert(sdi);
    assert(sdi->priv);

    (void)fd;
    (void)revents;

    sr_detail("Feed chunk.");

    ret = 0;
    bToEnd = 0;
    packet.status = SR_PKT_OK;

    vdev = sdi->priv;

    assert(vdev->unit_bits > 0);
    if (vdev->sample_generator != PATTERN_RANDOM) {
        assert(vdev->archive);
    }

    chan_num = vdev->num_probes;
    byte_align = sdi->mode == LOGIC ? 8 : 1;

    if (chan_num < 1) {
        sr_err("%s: channel count < 1.", __func__);
        return SR_ERR_ARG;
    }
    if (chan_num > SESSION_MAX_CHANNEL_COUNT) {
        sr_err("%s: channel count is to big.", __func__);
        return SR_ERR_ARG;
    }

    // Make buffer
    if (vdev->packet_buffer == NULL) {
        vdev->cur_block = 0;

        vdev->packet_buffer = g_try_malloc0(sizeof(struct session_packet_buffer));
        if (vdev->packet_buffer == NULL) {
            sr_err("%s: vdev->packet_buffer malloc failed", __func__);
            return SR_ERR_MALLOC;
        }
        memset(vdev->packet_buffer, 0, sizeof(struct session_packet_buffer));

        for (ch_index = 0; ch_index <= chan_num; ch_index++) {
            vdev->packet_buffer->block_bufs[ch_index] = NULL;
            vdev->packet_buffer->block_read_positions[ch_index] = 0;
        }

        vdev->packet_buffer->post_buf_len = chan_num * 10000;

        vdev->packet_buffer->post_buf = g_try_malloc0(vdev->packet_buffer->post_buf_len);
        if (vdev->packet_buffer->post_buf == NULL) {
            sr_err("%s: vdev->packet_buffer->post_buf malloc failed", __func__);
            return SR_ERR_MALLOC;
        }

        pack_buffer = vdev->packet_buffer;
        pack_buffer->block_buf_len = 0;
        pack_buffer->block_data_len = 0;
        pack_buffer->block_chan_read_pos = 0;
    }

    pack_buffer = vdev->packet_buffer;

    if (pack_buffer->post_buf_len != chan_num * 10000) {
        vdev->packet_buffer->post_buf_len = chan_num * 10000;
        safe_free(pack_buffer->post_buf);

        pack_buffer->post_buf = g_try_malloc0(pack_buffer->post_buf_len);
        if (pack_buffer->post_buf == NULL) {
            sr_err("%s: pack_buffer->post_buf malloc failed", __func__);
            return SR_ERR_MALLOC;
        }

        pack_buffer->post_len = 0;
    }

    if (chan_num != max_probe_num) {
        for (ch_index = 0; ch_index < chan_num; ch_index++) {
            if (pack_buffer->block_bufs[ch_index] != NULL) {
                safe_free(pack_buffer->block_bufs[ch_index]);
            }
            pack_buffer->block_bufs[ch_index] = NULL;
            pack_buffer->block_read_positions[ch_index] = 0;
        }
        pack_buffer->block_buf_len = 0;
        pack_buffer->block_data_len = 0;
        pack_buffer->block_chan_read_pos = 0;
        max_probe_num = chan_num;
    }

    // Make packet.
    read_chan_index = 0;

    if (vdev->vdiv_change || vdev->timebase_change || vdev->offset_change) {
        if (vdev->sample_generator == PATTERN_RANDOM) {
            for (uint64_t i = 0; i < pack_buffer->post_buf_len; i++) {
                if (i % 2 == 0) {
                    *(uint8_t*)(pack_buffer->post_buf + i) = DSO_RANDOM_DATA;
                } else {
                    *(uint8_t*)(pack_buffer->post_buf + i) = *(uint8_t*)(pack_buffer->post_buf + i - 1);
                }
            }
            pack_buffer->post_len = pack_buffer->post_buf_len;
        } else {
            if (vdev->load_data) {
                pack_buffer->post_len = 0;
                while (pack_buffer->post_len < pack_buffer->post_buf_len) {
                    if (pack_buffer->block_chan_read_pos >= pack_buffer->block_data_len) {
                        if (vdev->cur_block >= vdev->num_blocks) {
                            vdev->cur_block = 0;
                        }

                        for (ch_index = 0; ch_index < chan_num; ch_index++) {
                            snprintf(file_name, sizeof(file_name) - 1, "O-%d/0", ch_index);

                            if (unzLocateFile(vdev->archive, file_name, 0) != UNZ_OK) {
                                sr_err("cant't locate zip inner file:\"%s\"", file_name);
                                send_error_packet(sdi, vdev, &packet);
                                return FALSE;
                            }

                            if (unzGetCurrentFileInfo64(vdev->archive, &fileInfo, szFilePath,
                                    sizeof(szFilePath), NULL, 0, NULL, 0)
                                != UNZ_OK) {
                                sr_err("%s: unzGetCurrentFileInfo64 error.", __func__);
                                send_error_packet(sdi, vdev, &packet);
                                return FALSE;
                            }

                            if (ch_index == 0) {
                                pack_buffer->block_data_len = fileInfo.uncompressed_size;

                                if (pack_buffer->block_data_len > pack_buffer->block_buf_len) {
                                    for (malloc_chan_index = 0; malloc_chan_index < chan_num; malloc_chan_index++) {
                                        // Release the old buffer.
                                        if (pack_buffer->block_bufs[malloc_chan_index] != NULL) {
                                            safe_free(pack_buffer->block_bufs[malloc_chan_index]);
                                            pack_buffer->block_bufs[malloc_chan_index] = NULL;
                                        }

                                        pack_buffer->block_bufs[malloc_chan_index] = g_try_malloc0(pack_buffer->block_data_len + 1);
                                        if (pack_buffer->block_bufs[malloc_chan_index] == NULL) {
                                            sr_err("%s: block buffer malloc failed", __func__);
                                            send_error_packet(sdi, vdev, &packet);
                                            return FALSE;
                                        }
                                        pack_buffer->block_buf_len = pack_buffer->block_data_len;
                                    }
                                }
                            } else {
                                if (pack_buffer->block_data_len != fileInfo.uncompressed_size) {
                                    sr_err("The block size is not coincident:%s", file_name);
                                    send_error_packet(sdi, vdev, &packet);
                                    return FALSE;
                                }
                            }

                            // Read the data to buffer.
                            if (unzOpenCurrentFile(vdev->archive) != UNZ_OK) {
                                sr_err("cant't open zip inner file:\"%s\"", file_name);
                                send_error_packet(sdi, vdev, &packet);
                                return FALSE;
                            }

                            ret = unzReadCurrentFile(vdev->archive, pack_buffer->block_bufs[ch_index], pack_buffer->block_data_len);
                            if (-1 == ret) {
                                sr_err("read zip inner file error:\"%s\"", file_name);
                                send_error_packet(sdi, vdev, &packet);
                                return FALSE;
                            }
                            unzCloseCurrentFile(vdev->archive);
                            pack_buffer->block_read_positions[ch_index] = 0; // Reset the read position.
                        }
                        vdev->cur_block++;
                        pack_buffer->block_chan_read_pos = 0;
                    }

                    p_wr = (uint8_t*)pack_buffer->post_buf + pack_buffer->post_len;
                    p_rd = (uint8_t*)pack_buffer->block_bufs[read_chan_index] + pack_buffer->block_read_positions[read_chan_index];
                    *p_wr = *p_rd;

                    pack_buffer->post_len++;
                    pack_buffer->block_read_positions[read_chan_index]++;

                    if (pack_buffer->block_read_positions[read_chan_index] % byte_align == 0
                        || pack_buffer->block_read_positions[read_chan_index] == pack_buffer->block_data_len) {
                        read_chan_index++;

                        if (pack_buffer->block_read_positions[read_chan_index] == pack_buffer->block_data_len) {
                            if (vdev->cur_block < vdev->num_blocks) {
                                break;
                            }
                        }

                        // Each channel's data is ready.
                        if (read_chan_index == chan_num) {
                            read_chan_index = 0;
                            pack_buffer->block_chan_read_pos += byte_align;
                        }
                    }
                }
                memcpy(vdev->data_buf, pack_buffer->post_buf, DSO_PACKET_LEN);
                vdev->load_data = FALSE;
            } else {
                memcpy(pack_buffer->post_buf, vdev->data_buf, DSO_PACKET_LEN);
                pack_buffer->post_buf_len = DSO_PACKET_LEN;
            }
        }

        if (vdev->timebase_change || vdev->vdiv_change || vdev->offset_change) {
            dso_wavelength_updata(vdev);

            uint16_t high_gate, low_gate;
            for (uint64_t i = 0; i < pack_buffer->post_buf_len; i++) {
                if (i % 2 == 0) {
                    probe = g_slist_nth(sdi->channels, 0)->data;
                } else {
                    probe = g_slist_nth(sdi->channels, 1)->data;
                }

                if (!probe->enabled) {
                    if (i % 2 == 0) {
                        probe = g_slist_nth(sdi->channels, 1)->data;
                    } else {
                        probe = g_slist_nth(sdi->channels, 0)->data;
                    }
                }
                vdiv = probe->vdiv;

                uint8_t temp_val = *((uint8_t*)pack_buffer->post_buf + i);

                if (vdiv > SR_mV(200)) {
                    if (temp_val > DSO_MID_VAL) {
                        val = temp_val - DSO_MID_VAL;
                        tem = val * DSO_DEFAULT_VDIV / vdiv;
                        temp_val = DSO_MID_VAL + tem;
                    } else if (temp_val < DSO_MID_VAL) {
                        val = DSO_MID_VAL - temp_val;
                        tem = val * DSO_DEFAULT_VDIV / vdiv;
                        temp_val = DSO_MID_VAL - tem;
                    }
                    *((uint8_t*)pack_buffer->post_buf + i) = temp_val;
                } else {
                    if (temp_val > DSO_MID_VAL) {
                        val = temp_val - DSO_MID_VAL;
                        tem = (uint16_t)val * (uint16_t)DSO_DEFAULT_VDIV / (uint16_t)vdiv;
                        tem = DSO_EXPAND_MID_VAL(SR_mV(200) / vdiv) + tem;
                    } else if (temp_val < DSO_MID_VAL) {
                        val = DSO_MID_VAL - temp_val;
                        tem = (uint16_t)val * (uint16_t)DSO_DEFAULT_VDIV / (uint16_t)vdiv;
                        tem = DSO_EXPAND_MID_VAL(SR_mV(200) / vdiv) - tem;
                    } else {
                        tem = DSO_EXPAND_MID_VAL(SR_mV(200) / vdiv);
                    }

                    high_gate = DSO_EXPAND_MID_VAL(SR_mV(200) / vdiv);
                    high_gate -= probe->offset;
                    low_gate = high_gate + DSO_LIMIT;
                    if (tem <= high_gate) {
                        tem = DSO_MAX_VAL;
                    } else if (tem >= low_gate) {
                        tem = DSO_MIN_VAL;
                    } else {
                        tem -= high_gate;
                    }

                    *((uint8_t*)pack_buffer->post_buf + i) = (uint8_t)tem;
                }
            }

            vdev->offset_change = FALSE;
            vdev->timebase_change = FALSE;
            vdev->vdiv_change = FALSE;
        }
    }

    gdouble total_time = vdev->timebase / (gdouble)SR_SEC(1) * (gdouble)10;
    gdouble total_time_elapsed = g_timer_elapsed(run_time, NULL);

    if (!vdev->instant) {
        if (total_time_elapsed < total_time) {
            gdouble percent = total_time_elapsed / total_time;
            int buf_len = percent * DSO_PACKET_LEN;
            if (buf_len % 2 != 0) {
                buf_len += 1;
            }
            pack_buffer->post_len = buf_len;
        } else {
            uint8_t top0;
            uint8_t top1;
            if (vdev->sample_generator == PATTERN_RANDOM) {
                top0 = *((uint8_t*)pack_buffer->post_buf + pack_buffer->post_buf_len - 2);
                top1 = *((uint8_t*)pack_buffer->post_buf + pack_buffer->post_buf_len - 1);
            } else {
                top0 = *((uint8_t*)pack_buffer->post_buf + get_bit(vdev->timebase) - 2);
                top1 = *((uint8_t*)pack_buffer->post_buf + get_bit(vdev->timebase) - 1);
            }

            for (int i = pack_buffer->post_len - 1; i > 1; i -= 2) {
                *((uint8_t*)pack_buffer->post_buf + i) = *((uint8_t*)pack_buffer->post_buf + i - 2);
            }

            for (int i = pack_buffer->post_len - 2; i > 0; i -= 2) {
                *((uint8_t*)pack_buffer->post_buf + i) = *((uint8_t*)pack_buffer->post_buf + i - 2);
            }

            *(uint8_t*)pack_buffer->post_buf = top0;
            *((uint8_t*)pack_buffer->post_buf + 1) = top1;
            pack_buffer->post_len = DSO_PACKET_LEN;
        }
    } else {
        if (DSO_PACKET_LEN > vdev->post_data_len) {
            pack_buffer->post_len = vdev->packet_len;
            vdev->post_data_len += vdev->packet_len;
        } else {
            bToEnd = 1;
            vdev->instant = FALSE;
        }
    }

    if (pack_buffer->post_len >= byte_align * chan_num && !bToEnd) {
        packet.type = SR_DF_DSO;
        packet.payload = &dso;
        dso.probes = sdi->channels;
        dso.mq = SR_MQ_VOLTAGE;
        dso.unit = SR_UNIT_VOLT;
        dso.mqflags = SR_MQFLAG_AC;
        dso.num_samples = pack_buffer->post_len / chan_num;
        if (vdev->instant) {
            dso.data = pack_buffer->post_buf + vdev->post_data_len;
        } else {
            dso.data = pack_buffer->post_buf;
        }
        ds_data_forward(sdi, &packet);
        if (vdev->instant) {
            instant_delay_time(vdev);
        } else {
            delay_time(vdev);
            g_timer_start(packet_interval);
        }

        dso_status_update(vdev);
    }

    if (bToEnd || revents == -1) {
        packet.type = SR_DF_END;
        ds_data_forward(sdi, &packet);
        sr_session_source_remove(-1);
    }

    return TRUE;
}

static int receive_data_analog(int fd, int revents, const struct sr_dev_inst* sdi)
{
    struct session_vdev* vdev = sdi->priv;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_analog analog;
    struct sr_channel* probe = NULL;

    char file_name[32];
    int ret;

    assert(sdi);
    assert(sdi->priv);

    (void)fd;
    (void)revents;

    sr_detail("Feed chunk.");

    packet.status = SR_PKT_OK;

    assert(vdev->unit_bits > 0);

    g_timer_start(packet_interval);

    if (vdev->load_data) {
        if (vdev->sample_generator != PATTERN_RANDOM) {
            vdev->data_buf_len = 0;

            void* analog_data = g_try_malloc0(ANALOG_DATA_LEN_PER_CYCLE);
            if (analog_data == NULL) {
                sr_err("%s:analog_data malloc failed", __func__);
                return SR_ERR_MALLOC;
            }

            snprintf(file_name, sizeof(file_name) - 1, "%s-%d/%d", "A",
                0, 0);

            if (unzLocateFile(vdev->archive, file_name, 0) != UNZ_OK) {
                sr_err("can't locate zip inner file:\"%s\"", file_name);
                send_error_packet(sdi, vdev, &packet);
                return FALSE;
            }
            if (unzOpenCurrentFile(vdev->archive) != UNZ_OK) {
                sr_err("can't open zip inner file:\"%s\"", file_name);
                send_error_packet(sdi, vdev, &packet);
                return FALSE;
            }

            ret = unzReadCurrentFile(vdev->archive, analog_data, ANALOG_DATA_LEN_PER_CYCLE);
            if (-1 == ret) {
                sr_err("read zip inner file error:\"%s\"", file_name);
                send_error_packet(sdi, vdev, &packet);
                return FALSE;
            }

            uint64_t total_buf_len = ANALOG_CYCLE_RATIO * vdev->total_samples * ANALOG_PROBE_NUM;
            if (total_buf_len % ANALOG_DATA_LEN_PER_CYCLE != 0) {
                total_buf_len = total_buf_len / ANALOG_DATA_LEN_PER_CYCLE * ANALOG_DATA_LEN_PER_CYCLE;
            }

            safe_free(vdev->data_buf);
            vdev->data_buf = g_try_malloc0(total_buf_len);
            if (vdev->data_buf == NULL) {
                sr_err("%s: vdev->data_buf malloc failed", __func__);
                return SR_ERR_MALLOC;
            }

            vdev->data_buf_len = total_buf_len;
            uint64_t per_block_after_expend = total_buf_len / ANALOG_DATA_LEN_PER_CYCLE;

            probe = g_slist_nth(sdi->channels, 0)->data;
            uint64_t p0_vdiv = probe->vdiv;
            probe = g_slist_nth(sdi->channels, 1)->data;
            uint64_t p1_vdiv = probe->vdiv;
            uint64_t vdiv;
            uint8_t val = 0;
            uint16_t tem;
            uint64_t cur_l = 0;

            for (int i = 0; i < ANALOG_DATA_LEN_PER_CYCLE; i++) {
                if (i % 2 == 0) {
                    vdiv = p0_vdiv;
                } else {
                    vdiv = p1_vdiv;
                }
                tem = 0;

                uint8_t temp_value = *((uint8_t*)analog_data + i);

                if (temp_value > ANALOG_MID_VAL) {
                    val = temp_value - ANALOG_MID_VAL;
                    tem = val * ANALOG_DEFAULT_VDIV / vdiv;
                    if (tem >= ANALOG_MID_VAL) {
                        temp_value = ANALOG_MIN_VAL;
                    } else {
                        temp_value = ANALOG_MID_VAL + tem;
                    }
                } else if (temp_value < ANALOG_MID_VAL) {
                    val = ANALOG_MID_VAL - temp_value;
                    tem = val * ANALOG_DEFAULT_VDIV / vdiv;

                    if (tem >= ANALOG_MID_VAL) {
                        temp_value = ANALOG_MAX_VAL;
                    } else {
                        temp_value = ANALOG_MID_VAL - tem;
                    }
                }

                for (uint64_t j = 0; j < per_block_after_expend; j++) {
                    if (i % 2 == 0) {
                        cur_l = i * per_block_after_expend + j * 2;
                    } else {
                        cur_l = 1 + (i - 1) * per_block_after_expend + j * 2;
                    }
                    memset(vdev->data_buf + cur_l, temp_value, 1);
                }
            }
            safe_free(analog_data);
        }
        vdev->load_data = FALSE;
    }

    if (vdev->analog_post_buf_len != vdev->packet_len) {
        safe_free(vdev->analog_post_buf);
        vdev->analog_post_buf = g_try_malloc0(vdev->packet_len);
        if (vdev->analog_post_buf == NULL) {
            sr_err("%s: buf malloc failed", __func__);
            return SR_ERR_MALLOC;
        }
        vdev->analog_post_buf_len = vdev->packet_len;
    }

    if (vdev->analog_read_pos + vdev->packet_len >= vdev->data_buf_len - 1) {
        uint64_t back_len = vdev->data_buf_len - vdev->analog_read_pos;
        uint64_t front_len = vdev->packet_len - back_len;
        memcpy(vdev->analog_post_buf, vdev->data_buf + vdev->analog_read_pos, back_len);
        memcpy(vdev->analog_post_buf + back_len, vdev->data_buf, front_len);
        vdev->analog_read_pos = front_len;
    } else {
        memcpy(vdev->analog_post_buf, vdev->data_buf + vdev->analog_read_pos, vdev->packet_len);
        vdev->analog_read_pos += vdev->packet_len;
    }

    packet.type = SR_DF_ANALOG;
    packet.payload = &analog;
    analog.probes = sdi->channels;
    analog.num_samples = vdev->packet_len / vdev->num_probes;
    analog.unit_bits = vdev->unit_bits;
    analog.mq = SR_MQ_VOLTAGE;
    analog.unit = SR_UNIT_VOLT;
    analog.mqflags = SR_MQFLAG_AC;
    analog.data = vdev->analog_post_buf;

    delay_time(vdev);
    ds_data_forward(sdi, &packet);

    return TRUE;
}

static void send_error_packet(const struct sr_dev_inst* cb_sdi, struct session_vdev* vdev, struct sr_datafeed_packet* packet)
{
    packet->type = SR_DF_END;
    packet->status = SR_PKT_SOURCE_ERROR;
    ds_data_forward(cb_sdi, packet);
    sr_session_source_remove(-1);
    close_archive(vdev);
}

static int close_archive(struct session_vdev* vdev)
{
    if (vdev->sample_generator != PATTERN_RANDOM) {
        assert(vdev->archive);

        if (vdev->capfile) {
            unzCloseCurrentFile(vdev->archive);
            vdev->capfile = 0;
        }

        int ret = unzClose(vdev->archive);
        if (ret != UNZ_OK) {
            sr_err("close zip archive error!");
        }

        vdev->archive = NULL;
    }

    return SR_OK;
}

static int load_virtual_device_session(struct sr_dev_inst* sdi)
{
    GKeyFile* kf;
    unzFile archive = NULL;
    char szFilePath[15];
    unz_file_info64 fileInfo;

    struct sr_channel* probe;
    int i, j;
    uint64_t tmp_u64;
    char **sections, **keys, *metafile, *val;
    int channel_type = SR_CHANNEL_LOGIC;
    struct session_vdev* vdev = sdi->priv;

    assert(sdi);
    if (vdev->sample_generator != PATTERN_RANDOM) {
        assert(sdi->path);
    }

    switch (sdi->mode) {
    case LOGIC:
        if (vdev->sample_generator != PATTERN_RANDOM) {
            archive = unzOpen64(sdi->path);
            if (NULL == archive) {
                sr_err("%s: Load file error:\"%s\"", __func__, sdi->path);
                return SR_ERR;
            }
            if (unzLocateFile(archive, "header", 0) != UNZ_OK) {
                unzClose(archive);
                sr_err("%s: unzLocateFile error.", __func__);
                return SR_ERR;
            }
            if (unzGetCurrentFileInfo64(archive, &fileInfo, szFilePath,
                    sizeof(szFilePath), NULL, 0, NULL, 0)
                != UNZ_OK) {
                unzClose(archive);
                sr_err("%s: unzGetCurrentFileInfo64 error.", __func__);
                return SR_ERR;
            }
            if (unzOpenCurrentFile(archive) != UNZ_OK) {
                sr_err("%s: Cant't open zip inner file.", __func__);
                unzClose(archive);
                return SR_ERR;
            }

            if (!(metafile = g_try_malloc0(fileInfo.uncompressed_size))) {
                sr_err("%s: metafile malloc failed", __func__);
                return SR_ERR_MALLOC;
            }

            unzReadCurrentFile(archive, metafile, fileInfo.uncompressed_size);
            unzCloseCurrentFile(archive);

            if (unzClose(archive) != UNZ_OK) {
                sr_err("%s: Close zip archive error.", __func__);
                return SR_ERR;
            }
            archive = NULL;

            kf = g_key_file_new();
            if (!g_key_file_load_from_data(kf, metafile, fileInfo.uncompressed_size, 0, NULL)) {
                sr_err("Failed to parse metadata.");
                return SR_ERR;
            }

            sections = g_key_file_get_groups(kf, NULL);

            for (i = 0; sections[i]; i++) {
                if (!strncmp(sections[i], "header", 6)) {
                    keys = g_key_file_get_keys(kf, sections[i], NULL, NULL);

                    for (j = 0; keys[j]; j++) {
                        val = g_key_file_get_string(kf, sections[i], keys[j], NULL);

                        if (!strcmp(keys[j], "samplerate")) {
                            sr_parse_sizestring(val, &tmp_u64);
                            vdev->samplerate = tmp_u64;
                            samplerates_file[0] = vdev->samplerate;
                        } else if (!strcmp(keys[j], "total samples")) {
                            tmp_u64 = strtoull(val, NULL, 10);
                            vdev->total_samples = tmp_u64;
                            samplecounts_file[0] = vdev->total_samples;
                        } else if (!strcmp(keys[j], "total blocks")) {
                            tmp_u64 = strtoull(val, NULL, 10);
                            vdev->num_blocks = tmp_u64;
                        } else if (!strcmp(keys[j], "total probes")) {
                            sr_dev_probes_free(sdi);
                            tmp_u64 = strtoull(val, NULL, 10);
                            vdev->num_probes = tmp_u64;
                        } else if (!strncmp(keys[j], "probe", 5)) {
                            tmp_u64 = strtoul(keys[j] + 5, NULL, 10);
                            channel_type = SR_CHANNEL_LOGIC;
                            if (!(probe = sr_channel_new(tmp_u64, channel_type, TRUE, val))) {
                                sr_err("%s: create channel failed", __func__);
                                sr_dev_inst_free(sdi);
                                return SR_ERR;
                            }

                            sdi->channels = g_slist_append(sdi->channels, probe);
                        }
                    }
                    adjust_samplerate(sdi);
                    g_strfreev(keys);
                }
            }
            g_strfreev(sections);
            g_key_file_free(kf);
            safe_free(metafile);
        } else {
            vdev->samplerate = LOGIC_DEFAULT_SAMPLERATE;
            vdev->total_samples = LOGIC_DEFAULT_TOTAL_SAMPLES;
            vdev->num_probes = logic_channel_modes[vdev->logic_ch_mode_index].num;
            sr_dev_probes_free(sdi);

            for (int i = 0; i < vdev->num_probes; i++) {
                if (!(probe = sr_channel_new(i, SR_CHANNEL_LOGIC, TRUE, probe_names[i]))) {
                    sr_err("%s: create channel failed", __func__);
                    sr_dev_inst_free(sdi);
                    return SR_ERR;
                }
                sdi->channels = g_slist_append(sdi->channels, probe);
            }
            logic_adjust_samplerate(sdi->priv);
        }
        break;
    case DSO:
        vdev->samplerate = DSO_DEFAULT_SAMPLERATE;
        vdev->total_samples = DSO_DEFAULT_TOTAL_SAMPLES;
        vdev->num_probes = DSO_DEFAULT_NUM_PROBE;
        vdev->num_blocks = DSO_DEFAULT_NUM_BLOCK;
        sr_dev_probes_free(sdi);

        for (int i = 0; i < DSO_DEFAULT_NUM_PROBE; i++) {
            if (!(probe = sr_channel_new(i, SR_CHANNEL_DSO, TRUE, probe_names[i]))) {
                sr_err("%s: create channel failed", __func__);
                sr_dev_inst_free(sdi);
                return SR_ERR;
            }
            probe->enabled = DSO_DEFAULT_ENABLE;
            probe->coupling = DSO_DEFAULT_COUPLING;
            probe->vdiv = DSO_DEFAULT_VIDV;
            probe->vfactor = DSO_DEFAULT_VFACOTR;
            probe->hw_offset = DSO_DEFAULT_HW_OFFSET;
            probe->offset = DSO_DEFAULT_OFFSET;
            probe->trig_value = DSO_DEFAULT_TRIG_VAL;
            sdi->channels = g_slist_append(sdi->channels, probe);
        }
        adjust_samplerate(sdi);
        break;
    case ANALOG:
        vdev->samplerate = ANALOG_DEFAULT_SAMPLERATE;
        vdev->total_samples = ANALOG_DEFAULT_TOTAL_SAMPLES;
        vdev->num_probes = ANALOG_DEFAULT_NUM_PROBE;
        vdev->num_blocks = ANALOG_DEFAULT_NUM_BLOCK;
        sr_dev_probes_free(sdi);

        for (int i = 0; i < ANALOG_DEFAULT_NUM_PROBE; i++) {
            if (!(probe = sr_channel_new(i, SR_CHANNEL_ANALOG, TRUE, probe_names[i]))) {
                sr_err("%s: create channel failed", __func__);
                sr_dev_inst_free(sdi);
                return SR_ERR;
            }
            probe->bits = ANALOG_DEFAULT_BIT;
            probe->enabled = ANALOG_DEFAULT_ENABLE;
            probe->coupling = ANALOG_DEFAULT_COUPLING;
            probe->vdiv = ANALOG_DEFAULT_VIDV;
            probe->vfactor = ANALOG_DEFAULT_VFACOTR;
            probe->hw_offset = ANALOG_DEFAULT_HW_OFFSET;
            probe->offset = ANALOG_DEFAULT_OFFSET;
            probe->trig_value = ANALOG_DEFAULT_TRIG_VAL;
            probe->map_default = ANALOG_DEFAULT_MAP_DEFAULT;
            probe->map_unit = ANALOG_DEFAULT_MAP_UNIT;
            probe->map_min = ANALOG_DEFAULT_MAP_MIN;
            probe->map_max = ANALOG_DEFAULT_MAP_MAX;

            sdi->channels = g_slist_append(sdi->channels, probe);
        }
        adjust_samplerate(sdi);
        break;
    default:
        break;
    }
    return SR_OK;
}

int dso_wavelength_updata(struct session_vdev* vdev)
{
    int index;
    int bit = get_bit(vdev->timebase);
    struct session_packet_buffer* pack_buffer = vdev->packet_buffer;
    uint8_t tmp_val;

    int l = 0;
    char* pattern_mode = demo_pattern_array[DSO].patterns[vdev->sample_generator];
    const uint8_t wave_max_val = 78;

    if (vdev->sample_generator != PATTERN_RANDOM) {
        if (strcmp(pattern_mode, "square")) {
            for (int i = 0; i < DSO_PACKET_LEN / 2; i += 2) {
                tmp_val = *((uint8_t*)pack_buffer->post_buf + i);
                if (tmp_val == wave_max_val) {
                    l = i;
                    break;
                }
            }
        }

        void* tmp_buf = g_try_malloc0(bit);
        if (tmp_buf == NULL) {
            sr_err("%s: tmp_buf malloc failed", __func__);
            return SR_ERR_MALLOC;
        }

        for (int i = 0; i < bit; i++) {
            if (i % 2 == 0) {
                if (!strcmp(pattern_mode, "sawtooth") && i == bit - 2) {
                    index = DSO_WAVE_PERIOD_LEN - 2;
                } else if (strcmp(pattern_mode, "sawtooth") && bit == 10) {
                    index = i * 16;
                } else {
                    index = i * DSO_WAVE_PERIOD_LEN_PER_PROBE / (bit / 2);
                }
            } else {
                if (!strcmp(pattern_mode, "sawtooth") && i == bit - 1) {
                    index = DSO_WAVE_PERIOD_LEN - 1;
                } else if (strcmp(pattern_mode, "sawtooth") && bit == 10) {
                    index = (i - 1) * 16 + 1;
                } else {
                    index = (i - 1) * DSO_WAVE_PERIOD_LEN_PER_PROBE / (bit / 2) + 1;
                }
            }

            *((uint8_t*)tmp_buf + i) = *((uint8_t*)pack_buffer->post_buf + index + l);
        }

        for (int i = 0; i < DSO_PACKET_LEN / bit; i++) {
            memcpy(pack_buffer->post_buf + i * bit, tmp_buf, bit);
        }

        safe_free(tmp_buf);
    }

    return SR_OK;
}

SR_PRIV struct sr_dev_driver demo_driver_info = {
    .name = "virtual-demo",
    .longname = "Demo driver and pattern generator",
    .api_version = 1,
    .driver_type = DRIVER_TYPE_DEMO,
    .init = hw_init,
    .cleanup = hw_clean_up,
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
