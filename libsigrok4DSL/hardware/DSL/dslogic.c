/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2013 DreamSourceLab <dreamsourcelab@dreamsourcelab.com>
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
 */


#include "../../libsigrok-internal.h"

#include "dsl.h"
#include "command.h"
#include "../../log.h"

#undef LOG_PREFIX 
#define LOG_PREFIX "dslogic: "

static int dev_destroy(struct sr_dev_inst *sdi);

static const char *maxHeights[] = {
    "1X",
    "2X",
    "3X",
    "4X",
    "5X",
};

/** Device buffer mode */
enum DSLOGIC_BUFFER_OPT_MODE
{
    /** Stop immediately */
    SR_BUF_STOP = 0,
    /** Upload captured data */
    SR_BUF_UPLOAD = 1,
};

static const struct sr_list_item opmode_list[] = {
    {LO_OP_BUFFER,"Buffer Mode"},
    {LO_OP_STREAM,"Stream Mode"},
    {LO_OP_INTEST,"Internal Test"},
    //{OP_EXTEST,"External Test"},  // Removed
    //{OP_LPTEST,"DRAM Loopback Test"},
    {-1, NULL},
};

static const struct sr_list_item bufoption_list[] = {
    {SR_BUF_STOP, "Stop immediately"},
    {SR_BUF_UPLOAD,"Upload captured data"},
    {-1, NULL},
};

static const struct sr_list_item threshold_list[] = {
    {SR_TH_3V3, "1.8/2.5/3.3V Level"},
    {SR_TH_5V0,"5.0V Level"},
    {-1, NULL},
};

static const struct sr_list_item filter_list[] = {
    {SR_FILTER_NONE, "None"},
    {SR_FILTER_1T,"1 Sample Clock"},
    {-1, NULL},
};

#define CHANNEL_MODE_LIST_LEN 25
static struct sr_list_item channel_mode_list[CHANNEL_MODE_LIST_LEN];

static const struct lang_text_map_item lang_text_map[] = 
{
	{SR_CONF_OPERATION_MODE, LO_OP_BUFFER, "Buffer Mode", "Buffer模式"},
	{SR_CONF_OPERATION_MODE, LO_OP_STREAM, "Stream Mode", "Stream模式"},
	{SR_CONF_OPERATION_MODE, LO_OP_INTEST, "Internal Test", "内部测试"},
    {SR_CONF_OPERATION_MODE, LO_OP_EXTEST, "External Test", "外部测试"},
    {SR_CONF_OPERATION_MODE, LO_OP_LPTEST, "DRAM Loopback Test", "内存回环测试"},

    {SR_CONF_BUFFER_OPTIONS, SR_BUF_STOP, "Stop immediately", "立即停止"},
    {SR_CONF_BUFFER_OPTIONS, SR_BUF_UPLOAD, "Upload captured data", "上传已采集的数据"},

    {SR_CONF_THRESHOLD, SR_TH_3V3, "1.8/2.5/3.3V Level", NULL},
    {SR_CONF_THRESHOLD, SR_TH_5V0, "5.0V Level", NULL},

    {SR_CONF_FILTER, SR_FILTER_NONE, "None", "无"},
    {SR_CONF_FILTER, SR_FILTER_1T, "1 Sample Clock", "1个采样周期"},
};

static const struct sr_list_item channel_mode_cn_map[] = {
    {DSL_STREAM20x16, "使用16个通道(最大采样率 20MHz)"},
    {DSL_STREAM25x12, "使用12个通道(最大采样率 25MHz)"},
    {DSL_STREAM50x6, "使用6个通道(最大采样率 50MHz)"},
    {DSL_STREAM100x3, "使用3个通道(最大采样率 100MHz)"},
    {DSL_STREAM20x16_3DN2, "使用16个通道(最大采样率 20MHz)"},     
    {DSL_STREAM25x12_3DN2, "使用12个通道(最大采样率 25MHz)"},
    {DSL_STREAM50x6_3DN2, "使用6个通道(最大采样率 50MHz)"},
    {DSL_STREAM100x3_3DN2,"使用3个通道(最大采样率 100MHz)"},
    {DSL_STREAM10x32_32_3DN2, "使用32个通道(最大采样率 10MHz)"},
    {DSL_STREAM20x16_32_3DN2, "使用16个通道(最大采样率 20MHz)"},
    {DSL_STREAM25x12_32_3DN2, "使用12个通道(最大采样率 25MHz)"},
    {DSL_STREAM50x6_32_3DN2, "使用6个通道(最大采样率 50MHz)"},
    {DSL_STREAM100x3_32_3DN2, "使用3个通道(最大采样率 100MHz)"},
    {DSL_STREAM50x32, "使用32个通道(最大采样率 50MHz)"},
    {DSL_STREAM100x30, "使用30个通道(最大采样率 100MHz)"},
    {DSL_STREAM250x12, "使用12个通道(最大采样率 250MHz)"},
    {DSL_STREAM125x16_16, "使用16个通道(最大采样率 125MHz)"},
    {DSL_STREAM250x12_16, "使用12个通道(最大采样率 250MHz)"},     
    {DSL_STREAM500x6, "使用6个通道(最大采样率 500MHz)"},
    {DSL_STREAM1000x3, "使用3个通道(最大采样率 1GHz)"},
    // LA Buffer
    {DSL_BUFFER100x16, "使用通道 0~15 (最大采样率 100MHz)"},
    {DSL_BUFFER200x8, "使用通道 0~7 (最大采样率 200MHz)"},
    {DSL_BUFFER400x4, "使用通道 0~3 (最大采样率 400MHz)"},
    {DSL_BUFFER250x32, "使用通道 0~31 (最大采样率 250MHz)"},
    {DSL_BUFFER500x16, "使用通道 0~15 (最大采样率 500MHz)"},
    {DSL_BUFFER1000x8, "使用通道 0~7 (最大采样率 1GHz)"},

    // DAQ
    {DSL_ANALOG10x2, "使用通道 0~1 (最大采样率 10MHz)"},
    {DSL_ANALOG10x2_500, "使用通道 0~1 (最大采样率 10MHz)"},

    // OSC
    {DSL_DSO200x2, "使用通道 0~1 (最大采样率 200MHz)"},     
    {DSL_DSO1000x2, "使用通道 0~1 (最大采样率 1GHz)"}
};

static const int32_t hwoptions[] = {
    SR_CONF_OPERATION_MODE,
    SR_CONF_BUFFER_OPTIONS,
    SR_CONF_THRESHOLD,
    SR_CONF_FILTER,
    SR_CONF_MAX_HEIGHT,
    SR_CONF_RLE_SUPPORT,
    SR_CONF_CLOCK_TYPE,
    SR_CONF_CLOCK_EDGE,
    SR_CONF_EXT_SAMPLERATE,
};

static const int32_t hwoptions_pro[] = {
    SR_CONF_OPERATION_MODE,
    SR_CONF_BUFFER_OPTIONS,
    SR_CONF_VTH,
    SR_CONF_FILTER,
    SR_CONF_MAX_HEIGHT,
    SR_CONF_RLE_SUPPORT,
    SR_CONF_CLOCK_TYPE,
    SR_CONF_CLOCK_EDGE,
    SR_CONF_EXT_SAMPLERATE,
};

static const int32_t sessions[] = {
    SR_CONF_MAX_HEIGHT,
    SR_CONF_OPERATION_MODE,
    SR_CONF_BUFFER_OPTIONS,
    SR_CONF_CHANNEL_MODE,
    SR_CONF_SAMPLERATE,
    SR_CONF_LIMIT_SAMPLES,
    SR_CONF_RLE_SUPPORT,
    SR_CONF_CLOCK_TYPE,
    SR_CONF_CLOCK_EDGE,
    SR_CONF_THRESHOLD,
    SR_CONF_FILTER,
    SR_CONF_TRIGGER_SLOPE,
    SR_CONF_TRIGGER_SOURCE,
    SR_CONF_HORIZ_TRIGGERPOS,
    SR_CONF_TRIGGER_HOLDOFF,
    SR_CONF_TRIGGER_MARGIN,
    SR_CONF_EXT_SAMPLERATE,
};

static const int32_t sessions_pro[] = {
    SR_CONF_MAX_HEIGHT,
    SR_CONF_OPERATION_MODE,
    SR_CONF_BUFFER_OPTIONS,
    SR_CONF_CHANNEL_MODE,
    SR_CONF_SAMPLERATE,
    SR_CONF_LIMIT_SAMPLES,
    SR_CONF_RLE_SUPPORT,
    SR_CONF_CLOCK_TYPE,
    SR_CONF_CLOCK_EDGE,
    SR_CONF_VTH,
    SR_CONF_FILTER,
    SR_CONF_TRIGGER_SLOPE,
    SR_CONF_TRIGGER_SOURCE,
	SR_CONF_TRIGGER_CHANNEL,
    SR_CONF_HORIZ_TRIGGERPOS,
    SR_CONF_TRIGGER_HOLDOFF,
    SR_CONF_TRIGGER_MARGIN,
    SR_CONF_EXT_SAMPLERATE,
};

SR_PRIV struct sr_dev_driver DSLogic_driver_info;
static struct sr_dev_driver *di = &DSLogic_driver_info;

static struct DSL_context *DSLogic_dev_new(const struct DSL_profile *prof)
{
    struct DSL_context *devc;
    unsigned int i;

    assert(prof);

    if (!(devc = g_try_malloc0(sizeof(struct DSL_context)))) {
        sr_err("Device context malloc failed.");
		return NULL;
	}
    memset(devc, 0, sizeof(struct DSL_context));

    for (i = 0; i < ARRAY_SIZE(channel_modes); i++){
        if(channel_modes[i].id != i)
            assert(0);
    }

    devc->channel = NULL;
    devc->profile = prof;
	devc->fw_updated = 0;
    devc->cur_samplerate = prof->dev_caps.default_samplerate;
    devc->limit_samples = prof->dev_caps.default_samplelimit;
    devc->clock_type = FALSE;
    devc->clock_edge = FALSE;
    devc->rle_mode = FALSE;
    devc->instant = FALSE;
	devc->op_mode = LO_OP_STREAM;
    devc->test_mode = SR_TEST_NONE;
	devc->ch_mode = prof->dev_caps.default_channelmode;
    devc->stream = (devc->op_mode == LO_OP_STREAM);
    devc->buf_options = SR_BUF_UPLOAD;
    devc->th_level = SR_TH_3V3;
    devc->vth = 1.0;
    devc->filter = SR_FILTER_NONE;
    devc->timebase = 10000;
    devc->trigger_slope = DSO_TRIGGER_RISING;
    devc->trigger_source = DSO_TRIGGER_AUTO;
    devc->trigger_hpos = 0x0;
    devc->trigger_hrate = 0;
    devc->trigger_holdoff = 0;
    devc->zero = FALSE;
    devc->zero_branch = FALSE;
    devc->zero_comb_fgain = FALSE;
    devc->zero_comb = FALSE;
    devc->status = DSL_FINISH;
    devc->is_loop = 0;

    devc->mstatus_valid = FALSE;
    devc->data_lock = FALSE;
    devc->max_height = 0;
    devc->trigger_margin = 8;
    devc->trigger_channel = 0;

    devc->ext_samplerate = 1000000;
    dsl_adjust_samplerate(devc);

	return devc;
}

static int init(struct sr_context *sr_ctx)
{
    return std_hw_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
    struct DSL_context *devc;
    struct sr_dev_inst *sdi;
    struct sr_usb_dev_inst *usb;
    struct sr_config *src;
    const struct DSL_profile *prof;
	GSList *l, *devices, *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
    libusb_device *device_handle = NULL;
    int ret, i, j;
	const char *conn;
    enum libusb_speed usb_speed;
    struct sr_usb_dev_inst *usb_dev_info;
    uint8_t bus;
    uint8_t address;
    int isProduct;
    int num;
    int is_speed_not_match;

	drvc = di->priv;
    num = 0;
    is_speed_not_match = 0;
 
    if (options != NULL)
        sr_info("Scan DSLogic device with options.");
    else 
        sr_info("Scan DSLogic device...");

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
        case SR_CONF_CONN:
            conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn){
        sr_info("Find usb device with connect config.");
        conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
    }
	else
		conn_devices = NULL;

    /* Find all DSLogic compatible devices and upload firmware to them. */
	devices = NULL;
    devlist = NULL;
    
    libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
    
    if (devlist == NULL){
        sr_info("%s: Failed to call libusb_get_device_list(), it returns a null list.", __func__);
        return NULL;
    }
    
	for (i = 0; devlist[i]; i++) 
    {
        device_handle = devlist[i];

		if (conn) {
			usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(device_handle)
					&& usb->address == libusb_get_device_address(device_handle))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		if ((ret = libusb_get_device_descriptor(device_handle, &des)) != 0) {
            sr_warn("Failed to get device descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}
        
        // The vendor id is not right.
        if (des.idVendor != DS_VENDOR_ID)
            continue;

        usb_speed = libusb_get_device_speed(device_handle);
        if ((usb_speed != LIBUSB_SPEED_HIGH) && (usb_speed != LIBUSB_SPEED_SUPER)){
            sr_info("scan(): The idVendor is right, but the usb speed is too low, speed type:%d", usb_speed);
            is_speed_not_match = 1;
            continue;
        }

        /* Check manufactory id and product id, and speed type. */
		prof = NULL;
        for (j = 0; supported_DSLogic[j].vid; j++) 
        {
            if (des.idVendor == supported_DSLogic[j].vid &&
                des.idProduct == supported_DSLogic[j].pid){
                    if (usb_speed == supported_DSLogic[j].usb_speed) {
                        prof = &supported_DSLogic[j];
                        break;
                    }
			}
		}

		/* Skip if the device was not found. */
		if (prof == NULL){ 
              isProduct = 0;

               //Mybe is a dscope device.
              for (j = 0; supported_DSCope[j].vid; j++) 
                {
                    if (des.idVendor == supported_DSCope[j].vid &&
                        des.idProduct == supported_DSCope[j].pid &&
                        usb_speed == supported_DSCope[j].usb_speed) {
                        isProduct = 1;
                        break;
                    }
                }

            if (isProduct == 0){
                sr_info("scan(): The profile is not matched, idVendor:%02X, idProduct:%02X",
                    des.idVendor,
                    des.idProduct);
            }

            continue;
        }
        
        if (sr_usb_device_is_exists(device_handle)){
            sr_detail("Device is exists, handle: %p", device_handle);
            continue;
        }
        
        bus = libusb_get_bus_number(device_handle);
        address = libusb_get_device_address(device_handle);
        sr_info("Found a new device,handle:%p", device_handle);
 
        if (dsl_check_conf_profile(device_handle)) {
			/* Already has the firmware, so fix the new address. */
            sr_info("Found a DSLogic device,name:\"%s\",handle:%p, pid:%02x", 
                prof->model,device_handle, prof->pid);

            devc = DSLogic_dev_new(prof);
            if (devc == NULL){
                break;
            }
            
            sdi = sr_dev_inst_new(channel_modes[devc->ch_mode].mode, SR_ST_INITIALIZING,
                                prof->vendor, prof->model, prof->model_version);
            if (sdi == NULL) {
                g_free(devc);
                break;
            }

            sdi->priv = devc;
            sdi->driver = di;
            sdi->dev_type = DEV_TYPE_USB;
            sdi->handle = (ds_device_handle)device_handle;
    
            /* Fill in probelist according to this device's profile. */
            if (dsl_setup_probes(sdi, channel_modes[devc->ch_mode].num) != SR_OK){
                sr_err("dsl_setup_probes() error");
                dev_destroy(sdi);
                break;
            }
           
            usb_dev_info = sr_usb_dev_inst_new(bus, address);
            usb_dev_info->usb_dev = device_handle;
            sdi->conn = usb_dev_info;
            sdi->status = SR_ST_INACTIVE; 

            devices = g_slist_append(devices, sdi);
            num++;   
		}
        else {
            char *firmware;
            char *res_path = DS_RES_PATH;
            if (!(firmware = g_try_malloc0(strlen(res_path)+strlen(prof->firmware) + 5))) {
                sr_err("Firmware path malloc error!");
                break;
            }
            strcpy(firmware, res_path);
            strcat(firmware, "/");
            strcat(firmware, prof->firmware);

            sr_info("Install firmware bin file, device:\"%s\", file:\"%s\"", prof->model, firmware);

            if (ezusb_upload_firmware(device_handle, USB_CONFIGURATION, firmware) != SR_OK){
                sr_err("Firmware upload failed for device %s.", prof->model);
            }

            g_free(firmware);

            libusb_unref_device(device_handle);
#ifdef _WIN32
            libusb_unref_device(device_handle);
#endif

            sr_info("Waitting for device reconnect, name:\"%s\"", prof->model);
		}
	}

	libusb_free_device_list(devlist, 0);

    if (conn_devices){
        g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
    }

    sr_info("Found new DSLogic device count: %d", num);

    if (is_speed_not_match){
        post_message_callback(DS_EV_DEVICE_SPEED_NOT_MATCH);
    }

	return devices;
}

static const GSList *dev_mode_list(const struct sr_dev_inst *sdi)
{
    return dsl_mode_list(sdi);
}

static uint64_t dso_cmd_gen(const struct sr_dev_inst *sdi, struct sr_channel* ch, int id)
{
    struct DSL_context *devc;
    uint64_t cmd = 0;
    int channel_cnt = 0;
    GSList *l;
    struct sr_channel *en_probe = ch;

    devc = sdi->priv;

    switch (id) {
    case SR_CONF_PROBE_VDIV:
    case SR_CONF_PROBE_EN:
    case SR_CONF_TIMEBASE:
    case SR_CONF_PROBE_COUPLING:
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *probe = (struct sr_channel *)l->data;
            if (probe->enabled) {
                channel_cnt += probe->index + 0x1;
                en_probe = probe;
            }
        }
        if (channel_cnt == 0)
            return 0x0;

        //  --VDBS
        if (channel_cnt != 1)
            en_probe = ch;
        switch(en_probe->vdiv){
        case 5:     cmd += 0x247000; break;
        case 10:    cmd += 0x23D000; break;
        case 20:    cmd += 0x22F000; break;
        case 50:    cmd += 0x21C800; break;
        case 100:   cmd += 0x20E800; break;
        case 200:   cmd += 0x200800; break;
        case 500:   cmd += 0x2F000; break;
        case 1000:  cmd += 0x21100; break;
        case 2000:  cmd += 0x13000; break;
        case 5000:  cmd += 0x00800; break;
        default: cmd += 0x21100; break;
        }
        // --DC/AC
        if (channel_cnt == 1) {
            for (l = sdi->channels; l; l = l->next) {
                struct sr_channel *probe = (struct sr_channel *)l->data;
                if (probe->coupling == SR_AC_COUPLING)
                    cmd += 0x100000;
                break;
            }
        } else {
            if(ch->coupling == SR_AC_COUPLING)
                cmd += 0x100000;
        }

        // --Channel
        if (sdi->mode != LOGIC) {
            if(channel_cnt == 1)
                cmd += 0xC00000;
            else if(ch->index == 0)
                cmd += 0x400000;
            else if(ch->index == 1)
                cmd += 0x800000;
            else
                cmd += 0x000000;
        }

        // --Header
        cmd += 0x55000000;
        break;
    case SR_CONF_SAMPLERATE:
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *probe = (struct sr_channel *)l->data;
            channel_cnt += probe->enabled;
        }
        cmd += 0x18;
        uint32_t divider = (uint32_t)ceil(channel_modes[devc->ch_mode].max_samplerate * 1.0  / devc->cur_samplerate / channel_cnt);
        cmd += divider << 8;
        break;
    case SR_CONF_HORIZ_TRIGGERPOS:
        cmd += 0x20;
        cmd += devc->trigger_hpos << 8;
        break;
    case SR_CONF_TRIGGER_SLOPE:
        cmd += 0x28;
        cmd += devc->trigger_slope << 8;
        break;
    case SR_CONF_TRIGGER_SOURCE:
        cmd += 0x30;
        cmd += devc->trigger_source << 8;
        break;
    case SR_CONF_TRIGGER_VALUE:
        cmd += 0x38;
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *probe = (struct sr_channel *)l->data;
            cmd += probe->trig_value << (8 * (probe->index + 1));
        }
        break;
    case SR_CONF_TRIGGER_MARGIN:
        cmd += 0x40;
        cmd += ((uint64_t)devc->trigger_margin << 8);
        break;
    case SR_CONF_TRIGGER_HOLDOFF:
        cmd += 0x58;
        cmd += devc->trigger_holdoff << 8;
        break;
    case SR_CONF_DSO_SYNC:
        cmd = 0xa5a5a500;
        break;
    default:
        cmd = 0xFFFFFFFF;
    }

    return cmd;
}

static int dso_init(const struct sr_dev_inst *sdi)
{
    int ret;
    GSList *l;

    for(l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, probe, SR_CONF_PROBE_COUPLING));
        if (ret != SR_OK) {
            sr_err("DSO set coupling of channel %d command failed!", probe->index);
            return ret;
        }
        ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, probe, SR_CONF_PROBE_VDIV));
        if (ret != SR_OK) {
            sr_err("Set VDIV of channel %d command failed!", probe->index);
            return ret;
        }
    }

    ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, 0, SR_CONF_SAMPLERATE));
    if (ret != SR_OK) {
        sr_err("Set Sample Rate command failed!");
        return ret;
    }
    ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_HORIZ_TRIGGERPOS));
    if (ret != SR_OK) {
        sr_err("Set Horiz Trigger Position command failed!");
        return ret;
    }
    ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_HOLDOFF));
    if (ret != SR_OK) {
        sr_err("Set Trigger Holdoff Time command failed!");
        return ret;
    }
    ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_SLOPE));
    if (ret != SR_OK) {
        sr_err("Set Trigger Slope command failed!");
        return ret;
    }
    ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_SOURCE));
    if (ret != SR_OK) {
        sr_err("Set Trigger Source command failed!");
        return ret;
    }
    ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_VALUE));
    if (ret != SR_OK) {
        sr_err("Set Trigger Value command failed!");
        return ret;
    }
    ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_MARGIN));
    if (ret != SR_OK) {
        sr_err("Set Trigger Margin command failed!");
        return ret;
    }
    return ret;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
                      const struct sr_channel *ch,
                      const struct sr_channel_group *cg)
{
    struct DSL_context *devc = sdi->priv;
    int ret;

    assert(sdi);
    assert(sdi->priv);

    ret = dsl_config_get(id, data, sdi, ch, cg);
    if (ret != SR_OK) {
        switch (id) {
        case SR_CONF_OPERATION_MODE:
            *data = g_variant_new_int16(devc->op_mode);
            break;
        case SR_CONF_FILTER:
            *data = g_variant_new_int16(devc->filter);
            break;
        case SR_CONF_RLE:
            *data = g_variant_new_boolean(devc->rle_mode);
            break;
		case SR_CONF_TEST:
            *data = g_variant_new_boolean(devc->test_mode != SR_TEST_NONE);
            break;
        case SR_CONF_WAIT_UPLOAD:
            if (devc->buf_options == SR_BUF_UPLOAD &&
                devc->status == DSL_START) {
                devc->status = DSL_ABORT;
                dsl_wr_reg(sdi, CTR0_ADDR, bmFORCE_STOP);
                *data = g_variant_new_boolean(TRUE);
            } else {
                *data = g_variant_new_boolean(FALSE);
            }
            break;
        case SR_CONF_BUFFER_OPTIONS:
            *data = g_variant_new_int16(devc->buf_options);
            break;
        case SR_CONF_CHANNEL_MODE:
            *data = g_variant_new_int16(devc->ch_mode);
            break;
        case SR_CONF_MAX_HEIGHT:
            *data = g_variant_new_string(maxHeights[devc->max_height]);
            break;
        case SR_CONF_MAX_HEIGHT_VALUE:
            *data = g_variant_new_byte(devc->max_height);
            break;
        case SR_CONF_THRESHOLD:
            *data = g_variant_new_int16(devc->th_level);
            break;
        case SR_CONF_VTH:
            *data = g_variant_new_double(devc->vth);
            break;
        case SR_CONF_STREAM:
            *data = g_variant_new_boolean(devc->stream);
            break;
        case SR_CONF_MAX_DSO_SAMPLERATE:
            *data = g_variant_new_uint64(channel_modes[devc->ch_mode].max_samplerate);
            break;
        case SR_CONF_MAX_DSO_SAMPLELIMITS:
            *data = g_variant_new_uint64(devc->profile->dev_caps.dso_depth);
            break;
        case SR_CONF_HW_DEPTH:
            *data = g_variant_new_uint64(dsl_channel_depth(sdi));
            break;
        case SR_CONF_VLD_CH_NUM:
            *data = g_variant_new_int16(channel_modes[devc->ch_mode].vld_num);
            break;
        case SR_CONF_TOTAL_CH_NUM:
            *data = g_variant_new_int16(devc->profile->dev_caps.total_ch_num);
            break;
        case SR_CONF_EXT_SAMPLERATE:
            *data = g_variant_new_uint64(devc->ext_samplerate);
            break;
        default:
            return SR_ERR_NA;
        }
    }

    return SR_OK;
}

static int config_set(int id, GVariant *data, struct sr_dev_inst *sdi,
                      struct sr_channel *ch,
                      struct sr_channel_group *cg )
{
    struct DSL_context *devc;
    const char *stropt;
    int ret, num_probes = 0;
    struct sr_usb_dev_inst *usb;
    unsigned int i;
    int nv;

    assert(sdi);
    assert(sdi->priv);

    (void)cg;

    if (sdi->status != SR_ST_ACTIVE) {
        sr_err("%s: Device is not opened.", __func__);
        return SR_ERR;
    }
    //sr_info("key:%d", id);

	devc = sdi->priv;
    usb = sdi->conn;

    ret = dsl_config_set(id, data, sdi, ch, cg);
    if (ret == SR_OK)
        return ret;

    ret = SR_OK;
    if (id == SR_CONF_CLOCK_TYPE) {
        devc->clock_type = g_variant_get_boolean(data);
    } 
    else if (id == SR_CONF_RLE_SUPPORT) {
        devc->rle_support = g_variant_get_boolean(data);
    } 
    else if (id == SR_CONF_CLOCK_EDGE) {
        devc->clock_edge = g_variant_get_boolean(data);
    } 
    else if (id == SR_CONF_LIMIT_SAMPLES) {
        devc->limit_samples = g_variant_get_uint64(data);
    } 
    else if (id == SR_CONF_PROBE_VDIV) {
        ch->vdiv = g_variant_get_uint64(data);
        if (sdi->mode != LOGIC) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, ch, SR_CONF_PROBE_VDIV));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting VDIV of channel %d to %d mv",
                __func__, ch->index, ch->vdiv);
        else
            sr_dbg("%s: setting VDIV of channel %d to %d mv failed",
                __func__, ch->index, ch->vdiv);
    } 
    else if (id == SR_CONF_PROBE_FACTOR) {
        ch->vfactor = g_variant_get_uint64(data);
        sr_dbg("%s: setting Factor of channel %d to %d", __func__,
               ch->index, ch->vfactor);
    } 
    else if (id == SR_CONF_TIMEBASE) {
        devc->timebase = g_variant_get_uint64(data);
    } 
    else if (id == SR_CONF_PROBE_COUPLING) {
        ch->coupling = g_variant_get_byte(data);
        if (ch->coupling == SR_GND_COUPLING)
            ch->coupling = SR_DC_COUPLING;
        if (sdi->mode != LOGIC) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, ch, SR_CONF_PROBE_COUPLING));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting AC COUPLING of channel %d to %d",
                __func__, ch->index, ch->coupling);
        else
            sr_dbg("%s: setting AC COUPLING of channel %d to %d failed",
                __func__, ch->index, ch->coupling);
    } 
    else if (id == SR_CONF_TRIGGER_SLOPE) {
        devc->trigger_slope = g_variant_get_byte(data);
        if (sdi->mode == DSO) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_SLOPE));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting DSO Trigger Slope to %d",
                __func__, devc->trigger_slope);
        else
            sr_dbg("%s: setting DSO Trigger Slope to %d failed",
                __func__, devc->trigger_slope);
    } 
    else if (id == SR_CONF_TRIGGER_VALUE) {
        ch->trig_value = g_variant_get_byte(data);
        if (sdi->mode == DSO) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, ch, SR_CONF_TRIGGER_VALUE));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting channel %d Trigger Value to %d",
                __func__, ch->index, ch->trig_value);
        else
            sr_dbg("%s: setting DSO Trigger Value to %d failed",
                __func__, ch->index, ch->trig_value);
    } 
    else if (id == SR_CONF_HORIZ_TRIGGERPOS) {
        if (sdi->mode == DSO) {
            devc->trigger_hrate = g_variant_get_byte(data);
            //devc->trigger_hpos = devc->trigger_hrate * dsl_en_ch_num(sdi) * devc->limit_samples / 200.0;
            /*
             * devc->trigger_hpos should be updated before each acquisition
             * because the samplelimits may changed
             */
            devc->trigger_hpos = devc->trigger_hrate * dsl_en_ch_num(sdi) * devc->limit_samples / 200.0;
            if ((ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_HORIZ_TRIGGERPOS))) == SR_OK)
                sr_dbg("%s: setting DSO Horiz Trigger Position to %d",
                    __func__, devc->trigger_hpos);
            else
                sr_dbg("%s: setting DSO Horiz Trigger Position to %d failed",
                    __func__, devc->trigger_hpos);
        } else {
            devc->trigger_hpos = g_variant_get_byte(data) * devc->limit_samples / 100.0;
        }
    } 
    else if (id == SR_CONF_TRIGGER_HOLDOFF) {
        devc->trigger_holdoff = g_variant_get_uint64(data);
        if (sdi->mode == DSO) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_HOLDOFF));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting Trigger Holdoff Time to %d",
                __func__, devc->trigger_holdoff);
        else
            sr_dbg("%s: setting Trigger Holdoff Time to %d failed",
                __func__, devc->trigger_holdoff);
    } 
    else if (id == SR_CONF_TRIGGER_MARGIN) {
        devc->trigger_margin = g_variant_get_byte(data);
        if (sdi->mode == DSO) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_MARGIN));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting Trigger Margin to %d",
                __func__, devc->trigger_margin);
        else
            sr_dbg("%s: setting Trigger Margin to %d failed",
                __func__, devc->trigger_margin);
    } 
    else if (id == SR_CONF_SAMPLERATE) {
        if (devc->test_mode == SR_TEST_NONE) {
            devc->cur_samplerate = g_variant_get_uint64(data);
            if(sdi->mode != LOGIC) {
                ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, 0, SR_CONF_SAMPLERATE));
            }
        }
    } 
    else if (id == SR_CONF_FILTER) {
        nv = g_variant_get_int16(data);
        if (nv == SR_FILTER_NONE || nv == SR_FILTER_1T) 
            devc->filter = nv;
        else 
            ret = SR_ERR;
        
        sr_dbg("%s: setting filter to %d",
            __func__, devc->filter);
    } 
    else if (id == SR_CONF_RLE) {
        devc->rle_mode = g_variant_get_boolean(data);
    } 
    else if (id == SR_CONF_INSTANT) {
        if (sdi->mode == DSO) {
            devc->instant = g_variant_get_boolean(data);
            if (dsl_en_ch_num(sdi) != 0) {
                if (devc->instant)
                    devc->limit_samples = devc->profile->dev_caps.hw_depth / dsl_en_ch_num(sdi);
                else
                    devc->limit_samples = devc->profile->dev_caps.dso_depth / dsl_en_ch_num(sdi);
            }
        }
    } 
    else if (id == SR_CONF_DEVICE_MODE) {
        sdi->mode = g_variant_get_int16(data);
        if (sdi->mode == LOGIC) {
            dsl_wr_reg(sdi, CTR0_ADDR, bmSCOPE_CLR);

            for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                if (channel_modes[i].mode == LOGIC &&
                    devc->profile->dev_caps.channels & (1 << i)) {
                    devc->ch_mode = channel_modes[i].id;
                    num_probes = channel_modes[i].num;
                    devc->stream = channel_modes[i].stream;
                    dsl_adjust_samplerate(devc);
                    break;
                }
            }
        } 
        else if (sdi->mode == DSO) {
            dsl_wr_reg(sdi, CTR0_ADDR, bmSCOPE_SET);
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_DSO_SYNC));
            if (ret != SR_OK)
                sr_dbg("%s: DSO configuration sync failed", __func__);

            for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                if (channel_modes[i].mode == DSO &&
                    devc->profile->dev_caps.channels & (1 << i)) {
                    devc->ch_mode = channel_modes[i].id;
                    num_probes = channel_modes[i].num;
                    devc->stream = channel_modes[i].stream;
                    devc->cur_samplerate = channel_modes[i].max_samplerate / num_probes;
                    dsl_adjust_samplerate(devc);
                    break;
                }
            }
            devc->limit_samples = devc->profile->dev_caps.dso_depth / num_probes;
        } 
        else if (sdi->mode == ANALOG)  {
            dsl_wr_reg(sdi, CTR0_ADDR, bmSCOPE_SET);
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_DSO_SYNC));
            if (ret != SR_OK)
                sr_dbg("%s: DAQ configuration sync failed", __func__);
            devc->op_mode = LO_OP_STREAM;
            devc->test_mode = SR_TEST_NONE;
            for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                if (channel_modes[i].mode == ANALOG &&
                    devc->profile->dev_caps.channels & (1 << i)) {
                    devc->ch_mode = channel_modes[i].id;
                    num_probes = channel_modes[i].num;
                    devc->stream = channel_modes[i].stream;
                    dsl_adjust_samplerate(devc);
                    break;
                }
            }
        } 
        else {
            ret = SR_ERR;
        }
        assert(num_probes != 0);

        sr_dev_probes_free(sdi);
        dsl_setup_probes(sdi, num_probes);
        sr_dbg("%s: setting mode to %d", __func__, sdi->mode);

        if (sdi->mode != LOGIC) {
            dso_init(sdi);
        }
    } 
    else if (id == SR_CONF_OPERATION_MODE) {
        nv = g_variant_get_int16(data);

        if (sdi->mode == LOGIC && devc->op_mode != nv) 
        {
            if (nv == LO_OP_BUFFER) {
                devc->op_mode = LO_OP_BUFFER;
                devc->test_mode = SR_TEST_NONE;
                devc->stream = FALSE;
                
                for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                    if (channel_modes[i].mode == LOGIC &&
                        channel_modes[i].stream == devc->stream &&
                        devc->profile->dev_caps.channels & (1 << i)) {
                        devc->ch_mode = channel_modes[i].id;
                        break;
                    }
                }
            } 
            else if (nv == LO_OP_STREAM) {
                devc->op_mode = LO_OP_STREAM;
                devc->test_mode = SR_TEST_NONE;
                devc->stream = TRUE;
                
                for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                    if (channel_modes[i].mode == LOGIC &&
                        channel_modes[i].stream == devc->stream &&
                        devc->profile->dev_caps.channels & (1 << i)) {
                        devc->ch_mode = channel_modes[i].id;
                        break;
                    }
                }
            } 
            else if (nv == LO_OP_INTEST) {
                devc->op_mode = LO_OP_INTEST;
                devc->test_mode = SR_TEST_INTERNAL;
                devc->ch_mode = devc->profile->dev_caps.intest_channel;
                devc->stream = !(devc->profile->dev_caps.feature_caps & CAPS_FEATURE_BUF);
            } 
            else {
                ret = SR_ERR;
            }

            dsl_adjust_probes(sdi, channel_modes[devc->ch_mode].num);
            dsl_adjust_samplerate(devc);

            if (devc->op_mode == LO_OP_INTEST) {
                devc->cur_samplerate = devc->stream ? channel_modes[devc->ch_mode].max_samplerate / 10 :
                                                      SR_MHZ(100);
                devc->limit_samples = devc->stream ? devc->cur_samplerate * 3 :
                                                     devc->profile->dev_caps.hw_depth / dsl_en_ch_num(sdi);
            }
        }
        sr_dbg("%s: setting pattern to %d",
            __func__, devc->op_mode);
    } 
    else if (id == SR_CONF_BUFFER_OPTIONS) {
        nv = g_variant_get_int16(data);
        if (sdi->mode == LOGIC && (nv == SR_BUF_STOP || nv == SR_BUF_UPLOAD)) {
              devc->buf_options = nv;
        }
    } 
    else if (id == SR_CONF_CHANNEL_MODE) {
        nv = g_variant_get_int16(data);
        if (sdi->mode == LOGIC) {
            for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                if (devc->profile->dev_caps.channels & (1 << i)) {
                    if ((int)channel_modes[i].id == nv) {
                        devc->ch_mode = (enum DSL_CHANNEL_ID)nv;
                        break;
                    }
                }
            }
            dsl_adjust_probes(sdi, channel_modes[devc->ch_mode].num);
            dsl_adjust_samplerate(devc);
        }
        sr_info("%s: setting channel mode to %d",
            __func__, devc->ch_mode);
    }
    else if (id == SR_CONF_THRESHOLD) {    
        nv = g_variant_get_int16(data);    
        if (sdi->mode == LOGIC && nv != devc->th_level) {
            if (nv == SR_TH_3V3) 
                devc->th_level = SR_TH_3V3;
            else if (nv == SR_TH_5V0)
                devc->th_level = SR_TH_5V0;
            else
                return SR_ERR;

            char *fpga_bit;
            char *res_path = DS_RES_PATH;
            if (!(fpga_bit = g_try_malloc0(strlen(res_path) + strlen(devc->profile->fpga_bit33) + 5))) {
                sr_err("fpag_bit path malloc error!");
                return SR_ERR_MALLOC;
            }
            strcpy(fpga_bit, res_path);
            strcat(fpga_bit, "/");

            switch(devc->th_level) 
            {
            case SR_TH_3V3:
                strcat(fpga_bit, devc->profile->fpga_bit33);
                break;
            case SR_TH_5V0:
                strcat(fpga_bit, devc->profile->fpga_bit50);
                break;
            default:
                return SR_ERR;
            }

            ret = dsl_fpga_config(usb->devhdl, fpga_bit);
            g_free(fpga_bit);
            if (ret != SR_OK) {
                sr_err("Configure FPGA failed!");
            }
            
            sr_dbg("%s: setting threshold to %d",
                __func__, devc->th_level);
        }
    } 
    else if (id == SR_CONF_VTH) {
        devc->vth = g_variant_get_double(data);

        if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_MAX25_VTH)
            ret = dsl_wr_reg(sdi, VTH_ADDR, (uint8_t)(devc->vth/3.3*(1.0/2.0)*255));
        else
            ret = dsl_wr_reg(sdi, VTH_ADDR, (uint8_t)(devc->vth/3.3*(1.5/2.5)*255));
    } 
    else if (id == SR_CONF_MAX_HEIGHT) {
        stropt = g_variant_get_string(data, NULL);
        for (i = 0; i < ARRAY_SIZE(maxHeights); i++) {
            if (!strcmp(stropt, maxHeights[i])) {
                devc->max_height = i;
                break;
            }
        }
        sr_dbg("%s: setting Signal Max Height to %d",
            __func__, devc->max_height);
    } 
    else if (id == SR_CONF_PROBE_EN) {
        ch->enabled = g_variant_get_boolean(data);
        if (sdi->mode == DSO) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, ch, SR_CONF_PROBE_EN));
            uint16_t channel_cnt = 0;
            GSList *l;
            for (l = sdi->channels; l; l = l->next) {
                struct sr_channel *probe = (struct sr_channel *)l->data;
                channel_cnt += probe->enabled;
            }
            if (channel_cnt != 0)
                ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, 0, SR_CONF_SAMPLERATE));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting ENABLE of channel %d to %d",
                __func__, ch->index, ch->enabled);
        else
            sr_dbg("%s: setting ENABLE of channel %d to %d",
                __func__, ch->index, ch->enabled);
    } 
    else if (id == SR_CONF_PROBE_OFFSET) {
        ch->offset = g_variant_get_uint16(data);
        sr_dbg("%s: setting OFFSET of channel %d to %d", __func__,
               ch->index, ch->offset);
    } 
    else if (id == SR_CONF_TRIGGER_SOURCE) {
        devc->trigger_source = g_variant_get_byte(data);
        if (sdi->mode == DSO) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_SOURCE));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting DSO Trigger Source to %d",
                __func__, devc->trigger_source);
        else
            sr_dbg("%s: setting DSO Trigger Source to %d failed",
                __func__, devc->trigger_source);
    } 
    else if (id == SR_CONF_TRIGGER_CHANNEL) {
        devc->trigger_source = (g_variant_get_byte(data) << 4) + (devc->trigger_source & 0x0f);
        if (sdi->mode == DSO) {
            ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_TRIGGER_SOURCE));
        }
        if (ret == SR_OK)
            sr_dbg("%s: setting DSO Trigger Source to %d",
                __func__, devc->trigger_source);
        else
            sr_dbg("%s: setting DSO Trigger Source to %d failed",
                __func__, devc->trigger_source);
    } 
    else if (id == SR_CONF_STREAM) {
        devc->stream = g_variant_get_boolean(data);
    }
    else if (id == SR_CONF_LOOP_MODE){
        devc->is_loop = g_variant_get_boolean(data);
        sr_info("Set device loop mode:%d", devc->is_loop);
    }
    else if (id == SR_CONF_EXT_SAMPLERATE) {
        devc->ext_samplerate = g_variant_get_uint64(data);
        sr_info("Set device ext samplerate: %d", devc->ext_samplerate);
    }
    else {
        ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
                       const struct sr_channel_group *cg)
{
    struct DSL_context *devc;
    unsigned int i;
    int num;

    assert(sdi);
    assert(sdi->priv);

    (void)cg;
    devc = sdi->priv;

    if (dsl_config_list(key, data, sdi, cg) == SR_OK) {
        return SR_OK;
    }

	switch (key) {
    case SR_CONF_DEVICE_OPTIONS:
        if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_VTH)
            *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
                    hwoptions_pro, ARRAY_SIZE(hwoptions_pro)*sizeof(int32_t), TRUE, NULL, NULL);
        else
            *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
                    hwoptions, ARRAY_SIZE(hwoptions)*sizeof(int32_t), TRUE, NULL, NULL);
        break;

    case SR_CONF_DEVICE_SESSIONS:
        if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_VTH)
            *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
                    sessions_pro, ARRAY_SIZE(sessions_pro)*sizeof(int32_t), TRUE, NULL, NULL);
        else
            *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
                    sessions, ARRAY_SIZE(sessions)*sizeof(int32_t), TRUE, NULL, NULL);
        break;

    case SR_CONF_OPERATION_MODE:
        *data = g_variant_new_uint64((uint64_t)&opmode_list);
        break;

    case SR_CONF_BUFFER_OPTIONS:
        *data = g_variant_new_uint64((uint64_t)&bufoption_list);
        break;

    case SR_CONF_CHANNEL_MODE: 
        num = 0;
        for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
            if (channel_modes[i].stream == devc->stream && devc->profile->dev_caps.channels & (1 << i))
            {
                if (devc->test_mode != SR_TEST_NONE && devc->profile->dev_caps.intest_channel != channel_modes[i].id)
                    continue;
                
                if (num == CHANNEL_MODE_LIST_LEN - 1){
                    assert(0);
                }
                channel_mode_list[num].id = channel_modes[i].id;
                channel_mode_list[num].name = channel_modes[i].descr;
                num++;
            }
        }
        channel_mode_list[num].id = -1;
        channel_mode_list[num].name = NULL;
        *data = g_variant_new_uint64((uint64_t)&channel_mode_list);
        break;

    case SR_CONF_THRESHOLD: 
        *data = g_variant_new_uint64((uint64_t)&threshold_list);
        break;

    case SR_CONF_FILTER: 
        *data = g_variant_new_uint64((uint64_t)&filter_list);
        break;

    case SR_CONF_MAX_HEIGHT:
        *data = g_variant_new_strv(maxHeights, ARRAY_SIZE(maxHeights));
        break;

    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
    gboolean fpga_done;
    int ret;
    struct DSL_context *devc;

    devc = sdi->priv;

    if ((ret = dsl_dev_open(di, sdi, &fpga_done)) == SR_OK) {        
        if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_MAX25_VTH)
            ret = dsl_wr_reg(sdi, VTH_ADDR, (uint8_t)(devc->vth/3.3*(1.0/2.0)*255));
        else
            ret = dsl_wr_reg(sdi, VTH_ADDR, (uint8_t)(devc->vth/3.3*(1.5/2.5)*255));
        
        if (ret != SR_OK){
            sr_err("%s:%d, Failed to call dsl_wr_reg()!",
                        __func__, __LINE__);
            ds_set_last_error(SR_ERR_DEVICE_USB_IO_ERROR);
            return ret;
        }

        // set threshold
        if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ADF4360) {
            dsl_config_adc(sdi, adc_clk_init_500m);
        }
    }

    return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
    int ret;
    ret = dsl_dev_close(sdi);
    return ret;
}

static int dev_destroy(struct sr_dev_inst *sdi)
{
    return dsl_destroy_device(sdi);
}

static int cleanup(void)
{  
    safe_free(di->priv);
    return SR_OK;
}

static void remove_sources(struct DSL_context *devc)
{
    int i;
    sr_info("%s: remove fds from polling", __func__);
    /* Remove fds from polling. */
    for (i = 0; devc->usbfd[i] != -1; i++)
        sr_source_remove(devc->usbfd[i]);
    g_free(devc->usbfd);
}

static void report_overflow(struct DSL_context *devc)
{
    struct sr_datafeed_packet packet;
    struct sr_dev_inst *sdi = devc->cb_data;

    packet.status = SR_PKT_OK;
    packet.type = SR_DF_OVERFLOW;
    packet.payload = NULL;
    ds_data_forward(sdi, &packet);
}

static int receive_data(int fd, int revents, const struct sr_dev_inst *sdi)
{
    int completed = 0;
    struct timeval tv;
    struct drv_context *drvc;
    struct DSL_context *devc;
    struct sr_usb_dev_inst *usb;
    struct ctl_rd_cmd rd_cmd;
    uint8_t hw_info;
    int ret;

    (void)fd;
    (void)revents;

    drvc = di->priv;
    devc = sdi->priv;
    usb = sdi->conn;

    tv.tv_sec = tv.tv_usec = 0;
    libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv, &completed);

    if (devc->trf_completed)
        devc->empty_poll_count = 0;
    else
        devc->empty_poll_count++;

    // --
    // progress check
    // must before overflow check (1ch@10K)
    // --
    if ((devc->empty_poll_count > MAX_EMPTY_POLL) && (devc->status == DSL_START)) {
        devc->mstatus.captured_cnt0 = 0;
        rd_cmd.header.dest = DSL_CTL_I2C_STATUS;
        rd_cmd.header.offset = 0;
        rd_cmd.header.size = 4;
        rd_cmd.data = (unsigned char*)&devc->mstatus;
        if ((ret = command_ctl_rd(usb->devhdl, rd_cmd)) != SR_OK)
            sr_err("Failed to get progress infos.");

        devc->empty_poll_count = 0;
    }

    // overflow check
    if (devc->stream) {
        if (devc->empty_poll_count > MAX_EMPTY_POLL) {
            rd_cmd.header.dest = DSL_CTL_HW_STATUS;
            rd_cmd.header.size = 1;
            hw_info = 0;
            rd_cmd.data = &hw_info;
            if ((ret = command_ctl_rd(usb->devhdl, rd_cmd)) != SR_OK)
                sr_err("Failed to get hardware infos.");
            else
                devc->overflow = (hw_info & bmSYS_OVERFLOW) != 0;

            if (devc->overflow)
                report_overflow(devc);

            devc->empty_poll_count = 0;
        }
    }

    if (devc->status == DSL_FINISH) {
        /* Remove polling */
        remove_sources(devc);
    }

    devc->trf_completed = 0;
    return TRUE;
}

static int dev_acquisition_start(struct sr_dev_inst *sdi, void *cb_data)
{
    (void)cb_data;

    struct DSL_context *devc;
    struct sr_usb_dev_inst *usb;
    struct drv_context *drvc;
    const struct libusb_pollfd **lupfd;
	unsigned int i;
    int ret;
    struct ctl_wr_cmd wr_cmd;

    if (sdi->status != SR_ST_ACTIVE){
        ds_set_last_error(SR_ERR_DEVICE_CLOSED);
        return SR_ERR_DEVICE_CLOSED;
    }

    drvc = di->priv;
    devc = sdi->priv;
    usb = sdi->conn;

    //devc->cb_data = cb_data;
    devc->cb_data = sdi;
    devc->num_samples = 0;
    devc->num_bytes = 0;
    devc->empty_transfer_count = 0;
    devc->empty_poll_count = 0;
    devc->status = DSL_INIT;
    devc->num_transfers = 0;
    devc->submitted_transfers = 0;
    devc->actual_samples = (devc->limit_samples + SAMPLES_ALIGN) & ~SAMPLES_ALIGN;
    devc->actual_bytes = devc->actual_samples / DSLOGIC_ATOMIC_SAMPLES * dsl_en_ch_num(sdi) * DSLOGIC_ATOMIC_SIZE;
	devc->abort = FALSE;
    devc->mstatus_valid = FALSE;
    devc->mstatus.captured_cnt0 = 0;
    devc->mstatus.captured_cnt1 = 0;
    devc->mstatus.captured_cnt2 = 0;
    devc->mstatus.captured_cnt3 = 0;
    devc->mstatus.trig_hit = 0;
    devc->overflow = FALSE;

	/* Configures devc->trigger_* and devc->sample_wide */
    if (dsl_configure_probes(sdi) != SR_OK) {
        sr_err("%s: Failed to configure probes.", __func__);
        return SR_ERR;
	}

    /* Stop Previous GPIF acquisition */
    wr_cmd.header.dest = DSL_CTL_STOP;
    wr_cmd.header.size = 0;
    
    if ((ret = command_ctl_wr(usb->devhdl, wr_cmd)) != SR_OK) {
        sr_err("%s: Stop DSLogic acquisition failed!", __func__);
        return ret;
    } 
    else {
        sr_info("%s: Stop Previous DSLogic acquisition!", __func__);
    }

    /* Setting FPGA before acquisition start*/
    if ((ret = dsl_fpga_arm(sdi)) != SR_OK) {
        sr_err("%s: Arm FPGA failed!", __func__);
        return ret;
    }

    /*
     * settings must be updated before acquisition
     */
    if (sdi->mode == DSO) {
        devc->trigger_hpos =  devc->trigger_hrate * dsl_en_ch_num(sdi) * devc->limit_samples / 200.0;
        ret = dsl_wr_dso(sdi, dso_cmd_gen(sdi, NULL, SR_CONF_HORIZ_TRIGGERPOS));
        if (ret != SR_OK)
            sr_dbg("%s: setting DSO Horiz Trigger Position to %d failed", __func__, devc->trigger_hpos);
    }

    /* setup and submit usb transfer */
    if ((ret = dsl_start_transfers(devc->cb_data)) != SR_OK) {
        sr_err("%s: Could not submit usb transfer"
               "(%d)%d", __func__, ret, errno);
        return ret;
    }

    /* setup callback function for data transfer */
    lupfd = libusb_get_pollfds(drvc->sr_ctx->libusb_ctx);
    for (i = 0; lupfd[i]; i++);

    if (!(devc->usbfd = g_try_malloc0(sizeof(struct libusb_pollfd) * (i + 1)))){
        sr_err("%s,ERROR:failed to alloc memory.", __func__);
    	return SR_ERR;
    }

    for (i = 0; lupfd[i]; i++) {
        sr_source_add(lupfd[i]->fd, lupfd[i]->events,
                  dsl_get_timeout(sdi), receive_data, sdi);
        devc->usbfd[i] = lupfd[i]->fd;
    }
    devc->usbfd[i] = -1;
    g_free(lupfd);

    wr_cmd.header.dest = DSL_CTL_START;
    wr_cmd.header.size = 0;
    if ((ret = command_ctl_wr(usb->devhdl, wr_cmd)) != SR_OK) {
        devc->status = DSL_ERROR;
        devc->abort = TRUE;
        return ret;
    }
    devc->status = DSL_START;

	/* Send header packet to the session bus. */
    //std_session_send_df_header(cb_data, LOG_PREFIX);
    std_session_send_df_header(sdi, LOG_PREFIX);

    return SR_OK;
}

static int dev_acquisition_stop(const struct sr_dev_inst *sdi, void *cb_data)
{
    int ret = dsl_dev_acquisition_stop(sdi, cb_data);
    return ret;
}

static int dev_status_get(const struct sr_dev_inst *sdi, struct sr_status *status, gboolean prg)
{
    int ret = dsl_dev_status_get(sdi, status, prg);
    return ret;
}

SR_PRIV int sr_dslogic_option_value_to_code(const struct sr_dev_inst *sdi, int config_id, const char *value)
{
    int num;
    unsigned int i;
    struct DSL_context *devc;
    const char *list_text;
    const char *cn_name;

    assert(sdi);
    assert(sdi->priv);
    
    devc = sdi->priv;
    list_text = NULL;
    cn_name = NULL;

    if (config_id == SR_CONF_CHANNEL_MODE)
    {
         for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {

                 list_text = channel_modes[i].descr;

                if (devc->profile->dev_caps.channels & (1 << i)) 
                { 
                    if (strcmp(list_text, value) == 0)
                        return channel_modes[i].id;

                    if (i < ARRAY_SIZE(channel_mode_cn_map)){
                        if ((int)channel_modes[i].id != channel_mode_cn_map[i].id)
                            assert(0);
                        
                        cn_name = channel_mode_cn_map[i].name;
                        if (strcmp(cn_name, value) == 0)
                            return channel_modes[i].id;
                    }
                }
            }

        sr_err("Unkown text value:%s, config id:%d", value, config_id);
        return -1;
    }

    num = sizeof(lang_text_map) / sizeof(lang_text_map[0]);
    return sr_option_value_to_code(config_id, value, &lang_text_map[0], num);
}

SR_PRIV struct sr_dev_driver DSLogic_driver_info = {
    .name = "DSLogic",
    .longname = "DSLogic (generic driver for DSLogic LA)",
	.api_version = 1,
    .driver_type = DRIVER_TYPE_HARDWARE,
	.init = init,
	.cleanup = cleanup,
	.scan = scan, 
    .dev_mode_list = dev_mode_list,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
    .dev_destroy = dev_destroy,
    .dev_status_get = dev_status_get,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
