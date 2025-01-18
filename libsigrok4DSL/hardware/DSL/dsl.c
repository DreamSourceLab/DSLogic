/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 DreamSourceLab <dreamsourcelab@dreamsourcelab.com>
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
#include "command.h"
#include "dsl.h"
#include "../../log.h"

#include <math.h>
#include <assert.h>
#include <sys/stat.h>

#undef LOG_PREFIX
#define LOG_PREFIX "dsl: "

SR_PRIV int dsl_secuReset(const struct sr_dev_inst *sdi);
SR_PRIV int dsl_secuWrite(const struct sr_dev_inst *sdi, uint16_t cmd, uint16_t din);
SR_PRIV gboolean dsl_isSecuReady(const struct sr_dev_inst *sdi);
SR_PRIV gboolean dsl_isSecuPass(const struct sr_dev_inst *sdi);
SR_PRIV uint16_t dsl_secuRead(const struct sr_dev_inst *sdi);
static unsigned int to_bytes_per_ms(struct DSL_context *devc);

static const int32_t probeOptions[] = {
    SR_CONF_PROBE_COUPLING,
    SR_CONF_PROBE_VDIV,
    SR_CONF_PROBE_MAP_DEFAULT,
    SR_CONF_PROBE_MAP_UNIT,
    SR_CONF_PROBE_MAP_MIN,
    SR_CONF_PROBE_MAP_MAX,
};

static const uint8_t probeCoupling[] = {
    SR_DC_COUPLING,
    SR_AC_COUPLING,
};

static const char *probeMapUnits[] = {
    "V",
    "A",
    "℃",
    "℉",
    "g",
    "m",
    "m/s",
};

static const char *probe_names[] = {
    "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",
    "8",  "9",  "10", "11", "12", "13", "14", "15",
    "16", "17", "18", "19", "20", "21", "22", "23",
    "24", "25", "26", "27", "28", "29", "30", "31",
    NULL,
};

SR_PRIV void dsl_probe_init(struct sr_dev_inst *sdi)
{
    unsigned int i, j;
    GSList *l;
    struct DSL_context *devc = sdi->priv;

    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        probe->bits = channel_modes[devc->ch_mode].unit_bits;
        probe->vdiv = 1000;
        probe->vfactor = 1;
        probe->cali_fgain0 = 1;
        probe->cali_fgain1 = 1;
        probe->cali_fgain2 = 1;
        probe->cali_fgain3 = 1;
        probe->cali_comb_fgain0 = 1;
        probe->cali_comb_fgain1 = 1;
        probe->cali_comb_fgain2 = 1;
        probe->cali_comb_fgain3 = 1;
        probe->offset = (1 << (probe->bits - 1));
        probe->coupling = SR_DC_COUPLING;
        probe->trig_value = (1 << (probe->bits - 1));
        probe->vpos_trans = devc->profile->dev_caps.default_pwmtrans;
        probe->comb_comp = devc->profile->dev_caps.default_comb_comp;
        probe->digi_fgain = 0;

        probe->map_default = TRUE;
        probe->map_unit = probeMapUnits[0];
        probe->map_min = -(probe->vdiv * probe->vfactor * DS_CONF_DSO_VDIVS / 2000.0);
        probe->map_max = probe->vdiv * probe->vfactor * DS_CONF_DSO_VDIVS / 2000.0;

        if (devc->profile->dev_caps.vdivs && probe->vga_ptr == NULL) {

            for (i = 0; devc->profile->dev_caps.vdivs[i]; i++){
            }

            probe->vga_ptr = g_try_malloc0((i+1)*sizeof(struct DSL_vga));
            if (probe->vga_ptr == NULL){
                sr_err("%s,ERROR:failed to alloc memory.", __func__);
                return;
            }

            for (i = 0; devc->profile->dev_caps.vdivs[i]; i++) {
                (probe->vga_ptr + i)->id = devc->profile->dev_caps.vga_id;
                (probe->vga_ptr + i)->key = devc->profile->dev_caps.vdivs[i];
                for (j = 0; j < ARRAY_SIZE(vga_defaults); j++) {
                    if (vga_defaults[j].id == devc->profile->dev_caps.vga_id &&
                        vga_defaults[j].key == devc->profile->dev_caps.vdivs[i]) {
                        (probe->vga_ptr+i)->vgain = vga_defaults[j].vgain;
                        (probe->vga_ptr+i)->preoff = vga_defaults[j].preoff;
                        (probe->vga_ptr + i)->preoff_comp = 0;
                    }
                }
            }

            // end flag must have
            (probe->vga_ptr + i)->id = 0;
            (probe->vga_ptr + i)->key = 0;
            (probe->vga_ptr+i)->vgain = 0;
            (probe->vga_ptr+i)->preoff = 0;
            (probe->vga_ptr + i)->preoff_comp = 0;
        }
    }
}

SR_PRIV int dsl_setup_probes(struct sr_dev_inst *sdi, int num_probes)
{
    uint16_t j;
    struct sr_channel *probe;
    struct DSL_context *devc = sdi->priv;

    for (j = 0; j < num_probes; j++) {
        if (!(probe = sr_channel_new(j, channel_modes[devc->ch_mode].type,
                                   TRUE, probe_names[j])))
            return SR_ERR;
        sdi->channels = g_slist_append(sdi->channels, probe);
    }
    dsl_probe_init(sdi);
    return SR_OK;
}

SR_PRIV int dsl_adjust_probes(struct sr_dev_inst *sdi, int num_probes)
{
    uint16_t j;
    struct sr_channel *probe;
    struct DSL_context *devc = sdi->priv;
    GSList *l;

    assert(num_probes > 0);

    j = g_slist_length(sdi->channels);
    while(j < num_probes) {
        if (!(probe = sr_channel_new(j, channel_modes[devc->ch_mode].type,
                                   TRUE, probe_names[j])))
            return SR_ERR;
        sdi->channels = g_slist_append(sdi->channels, probe);
        j++;
    }

    while(j > num_probes) {
        sdi->channels = g_slist_delete_link(sdi->channels, g_slist_last(sdi->channels));
        j--;
    }

    for(l = sdi->channels; l; l = l->next) {
        probe = (struct sr_channel *)l->data;
        probe->enabled = TRUE;
        probe->type = channel_modes[devc->ch_mode].type;
    }
    return SR_OK;
}

SR_PRIV const GSList *dsl_mode_list(const struct sr_dev_inst *sdi)
{
    struct DSL_context *devc;
    GSList *l = NULL;
    unsigned int i; 

    devc = sdi->priv;
    for (i = 0; i < ARRAY_SIZE(sr_mode_list); i++) {
        if (devc->profile->dev_caps.mode_caps & (1 << i))
            l = g_slist_append(l, (gpointer)&sr_mode_list[i]);    
    }

    return l;
}

SR_PRIV void dsl_adjust_samplerate(struct DSL_context *devc)
{
    int i;
    for (i = 0; devc->profile->dev_caps.samplerates[i]; i++) {
        if (devc->profile->dev_caps.samplerates[i] >
                channel_modes[devc->ch_mode].max_samplerate)
            break;
    }
    devc->samplerates_max_index = i-1;

    for (i = 0; devc->profile->dev_caps.samplerates[i]; i++) {
        if (devc->profile->dev_caps.samplerates[i] >=
                channel_modes[devc->ch_mode].min_samplerate)
            break;
    }
    devc->samplerates_min_index = i;

    assert(devc->samplerates_max_index >= devc->samplerates_min_index);

    if (devc->cur_samplerate > devc->profile->dev_caps.samplerates[devc->samplerates_max_index])
        devc->cur_samplerate = devc->profile->dev_caps.samplerates[devc->samplerates_max_index];

    if (devc->cur_samplerate < devc->profile->dev_caps.samplerates[devc->samplerates_min_index])
        devc->cur_samplerate = devc->profile->dev_caps.samplerates[devc->samplerates_min_index];
}

SR_PRIV int dsl_en_ch_num(const struct sr_dev_inst *sdi)
{
    GSList *l;
    int channel_en_cnt = 0;

    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        channel_en_cnt += probe->enabled;
    }
    channel_en_cnt += (channel_en_cnt == 0);

    return channel_en_cnt;
}

/**
 * Check the USB configuration to determine if this is an dsl device.
 *
 * @return TRUE if the device's configuration profile match dsl hardware
 *         configuration, FALSE otherwise.
 */
SR_PRIV gboolean dsl_check_conf_profile(libusb_device *dev)
{
    struct libusb_device_descriptor des;
    struct libusb_device_handle *hdl;
    int ret;
    gboolean bSucess;
    unsigned char strdesc[64];

    hdl = NULL;
    bSucess = FALSE;
    ret = 0;

    while (!bSucess) {
        /* Assume the FW has not been loaded, unless proven wrong. */
        if ((ret = libusb_get_device_descriptor(dev, &des)) < 0){
            sr_err("%s:%d, Failed to get device descriptor: %s", 
			    __func__, __LINE__, libusb_error_name(ret));
            break;
        }

        if ((ret = libusb_open(dev, &hdl)) < 0){
            sr_err("%s:%d, Failed to open device: %s", 
			    __func__, __LINE__, libusb_error_name(ret));
            // Mybe the device is busy, add it to list.
            return TRUE;
        }

        if ((ret = libusb_get_string_descriptor_ascii(hdl,
                des.iManufacturer, strdesc, sizeof(strdesc))) < 0){
            sr_err("%s:%d, Failed to get device descriptor ascii: %s", 
			    __func__, __LINE__, libusb_error_name(ret));
            break;
        }

        if (strncmp((const char *)strdesc, "DreamSourceLab", 14))
            break;

        if ((ret = libusb_get_string_descriptor_ascii(hdl,
                des.iProduct, strdesc, sizeof(strdesc))) < 0){
            sr_err("%s:%d, Failed to get device descriptor ascii: %s", 
			    __func__, __LINE__, libusb_error_name(ret));
            break;
        }

        if (strncmp((const char *)strdesc, "USB-based DSL Instrument v2", 27))
            break;

        /* If we made it here, it must be an dsl device. */
        bSucess = TRUE;
    }

    if (hdl){
        libusb_close(hdl);
    }

    return bSucess;
}

static int hw_dev_open(struct sr_dev_driver *di, struct sr_dev_inst *sdi)
{
    (void)di;
    
    libusb_device *dev_handel=NULL;
    struct sr_usb_dev_inst *usb;
    struct version_info vi;
    int ret;
    struct ctl_rd_cmd rd_cmd;
    uint8_t rd_cmd_data[2];

    usb = sdi->conn;
  
    if (usb->usb_dev == NULL){
        sr_err("hw_dev_open(), usb->usb_dev is null.");
        return SR_ERR;
    }

    if (sdi->status == SR_ST_ACTIVE) {
        /* Device is already in use. */
        sr_detail("The usb device is opened, handle:%p", usb->usb_dev);
        return SR_OK;
    }

    if (sdi->status == SR_ST_INITIALIZING) {
        sr_info("The device instance is still boosting.");        
    }
    dev_handel = usb->usb_dev;

    sr_info("Open usb device instance, handle: %p", dev_handel);

    ret = libusb_open(dev_handel, &usb->devhdl);
    if (ret != LIBUSB_SUCCESS){
        sr_err("%s:%d, Failed to open device: %s, handle:%p",
                __func__, __LINE__, libusb_error_name(ret), dev_handel);
        
        if (ret == LIBUSB_ERROR_NOT_SUPPORTED)
            ds_set_last_error(SR_ERR_DEVICE_NO_DRIVER);
        else
            ds_set_last_error(SR_ERR_DEVICE_IS_EXCLUSIVE);
        return SR_ERR;
    }
    //sr_info("------------Open returns the libusb_device_handle: %p, struct:%p", usb->devhdl, usb);

    if (usb->address == 0xff){
        /*
        * First time we touch this device after FW
        * upload, so we don't know the address yet.
        */
        usb->address = libusb_get_device_address(dev_handel);
    }

    rd_cmd.header.dest = DSL_CTL_FW_VERSION;
    rd_cmd.header.size = 2;
    rd_cmd.data = rd_cmd_data;

    if ((ret = command_ctl_rd(usb->devhdl, rd_cmd)) != SR_OK) {
        sr_err("%s:%d, Failed to get firmware version.",
                __func__, __LINE__);
        ds_set_last_error(SR_ERR_DEVICE_USB_IO_ERROR);
        return ret;
    }

    vi.major = rd_cmd_data[0];
    vi.minor = rd_cmd_data[1];

    /*
     * Different versions may have incompatible issue,
     * Mark for up level process.
    */
    if (vi.major != DSL_REQUIRED_VERSION_MAJOR) {
        sr_err("Expected firmware version %d.%d, "
                "got %d.%d.", DSL_REQUIRED_VERSION_MAJOR, DSL_REQUIRED_VERSION_MINOR,
                vi.major, vi.minor);
        ds_set_last_error(SR_ERR_DEVICE_FIRMWARE_VERSION_LOW);
        sdi->status = SR_ST_INCOMPATIBLE;
    }
    else {
        sdi->status = SR_ST_ACTIVE;
    }

    sr_info("Opened device %p on %d.%d, "
            "interface %d, firmware %d.%d.",
            usb->usb_dev, usb->bus, usb->address,
            USB_INTERFACE, vi.major, vi.minor);

    if ((sdi->status != SR_ST_ACTIVE) &&
        (sdi->status != SR_ST_INCOMPATIBLE)){
        assert(0);
    }
    
    return SR_OK;
}

SR_PRIV int dsl_configure_probes(const struct sr_dev_inst *sdi)
{
    struct DSL_context *devc;
    struct sr_channel *probe;
    GSList *l;
    int probe_bit, stage, i;
    char *tc;

    devc = sdi->priv;
    for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
        devc->trigger_mask[i] = 0;
        devc->trigger_value[i] = 0;
    }

    stage = -1;
    for (l = sdi->channels; l; l = l->next) {
        probe = (struct sr_channel *)l->data;
        if (probe->enabled == FALSE)
            continue;

        probe_bit = 1 << (probe->index);
        if (!(probe->trigger))
            continue;

        stage = 0;
        for (tc = probe->trigger; *tc; tc++) {
            devc->trigger_mask[stage] |= probe_bit;
            if (*tc == '1')
                devc->trigger_value[stage] |= probe_bit;
            stage++;
            if (stage > NUM_TRIGGER_STAGES)
                return SR_ERR;
        }
    }

    return SR_OK;
}

SR_PRIV uint64_t dsl_channel_depth(const struct sr_dev_inst *sdi)
{
    struct DSL_context *devc = sdi->priv;
    int ch_num = dsl_en_ch_num(sdi);
    return (devc->profile->dev_caps.hw_depth / (ch_num ? ch_num : 1)) & ~SAMPLES_ALIGN;
}

SR_PRIV int dsl_hdl_version(const struct sr_dev_inst *sdi, uint8_t *value)
{
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct ctl_rd_cmd rd_cmd;
    int ret;
    uint8_t rdata[HDL_VERSION_ADDR+1];

    usb = sdi->conn;
    hdl = usb->devhdl;

    rd_cmd.header.dest = DSL_CTL_I2C_STATUS;
    rd_cmd.header.offset = 0;
    rd_cmd.header.size = HDL_VERSION_ADDR+1;
    rd_cmd.data = rdata;
    if ((ret = command_ctl_rd(hdl, rd_cmd)) != SR_OK) {
        sr_err("Sent DSL_CTL_I2C_STATUS command failed.");
        return SR_ERR;
    }

    *value = rdata[HDL_VERSION_ADDR];
    return SR_OK;
}

SR_PRIV int dsl_wr_reg(const struct sr_dev_inst *sdi, uint8_t addr, uint8_t value)
{
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct ctl_wr_cmd wr_cmd;
    int ret;

    usb = sdi->conn;
    hdl = usb->devhdl;

    wr_cmd.header.dest = DSL_CTL_I2C_REG;
    wr_cmd.header.offset = addr;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = value;
    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK) {
        sr_err("Sent DSL_CTL_I2C_REG command failed.");
        return SR_ERR;
    }

    return SR_OK;
}

SR_PRIV int dsl_rd_reg(const struct sr_dev_inst *sdi, uint8_t addr, uint8_t *value)
{
    struct sr_usb_dev_inst *usb;
    struct ctl_rd_cmd rd_cmd;
    int ret;

    usb = sdi->conn;

    rd_cmd.header.dest = DSL_CTL_I2C_STATUS;
    rd_cmd.header.offset = addr;
    rd_cmd.header.size = 1;
    rd_cmd.data = value;
    if ((ret = command_ctl_rd(usb->devhdl, rd_cmd)) != SR_OK) {
        sr_err("Sent DSL_CTL_I2C_STATUS read command failed.");
        return SR_ERR;
    }

    return SR_OK;
}

SR_PRIV int dsl_wr_ext(const struct sr_dev_inst *sdi, uint8_t addr, uint8_t value)
{
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct ctl_wr_cmd wr_cmd;
    struct DSL_context *devc = sdi->priv;
    uint8_t rdata;
    int ret;

    if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_POGOPIN) {
        usb = sdi->conn;
        hdl = usb->devhdl;

        wr_cmd.header.dest = DSL_CTL_I2C_EXT;
        wr_cmd.header.offset = addr;
        wr_cmd.header.size = 1;
        wr_cmd.data[0] = value;
        if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK) {
            sr_err("Sent DSL_CTL_I2C_EXT command failed.");
            return SR_ERR;
        }
    } else {
        // write addr + wr
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_TXR_OFF, EI2C_AWR);
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STA | bmEI2C_WR);
        // check done
        ret = dsl_rd_reg(sdi, EI2C_ADDR+EI2C_SR_OFF, &rdata);
        if (rdata & bmEI2C_RXNACK) {
            ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STO | bmEI2C_WR);
            return SR_ERR;
        }

        // write offset + wr
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_TXR_OFF, addr);
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_WR);
        // check done
        ret = dsl_rd_reg(sdi, EI2C_ADDR+EI2C_SR_OFF, &rdata);
        if (rdata & bmEI2C_RXNACK) {
            ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STO | bmEI2C_WR);
            return SR_ERR;
        }

        // write value + wr
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_TXR_OFF, value);
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STO | bmEI2C_WR);
        // check done
        ret = dsl_rd_reg(sdi, EI2C_ADDR+EI2C_SR_OFF, &rdata);
        if (rdata & bmEI2C_RXNACK) {
            ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STO | bmEI2C_WR);
            return SR_ERR;
        }
    }

    return ret;
}

SR_PRIV int dsl_rd_ext(const struct sr_dev_inst *sdi, unsigned char *ctx, uint16_t addr, uint8_t len)
{
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct ctl_rd_cmd rd_cmd;
    struct DSL_context *devc = sdi->priv;
    uint8_t rdata;
    int ret;

    if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_POGOPIN) {
        usb = sdi->conn;
        hdl = usb->devhdl;

        rd_cmd.header.dest = DSL_CTL_I2C_EXT;
        rd_cmd.header.size = len;
        rd_cmd.header.offset = addr;
        rd_cmd.data = ctx;
        if ((ret = command_ctl_rd(hdl, rd_cmd)) != SR_OK) {
            sr_err("Sent DSL_CTL_I2C_EXT read command failed.");
            return SR_ERR;
        }
    } else {
        // write addr + wr
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_TXR_OFF, EI2C_AWR);
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STA | bmEI2C_WR);
        // check done
        ret = dsl_rd_reg(sdi, EI2C_ADDR+EI2C_SR_OFF, &rdata);
        if (rdata & bmEI2C_RXNACK) {
            ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STO | bmEI2C_WR);
            return SR_ERR;
        }

        // write offset + wr
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_TXR_OFF, addr);
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_WR);
        // check done
        ret = dsl_rd_reg(sdi, EI2C_ADDR+EI2C_SR_OFF, &rdata);
        if (rdata & bmEI2C_RXNACK) {
            ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STO | bmEI2C_WR);
            return SR_ERR;
        }

        // write read addr + wr
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_TXR_OFF, EI2C_ARD);
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STA | bmEI2C_WR);
        // check done
        ret = dsl_rd_reg(sdi, EI2C_ADDR+EI2C_SR_OFF, &rdata);
        if (rdata & bmEI2C_RXNACK) {
            ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STO | bmEI2C_WR);
            return SR_ERR;
        }

        while(--len) {
            ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_RD);
            ret = dsl_rd_reg(sdi, EI2C_ADDR+EI2C_RXR_OFF, ctx);
            ctx++;
        }
        ret = dsl_wr_reg(sdi, EI2C_ADDR+EI2C_CR_OFF, bmEI2C_STO | bmEI2C_RD | bmEI2C_NACK);
        ret = dsl_rd_reg(sdi, EI2C_ADDR+EI2C_RXR_OFF, ctx);
    }

    return ret;
}

SR_PRIV int dsl_wr_dso(const struct sr_dev_inst *sdi, uint64_t cmd)
{
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct ctl_wr_cmd wr_cmd;
    int ret;

    usb = sdi->conn;
    hdl = usb->devhdl;

    wr_cmd.header.dest = DSL_CTL_I2C_DSO;
    wr_cmd.header.offset = 0;
    wr_cmd.header.size = 8;
    wr_cmd.data[0] = (uint8_t)cmd;
    wr_cmd.data[1] = (uint8_t)(cmd >> 8);
    wr_cmd.data[2] = (uint8_t)(cmd >> 16);
    wr_cmd.data[3] = (uint8_t)(cmd >> 24);
    wr_cmd.data[4] = (uint8_t)(cmd >> 32);
    wr_cmd.data[5] = (uint8_t)(cmd >> 40);
    wr_cmd.data[6] = (uint8_t)(cmd >> 48);
    wr_cmd.data[7] = (uint8_t)(cmd >> 56);
    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK) {
        sr_err("Sent DSL_CTL_I2C_DSO command failed.");
        return SR_ERR;
    }

    return SR_OK;
}

SR_PRIV int dsl_wr_nvm(const struct sr_dev_inst *sdi, unsigned char *ctx, uint16_t addr, uint8_t len)
{
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct ctl_wr_cmd wr_cmd;
    int ret;
    int i;

    usb = sdi->conn;
    hdl = usb->devhdl;

    wr_cmd.header.dest = DSL_CTL_NVM;
    wr_cmd.header.offset = addr;
    wr_cmd.header.size = len;
    for (i = 0; i < len; i++)
        wr_cmd.data[i] = *(ctx+i);
    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK) {
        sr_err("Sent DSL_CTL_NVM write command failed.");
        return SR_ERR;
    }

    return SR_OK;
}

SR_PRIV int dsl_rd_nvm(const struct sr_dev_inst *sdi, unsigned char *ctx, uint16_t addr, uint8_t len)
{
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct ctl_rd_cmd rd_cmd;
    int ret;

    usb = sdi->conn;
    hdl = usb->devhdl;

    rd_cmd.header.dest = DSL_CTL_NVM;
    rd_cmd.header.size = len;
    rd_cmd.header.offset = addr;
    rd_cmd.data = ctx;
    if ((ret = command_ctl_rd(hdl, rd_cmd)) != SR_OK) {
        sr_err("Sent DSL_CTL_NVM read command failed.");
        return SR_ERR;
    }

    return SR_OK;
}

SR_PRIV int dsl_config_adc(const struct sr_dev_inst *sdi, const struct DSL_adc_config *config)
{
    while(config->dest) {
        assert((config->cnt > 0) && (config->cnt <= 4));
        if (config->delay >0)
            g_usleep(config->delay*1000);
        for (int i = 0; i < config->cnt; i++) {
            dsl_wr_reg(sdi, config->dest, config->byte[i]);
        }
        config++;
    }
    return SR_OK;
}

SR_PRIV double dsl_adc_code2fgain(uint8_t code)
{
    double xcode = code & 0x40 ? -(~code & 0x3F) : code & 0x3F;
    return (1 + xcode / (1 << 13));
}

SR_PRIV uint8_t dsl_adc_fgain2code(double gain)
{
    int xratio = (gain - 1) * (1 << 13);
    uint8_t code = xratio > 63 ? 63 :
                   xratio > 0 ? xratio :
                   xratio < -63 ? 64 : ~(-xratio) & 0x7F;
    return code;
}

SR_PRIV int dsl_config_adc_fgain(const struct sr_dev_inst *sdi, uint8_t branch, double gain0, double gain1)
{
    dsl_wr_reg(sdi, ADCC_ADDR, 0x00);
    dsl_wr_reg(sdi, ADCC_ADDR, dsl_adc_fgain2code(gain0));
    dsl_wr_reg(sdi, ADCC_ADDR, dsl_adc_fgain2code(gain1));
    dsl_wr_reg(sdi, ADCC_ADDR, 0x34 + branch);

    return SR_OK;
}

SR_PRIV int dsl_config_fpga_fgain(const struct sr_dev_inst *sdi)
{
    GSList *l;
    int ret = SR_OK;

    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        if (probe->index == 0) {
            ret = dsl_wr_reg(sdi, ADCC_ADDR+3, (probe->digi_fgain & 0x00FF));
            ret = dsl_wr_reg(sdi, ADCC_ADDR+4, (probe->digi_fgain >> 8));
        } else if (probe->index == 1) {
            ret = dsl_wr_reg(sdi, ADCC_ADDR+5, (probe->digi_fgain & 0x00FF));
            ret = dsl_wr_reg(sdi, ADCC_ADDR+6, (probe->digi_fgain >>8));
        }
    }

    return ret;
}

SR_PRIV int dsl_skew_fpga_fgain(const struct sr_dev_inst *sdi, gboolean comb, double skew[])
{
    uint8_t fgain_up = 0;
    uint8_t fgain_dn = 0;
    GSList *l;
    gboolean tmp;
    int ret;

    for (int i = 0; i <= 7; i++) {
        tmp = (-skew[i] > 1.6*MAX_ACC_VARIANCE);
        fgain_up += (tmp << i);
    }

    if (comb) {
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *probe = (struct sr_channel *)l->data;
            if (probe->index == 0) {
                probe->digi_fgain |= (probe->digi_fgain & 0xFF00) + fgain_up;
                fgain_up = (probe->digi_fgain & 0x00FF);
                break;
            }
        }
        ret = dsl_wr_reg(sdi, ADCC_ADDR+3, fgain_up);
    } else {
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *probe = (struct sr_channel *)l->data;
            if (probe->index == 0) {
                probe->digi_fgain |= (fgain_up << 8) + (probe->digi_fgain & 0x00FF);
                fgain_up = (probe->digi_fgain>>8);
                break;
            }
        }
        ret = dsl_wr_reg(sdi, ADCC_ADDR+4, fgain_up);
    }

    for (int i = 0; i <= 7; i++) {
        tmp = (skew[i] > 1.6*MAX_ACC_VARIANCE);
        fgain_dn += (tmp << i);
    }

    if (comb) {
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *probe = (struct sr_channel *)l->data;
            if (probe->index == 1) {
                probe->digi_fgain |= (probe->digi_fgain & 0xFF00) + fgain_dn;
                fgain_dn = (probe->digi_fgain & 0x00FF);
                break;
            }
        }
        ret = dsl_wr_reg(sdi, ADCC_ADDR+5, fgain_dn);
    } else {
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *probe = (struct sr_channel *)l->data;
            if (probe->index == 1) {
                probe->digi_fgain |= (fgain_dn << 8) + (probe->digi_fgain & 0x00FF);
                fgain_dn = (probe->digi_fgain>>8);
                break;
            }
        }
        ret = dsl_wr_reg(sdi, ADCC_ADDR+6, fgain_dn);
    }

    return ret;
}

SR_PRIV int dsl_probe_cali_fgain(struct DSL_context *devc, struct sr_channel *probe, double mean, gboolean comb, gboolean reset)
{
    const double UPGAIN = 1.0077;
    const double DNGAIN = 0.9923;
    const double MDGAIN = 1;
    const double ignore_ratio = 2.0;
    const double ratio = 2.0;
    double drift;
    if (reset) {
        if (comb) {
            probe->cali_comb_fgain0 = MDGAIN;
            probe->cali_comb_fgain1 = MDGAIN;
            probe->cali_comb_fgain2 = MDGAIN;
            probe->cali_comb_fgain3 = MDGAIN;
        } else {
            probe->cali_fgain0 = MDGAIN;
            probe->cali_fgain1 = MDGAIN;
            probe->cali_fgain2 = MDGAIN;
            probe->cali_fgain3 = MDGAIN;
        }
    } else {
        if (comb) {
            /*
             * not consist with hmcad1511 datasheet
             *
             * byte order | acc_mean          | single channel branch (datasheet)
             * 0            ch0_acc_mean        1 (0->cali_comb_fgain0)
             * 1            ch1_acc_mean        6 (1->cali_comb_fgain1)
             * 2            ch0_acc_mean_p1     2 (0->cali_comb_fgain1)
             * 3            ch1_acc_mean_p1     5 (1->cali_comb_fgain0)
             * 4            ch0_acc_mean_p2     8 (1->cali_comb_fgain3)
             * 5            ch1_acc_mean_p2     3 (0->cali_comb_fgain2)
             * 6            ch0_acc_mean_p3     7 (1->cali_comb_fgain2)
             * 7            ch1_acc_mean_p3     4 (0->cali_comb_fgain3)
             */
            if (probe->index == 0) {
                drift = (devc->mstatus.ch0_acc_mean / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_comb_fgain0 /= (1 + drift);
                drift = (devc->mstatus.ch0_acc_mean_p1 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_comb_fgain1 /= (1 + drift);
                drift = (devc->mstatus.ch1_acc_mean_p2 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_comb_fgain2 /= (1 + drift);
                drift = (devc->mstatus.ch1_acc_mean_p3 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_comb_fgain3 /= (1 + drift);
            } else {
                drift = (devc->mstatus.ch1_acc_mean_p1 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_comb_fgain0 /= (1 + drift);
                drift = (devc->mstatus.ch1_acc_mean / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_comb_fgain1 /= (1 + drift);
                drift = (devc->mstatus.ch0_acc_mean_p3 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_comb_fgain2 /= (1 + drift);
                drift = (devc->mstatus.ch0_acc_mean_p2 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_comb_fgain3 /= (1 + drift);
            }

            probe->cali_comb_fgain0 = max(min(probe->cali_comb_fgain0, UPGAIN), DNGAIN);
            probe->cali_comb_fgain1 = max(min(probe->cali_comb_fgain1, UPGAIN), DNGAIN);
            probe->cali_comb_fgain2 = max(min(probe->cali_comb_fgain2, UPGAIN), DNGAIN);
            probe->cali_comb_fgain3 = max(min(probe->cali_comb_fgain3, UPGAIN), DNGAIN);
        } else {
            /*
             * byte order | acc_mean          | dual channel branch
             * 0            ch0_acc_mean        1 (0->cali_fgain0)
             * 1            ch0_acc_mean_p1     3 (0->cali_fgain2)
             * 2            ch0_acc_mean_p2     2 (0->cali_fgain1)
             * 3            ch0_acc_mean_p3     4 (0->cali_fgain3)
             * 4            ch1_acc_mean        5 (1->cali_fgain0)
             * 5            ch1_acc_mean_p1     7 (1->cali_fgain2)
             * 6            ch1_acc_mean_p2     6 (1->cali_fgain1)
             * 7            ch1_acc_mean_p3     8 (1->cali_fgain3)
             */
            if (probe->index == 0) {
                drift = (devc->mstatus.ch0_acc_mean / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_fgain0 /= (1 + drift);
                drift = (devc->mstatus.ch0_acc_mean_p2 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_fgain1 /= (1 + drift);
                drift = (devc->mstatus.ch0_acc_mean_p1 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_fgain2 /= (1 + drift);
                drift = (devc->mstatus.ch0_acc_mean_p3 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_fgain3 /= (1 + drift);
            } else {
                drift = (devc->mstatus.ch1_acc_mean / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_fgain0 /= (1 + drift);
                drift = (devc->mstatus.ch1_acc_mean_p2 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_fgain1 /= (1 + drift);
                drift = (devc->mstatus.ch1_acc_mean_p1 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_fgain2 /= (1 + drift);
                drift = (devc->mstatus.ch1_acc_mean_p3 / mean - 1) / ratio;
                if (fabs(drift) > MAX_ACC_VARIANCE / ignore_ratio)
                    probe->cali_fgain3 /= (1 + drift);
            }

            probe->cali_fgain0 = max(min(probe->cali_fgain0, UPGAIN), DNGAIN);
            probe->cali_fgain1 = max(min(probe->cali_fgain1, UPGAIN), DNGAIN);
            probe->cali_fgain2 = max(min(probe->cali_fgain2, UPGAIN), DNGAIN);
            probe->cali_fgain3 = max(min(probe->cali_fgain3, UPGAIN), DNGAIN);
        }
    }

    return SR_OK;
}

SR_PRIV gboolean dsl_probe_fgain_inrange(struct sr_channel *probe, gboolean comb, double skew[])
{
    const double UPGAIN = 1.0077;
    const double DNGAIN = 0.9923;

    if (comb) {
        if (probe->index == 0) {
            if (fabs(skew[0]) > MAX_ACC_VARIANCE && probe->cali_comb_fgain0 > DNGAIN && probe->cali_comb_fgain0 < UPGAIN)
                return TRUE;
            if (fabs(skew[1]) > MAX_ACC_VARIANCE && probe->cali_comb_fgain1 > DNGAIN && probe->cali_comb_fgain1 < UPGAIN)
                return TRUE;
            if (fabs(skew[6]) > MAX_ACC_VARIANCE && probe->cali_comb_fgain2 > DNGAIN && probe->cali_comb_fgain2 < UPGAIN)
                return TRUE;
            if (fabs(skew[7]) > MAX_ACC_VARIANCE && probe->cali_comb_fgain3 > DNGAIN && probe->cali_comb_fgain3 < UPGAIN)
                return TRUE;
        } else {
            if (fabs(skew[5]) > MAX_ACC_VARIANCE && probe->cali_comb_fgain0 > DNGAIN && probe->cali_comb_fgain0 < UPGAIN)
                return TRUE;
            if (fabs(skew[4]) > MAX_ACC_VARIANCE && probe->cali_comb_fgain1 > DNGAIN && probe->cali_comb_fgain1 < UPGAIN)
                return TRUE;
            if (fabs(skew[3]) > MAX_ACC_VARIANCE && probe->cali_comb_fgain2 > DNGAIN && probe->cali_comb_fgain2 < UPGAIN)
                return TRUE;
            if (fabs(skew[2]) > MAX_ACC_VARIANCE && probe->cali_comb_fgain3 > DNGAIN && probe->cali_comb_fgain3 < UPGAIN)
                return TRUE;
        }
    } else {
        if (probe->index == 0) {
            if (fabs(skew[0]) > MAX_ACC_VARIANCE && probe->cali_fgain0 > DNGAIN && probe->cali_fgain0 < UPGAIN)
                return TRUE;
            if (fabs(skew[2]) > MAX_ACC_VARIANCE && probe->cali_fgain1 > DNGAIN && probe->cali_fgain1 < UPGAIN)
                return TRUE;
            if (fabs(skew[1]) > MAX_ACC_VARIANCE && probe->cali_fgain2 > DNGAIN && probe->cali_fgain2 < UPGAIN)
                return TRUE;
            if (fabs(skew[3]) > MAX_ACC_VARIANCE && probe->cali_fgain3 > DNGAIN && probe->cali_fgain3 < UPGAIN)
                return TRUE;
        } else {
            if (fabs(skew[4]) > MAX_ACC_VARIANCE && probe->cali_fgain0 > DNGAIN && probe->cali_fgain0 < UPGAIN)
                return TRUE;
            if (fabs(skew[6]) > MAX_ACC_VARIANCE && probe->cali_fgain1 > DNGAIN && probe->cali_fgain1 < UPGAIN)
                return TRUE;
            if (fabs(skew[5]) > MAX_ACC_VARIANCE && probe->cali_fgain2 > DNGAIN && probe->cali_fgain2 < UPGAIN)
                return TRUE;
            if (fabs(skew[7]) > MAX_ACC_VARIANCE && probe->cali_fgain3 > DNGAIN && probe->cali_fgain3 < UPGAIN)
                return TRUE;
        }
    }

    return FALSE;
}

SR_PRIV int dsl_rd_probe(const struct sr_dev_inst *sdi, unsigned char *ctx, uint16_t addr, uint8_t len)
{
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct ctl_rd_cmd rd_cmd;
    int ret;

    usb = sdi->conn;
    hdl = usb->devhdl;

    rd_cmd.header.dest = DSL_CTL_I2C_PROBE;
    rd_cmd.header.size = len;
    rd_cmd.header.offset = addr;
    rd_cmd.data = ctx;
    if ((ret = command_ctl_rd(hdl, rd_cmd)) != SR_OK) {
        sr_err("Sent DSL_CTL_NVM read command failed.");
        return SR_ERR;
    }

    return SR_OK;
}

SR_PRIV int dsl_fpga_arm(const struct sr_dev_inst *sdi)
{
    struct DSL_context *devc;
    struct sr_usb_dev_inst *usb;
    struct libusb_device_handle *hdl;
    struct DSL_setting setting;
    struct DSL_setting_ext32 setting_ext32;
    int ret;
    int transferred;
    int i;
    GSList *l;
    uint32_t tmp_u32;
    uint64_t tmp_u64;
    const int ch_num = dsl_en_ch_num(sdi);
    uint32_t arm_size;
    struct ctl_wr_cmd wr_cmd;
    struct ctl_rd_cmd rd_cmd;
    uint8_t rd_cmd_data;
    gboolean qutr_trig;
    gboolean half_trig;

    devc = sdi->priv;
    usb = sdi->conn;
    hdl = usb->devhdl;

    setting.sync = 0xf5a5f5a5;
    setting.mode_header = 0x0001;
    setting.divider_header = 0x0102;
    setting.count_header = 0x0302;
    setting.trig_pos_header = 0x0502;
    setting.trig_glb_header = 0x0701;
    setting.dso_count_header = 0x0802;
    setting.ch_en_header = 0x0a02;
    setting.fgain_header = 0x0c01;
    setting.trig_header = 0x40a0;
    setting.end_sync = 0xfa5afa5a;

    setting_ext32.sync = 0xf5a5f5a5;
    setting_ext32.trig_header = 0x6060;
    setting_ext32.align_bytes = 0xffff;
    setting_ext32.end_sync = 0xfa5afa5a;

    if (trigger == NULL)
    {
        sr_err("Trigger have'nt inited.");
        return SR_ERR_CALL_STATUS;
    }

    // basic configuration
    setting.mode = (trigger->trigger_en << TRIG_EN_BIT) +
                   (devc->clock_type << CLK_TYPE_BIT) +
                   (devc->clock_edge << CLK_EDGE_BIT) +
                   (devc->rle_mode << RLE_MODE_BIT) +
                   ((sdi->mode == DSO) << DSO_MODE_BIT) +
                   ((devc->cur_samplerate == devc->profile->dev_caps.half_samplerate) << HALF_MODE_BIT) +
                   ((devc->cur_samplerate == devc->profile->dev_caps.quarter_samplerate) << QUAR_MODE_BIT) +
                   (((sdi->mode == ANALOG) || devc->is_loop) << ANALOG_MODE_BIT) +
                   ((devc->filter == SR_FILTER_1T) << FILTER_BIT) +
                   (devc->instant << INSTANT_BIT) +
                   ((to_bytes_per_ms(devc) < 1024) << SLOW_ACQ_BIT) +
                   ((trigger->trigger_mode == SERIAL_TRIGGER) << STRIG_MODE_BIT) +
                   ((devc->stream) << STREAM_MODE_BIT) +
                   ((devc->test_mode == SR_TEST_LOOPBACK) << LPB_TEST_BIT) +
                   ((devc->test_mode == SR_TEST_EXTERNAL) << EXT_TEST_BIT) +
                   ((devc->test_mode == SR_TEST_INTERNAL) << INT_TEST_BIT);

    // sample rate divider
    tmp_u32  = (sdi->mode == DSO) ? (uint32_t)ceil(channel_modes[devc->ch_mode].max_samplerate * 1.0 / devc->cur_samplerate / ch_num) :
               (sdi->mode == ANALOG) ? (uint32_t)ceil(channel_modes[devc->ch_mode].hw_max_samplerate * 1.0 / max(devc->cur_samplerate, channel_modes[devc->ch_mode].hw_min_samplerate)) :
                                       (uint32_t)ceil(channel_modes[devc->ch_mode].hw_max_samplerate * 1.0 / devc->cur_samplerate);
    devc->unit_pitch = ceil(channel_modes[devc->ch_mode].hw_min_samplerate * 1.0 / devc->cur_samplerate);
    setting.div_h = ((tmp_u32 >= channel_modes[devc->ch_mode].pre_div) ? channel_modes[devc->ch_mode].pre_div - 1U : tmp_u32 - 1U) << 8;
    tmp_u32 = (uint32_t)ceil(tmp_u32 * 1.0 / channel_modes[devc->ch_mode].pre_div);
    setting.div_l = tmp_u32 & 0x0000ffff;
    setting.div_h += tmp_u32 >> 16;

    // capture counter
    tmp_u64 = (sdi->mode == DSO) ? (devc->actual_samples / (channel_modes[devc->ch_mode].num / ch_num)) :
                                   (devc->actual_samples);
    tmp_u64 >>= 4; // hardware minimum unit 64
    setting.cnt_l = tmp_u64 & 0x0000ffff;
    setting.cnt_h = tmp_u64 >> 16;
    tmp_u64 = (sdi->mode == DSO) ? (devc->limit_samples / (channel_modes[devc->ch_mode].num / ch_num)) :
                                   (devc->actual_samples);
    setting.dso_cnt_l = tmp_u64 & 0x0000ffff;
    setting.dso_cnt_h = tmp_u64 >> 16;

    // trigger position
    // must be align to minimum parallel bits
    tmp_u32 = max((uint32_t)(trigger->trigger_pos / 100.0 * devc->limit_samples), DSLOGIC_ATOMIC_SAMPLES);
    if (devc->stream)
        tmp_u32 = min(tmp_u32, dsl_channel_depth(sdi) * 10 / 100);
    else
        tmp_u32 = min(tmp_u32, dsl_channel_depth(sdi) * DS_MAX_TRIG_PERCENT / 100);
    setting.tpos_l = tmp_u32 & DSLOGIC_ATOMIC_MASK;
    setting.tpos_h = tmp_u32 >> 16;

    // trigger global settings
    setting.trig_glb = ((ch_num & 0x1f) << 8) +
                       (trigger->trigger_stages & 0x00ff);

    // channel enable mapping
    setting.ch_en_l = 0;
    setting.ch_en_h = 0;
    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        if (probe->index < 16)
            setting.ch_en_l += probe->enabled << probe->index;
        else
            setting.ch_en_h += probe->enabled << (probe->index - 16);
    }

    // digital fgain
    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        setting.fgain = probe->digi_fgain;
        break;
    }

    // trigger advanced configuration
    if (trigger->trigger_mode == SIMPLE_TRIGGER) {
        qutr_trig = !(devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ADF4360) && (setting.mode & (1 << QUAR_MODE_BIT));
        half_trig = (!(devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ADF4360) && setting.mode & (1 << HALF_MODE_BIT)) ||
                    ((devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ADF4360) && setting.mode & (1 << QUAR_MODE_BIT));

        setting.trig_mask0[0] = ds_trigger_get_mask0(TriggerStages, TriggerProbes-1, 0, qutr_trig, half_trig);
        setting.trig_mask1[0] = ds_trigger_get_mask1(TriggerStages, TriggerProbes-1, 0, qutr_trig, half_trig);
        setting.trig_value0[0] = ds_trigger_get_value0(TriggerStages, TriggerProbes-1, 0, qutr_trig, half_trig);
        setting.trig_value1[0] = ds_trigger_get_value1(TriggerStages, TriggerProbes-1, 0, qutr_trig, half_trig);
        setting.trig_edge0[0] = ds_trigger_get_edge0(TriggerStages, TriggerProbes-1, 0, qutr_trig, half_trig);
        setting.trig_edge1[0] = ds_trigger_get_edge1(TriggerStages, TriggerProbes-1, 0, qutr_trig, half_trig);

        setting_ext32.trig_mask0[0] = ds_trigger_get_mask0(TriggerStages, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
        setting_ext32.trig_mask1[0] = ds_trigger_get_mask1(TriggerStages, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
        setting_ext32.trig_value0[0] = ds_trigger_get_value0(TriggerStages, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
        setting_ext32.trig_value1[0] = ds_trigger_get_value1(TriggerStages, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
        setting_ext32.trig_edge0[0] = ds_trigger_get_edge0(TriggerStages, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
        setting_ext32.trig_edge1[0] = ds_trigger_get_edge1(TriggerStages, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);

        setting.trig_logic0[0] = (trigger->trigger_logic[TriggerStages] << 1) + trigger->trigger0_inv[TriggerStages];
        setting.trig_logic1[0] = (trigger->trigger_logic[TriggerStages] << 1) + trigger->trigger1_inv[TriggerStages];

        setting.trig_count[0] = trigger->trigger0_count[TriggerStages];

        for (i = 1; i < NUM_TRIGGER_STAGES; i++) {
            setting.trig_mask0[i] = 0xffff;
            setting.trig_mask1[i] = 0xffff;
            setting.trig_value0[i] = 0;
            setting.trig_value1[i] = 0;
            setting.trig_edge0[i] = 0;
            setting.trig_edge1[i] = 0;

            setting_ext32.trig_mask0[i] = 0xffff;
            setting_ext32.trig_mask1[i] = 0xffff;
            setting_ext32.trig_value0[i] = 0;
            setting_ext32.trig_value1[i] = 0;
            setting_ext32.trig_edge0[i] = 0;
            setting_ext32.trig_edge1[i] = 0;

            setting.trig_logic0[i] = 2;
            setting.trig_logic1[i] = 2;

            setting.trig_count[i] = 0;
        }
    } else {
        for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
            if (setting.mode & (1 << STRIG_MODE_BIT) && i == STriggerDataStage) {
                qutr_trig = FALSE;
                half_trig = FALSE;
            } else {
                qutr_trig = !(devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ADF4360) && (setting.mode & (1 << QUAR_MODE_BIT));
                half_trig = (!(devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ADF4360) && setting.mode & (1 << HALF_MODE_BIT)) ||
                            ((devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ADF4360) && setting.mode & (1 << QUAR_MODE_BIT));
            }

            setting.trig_mask0[i] = ds_trigger_get_mask0(i, TriggerProbes-1 , 0, qutr_trig, half_trig);
            setting.trig_mask1[i] = ds_trigger_get_mask1(i, TriggerProbes-1, 0, qutr_trig, half_trig);
            setting.trig_value0[i] = ds_trigger_get_value0(i, TriggerProbes-1, 0, qutr_trig, half_trig);
            setting.trig_value1[i] = ds_trigger_get_value1(i, TriggerProbes-1, 0, qutr_trig, half_trig);
            setting.trig_edge0[i] = ds_trigger_get_edge0(i, TriggerProbes-1, 0, qutr_trig, half_trig);
            setting.trig_edge1[i] = ds_trigger_get_edge1(i, TriggerProbes-1, 0, qutr_trig, half_trig);

            setting_ext32.trig_mask0[i] = ds_trigger_get_mask0(i, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
            setting_ext32.trig_mask1[i] = ds_trigger_get_mask1(i, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
            setting_ext32.trig_value0[i] = ds_trigger_get_value0(i, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
            setting_ext32.trig_value1[i] = ds_trigger_get_value1(i, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
            setting_ext32.trig_edge0[i] = ds_trigger_get_edge0(i, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);
            setting_ext32.trig_edge1[i] = ds_trigger_get_edge1(i, 2*TriggerProbes-1, TriggerProbes, qutr_trig, half_trig);

            setting.trig_logic0[i] = (trigger->trigger_logic[i] << 1) + trigger->trigger0_inv[i];
            setting.trig_logic1[i] = (trigger->trigger_logic[i] << 1) + trigger->trigger1_inv[i];

            setting.trig_count[i] = trigger->trigger0_count[i];
        }
    }

    if (!(devc->profile->usb_speed == LIBUSB_SPEED_SUPER)) {
        // set GPIF to be wordwide
        wr_cmd.header.dest = DSL_CTL_WORDWIDE;
        wr_cmd.header.size = 1;
        wr_cmd.data[0] = bmWR_WORDWIDE;
        if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK) {
            sr_err("Sent DSL_CTL_WORDWIDE command failed.");
            return SR_ERR;
        }
    }

    // send bulk write control command
    arm_size = sizeof(struct DSL_setting) / sizeof(uint16_t);
    wr_cmd.header.dest = DSL_CTL_BULK_WR;
    wr_cmd.header.size = 3;
    wr_cmd.data[0] = (uint8_t)arm_size;
    wr_cmd.data[1] = (uint8_t)(arm_size >> 8);
    wr_cmd.data[2] = (uint8_t)(arm_size >> 16);
    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK) {
        sr_err("Sent bulk write command of arm FPGA failed.");
        return SR_ERR;
    }
    // check sys_clr dessert
    rd_cmd.header.dest = DSL_CTL_HW_STATUS;
    rd_cmd.header.size = 1;
    rd_cmd_data = 0;
    rd_cmd.data = &rd_cmd_data;
    while(1) {
        if ((ret = command_ctl_rd(hdl, rd_cmd)) != SR_OK)
            return SR_ERR;
        if (rd_cmd_data & bmSYS_CLR)
            break;
    }

    // send bulk data
    // setting
    ret = libusb_bulk_transfer(hdl, 2 | LIBUSB_ENDPOINT_OUT,
                               (unsigned char *)&setting,
                               sizeof(struct DSL_setting),
                               &transferred, 1000);
    if (ret < 0) {
        sr_err("Unable to arm FPGA of dsl device: %s.",
                libusb_error_name(ret));
        return SR_ERR;
    } else if (transferred != sizeof(struct DSL_setting)) {
        sr_err("Arm FPGA error: expacted transfer size %d; actually %d",
                sizeof(struct DSL_setting), transferred);
        return SR_ERR;
    }
    // setting_ext32
    if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_LA_CH32) {
        ret = libusb_bulk_transfer(hdl, 2 | LIBUSB_ENDPOINT_OUT,
                                   (unsigned char *)&setting_ext32,
                                   sizeof(struct DSL_setting_ext32),
                                   &transferred, 1000);
        if (ret < 0) {
            sr_err("Unable to arm FPGA(setting_ext32) of dsl device: %s.",
                    libusb_error_name(ret));
            return SR_ERR;
        } else if (transferred != sizeof(struct DSL_setting_ext32)) {
            sr_err("Arm FPGA(setting_ext32) error: expacted transfer size %d; actually %d",
                    sizeof(struct DSL_setting_ext32), transferred);
            return SR_ERR;
        }
    }

    // assert INTRDY high (indicate data end)
    wr_cmd.header.dest = DSL_CTL_INTRDY;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = bmWR_INTRDY;
    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK)
        return SR_ERR;


    // check FPGA_DONE bit
    rd_cmd.header.dest = DSL_CTL_HW_STATUS;
    rd_cmd.header.size = 1;
    rd_cmd_data = 0;
    rd_cmd.data = &rd_cmd_data;
    if ((ret = command_ctl_rd(hdl, rd_cmd)) != SR_OK)
        return SR_ERR;
    if (rd_cmd_data & bmGPIF_DONE) {
        sr_info("Arm FPGA done");
        return SR_OK;
    } else {
        return SR_ERR;
    }
}

SR_PRIV int dsl_fpga_config(struct libusb_device_handle *hdl, const char *filename)
{
    FILE *fw;
    int chunksize, ret;
    unsigned char *buf;
    int transferred;
    uint64_t filesize;
    struct ctl_wr_cmd wr_cmd;
    struct ctl_rd_cmd rd_cmd;
    uint8_t rd_cmd_data;
	struct stat f_stat;

    sr_info("Configure FPGA using \"%s\"", filename);
    if ((fw = fopen(filename, "rb")) == NULL) {
        sr_err("Unable to open FPGA bit file %s for reading: %s",
               filename, strerror(errno));
        ds_set_last_error(SR_ERR_FIRMWARE_NOT_EXIST);
        return SR_ERR;
    }
	
    if (stat(filename, &f_stat) == -1){
        fclose(fw);
        return SR_ERR;
    }

    filesize = (uint64_t)f_stat.st_size;

    if ((buf = g_try_malloc0(filesize)) == NULL) {
        sr_err("FPGA configure buf malloc failed.");
        fclose(fw);
        return SR_ERR;
    }

	// step0: assert PROG_B low
    wr_cmd.header.dest = DSL_CTL_PROG_B;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = ~bmWR_PROG_B;

    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK){
        fclose(fw);
        g_free(buf);
		return SR_ERR;
    }

	// step1: turn off GREEN/RED led
    wr_cmd.header.dest = DSL_CTL_LED;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = ~bmLED_GREEN & ~bmLED_RED;

    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK){
        fclose(fw);
        g_free(buf);
		return SR_ERR;
    }

	// step2: assert PORG_B high
    wr_cmd.header.dest = DSL_CTL_PROG_B;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = bmWR_PROG_B;

    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK){
        fclose(fw);
        g_free(buf);
		return SR_ERR;
    }

	// step3: wait INIT_B go high
    rd_cmd.header.dest = DSL_CTL_HW_STATUS;
    rd_cmd.header.size = 1;
	rd_cmd_data = 0;
    rd_cmd.data = &rd_cmd_data;

    while(1) {
        if ((ret = command_ctl_rd(hdl, rd_cmd)) != SR_OK){
            fclose(fw);
            g_free(buf);
			return SR_ERR;
        }
        if (rd_cmd_data & bmFPGA_INIT_B)
            break;
    }

	// step4: send config ctl command
    wr_cmd.header.dest = DSL_CTL_INTRDY;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = (uint8_t)~bmWR_INTRDY;

    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK){
        fclose(fw);
        g_free(buf);
        return SR_ERR;
    }

    wr_cmd.header.dest = DSL_CTL_BULK_WR;
    wr_cmd.header.size = 3;
    wr_cmd.data[0] = (uint8_t)filesize;
    wr_cmd.data[1] = (uint8_t)(filesize >> 8);
    wr_cmd.data[2] = (uint8_t)(filesize >> 16);

    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK) {
        sr_err("Configure FPGA error: send command fpga_config failed.");
        fclose(fw);
        g_free(buf);
		return SR_ERR;
    }

	// step5: send config data
    chunksize = fread(buf, 1, filesize, fw);

    if (chunksize == EOF){
        sr_err("dsl_fpga_config(), f-read returns EOF.");
        fclose(fw);
        g_free(buf);
		return SR_ERR;	
    }

    if (chunksize == 0){
        fclose(fw);
        g_free(buf);
		return SR_ERR;
    }

    ret = libusb_bulk_transfer(hdl, 2 | LIBUSB_ENDPOINT_OUT,
                               buf, chunksize,
                               &transferred, 1000);
    fclose(fw);
    g_free(buf);
    fw = NULL;
    buf = NULL;

    if (ret < 0) {
        sr_err("Unable to configure FPGA of dsl device: %s.",
                libusb_error_name(ret));
		return SR_ERR;
    } else if (transferred != chunksize) {
        sr_err("Configure FPGA error: expacted transfer size %d; actually %d.",
                chunksize, transferred);
		return SR_ERR;
    }

	// step6: assert INTRDY high (indicate data end)
    wr_cmd.header.dest = DSL_CTL_INTRDY;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = bmWR_INTRDY;
    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK)
		return SR_ERR;

    // step7: check GPIF_DONE
    rd_cmd.header.dest = DSL_CTL_HW_STATUS;
    rd_cmd.header.size = 1;
    rd_cmd_data = 0;
    rd_cmd.data = &rd_cmd_data;
    while ((ret = command_ctl_rd(hdl, rd_cmd)) == SR_OK) {
        if (rd_cmd_data & bmGPIF_DONE) {
            break;
        }
    }

    // step8: assert INTRDY low
    wr_cmd.header.dest = DSL_CTL_INTRDY;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = (uint8_t)~bmWR_INTRDY;
    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK)
        return SR_ERR;

    // step9: check FPGA_DONE bit
    rd_cmd.header.dest = DSL_CTL_HW_STATUS;
    rd_cmd.header.size = 1;
	rd_cmd_data = 0;
    rd_cmd.data = &rd_cmd_data;
    while ((ret = command_ctl_rd(hdl, rd_cmd)) == SR_OK) {
        if (rd_cmd_data & bmFPGA_DONE) {
            // step10: turn on GREEN led
            wr_cmd.header.dest = DSL_CTL_LED;
            wr_cmd.data[0] = bmLED_GREEN;
            if ((ret = command_ctl_wr(hdl, wr_cmd)) == SR_OK)
                break;
        }
    }

    // step10: recover GPIF to be wordwide
    wr_cmd.header.dest = DSL_CTL_WORDWIDE;
    wr_cmd.header.size = 1;
    wr_cmd.data[0] = bmWR_WORDWIDE;
    if ((ret = command_ctl_wr(hdl, wr_cmd)) != SR_OK) {
        sr_err("Sent DSL_CTL_WORDWIDE command failed.");
        return SR_ERR;
    }

    sr_info("FPGA configure done: %d bytes.", chunksize);
    return SR_OK;
}

SR_PRIV int dsl_config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
                      const struct sr_channel *ch,
                      const struct sr_channel_group *cg)
{
    struct DSL_context *devc = sdi->priv;
    struct sr_usb_dev_inst *usb;
    char str[128];

    (void)cg;

    switch (id) {
    case SR_CONF_CONN:
        if (!sdi || !sdi->conn)
            return SR_ERR_ARG;
        usb = sdi->conn;
        if (usb->address == 255)
            /* Device still needs to re-enumerate after firmware
             * upload, so we don't know its (future) address. */
            return SR_ERR;
        snprintf(str, 128, "%d.%d", usb->bus, usb->address);
        *data = g_variant_new_string(str);
        break;
    case SR_CONF_USB_SPEED:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_int32(devc->profile->usb_speed);
        break;
    case SR_CONF_USB30_SUPPORT:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean((devc->profile->dev_caps.feature_caps & CAPS_FEATURE_USB30) != 0);
        break;
    case SR_CONF_LIMIT_SAMPLES:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint64(devc->limit_samples);
        break;
    case SR_CONF_SAMPLERATE:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint64(devc->cur_samplerate);
        break;
    case SR_CONF_RLE_SUPPORT:
        if (!sdi)
            return SR_ERR;
        if ((devc->test_mode != SR_TEST_NONE))
            *data = g_variant_new_boolean(FALSE);
        else
            *data = g_variant_new_boolean(devc->rle_support);
        break;
    case SR_CONF_CLOCK_TYPE:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean(devc->clock_type);
        break;
    case SR_CONF_CLOCK_EDGE:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean(devc->clock_edge);
        break;
    case SR_CONF_INSTANT:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean(devc->instant);
        break;
    case SR_CONF_PROBE_VDIV:
        if (!ch)
            return SR_ERR;
        *data = g_variant_new_uint64(ch->vdiv);
        break;
    case SR_CONF_PROBE_FACTOR:
        if (!ch)
            return SR_ERR;
        *data = g_variant_new_uint64(ch->vfactor);
        break;
    case SR_CONF_PROBE_OFFSET:
        if (!ch)
            return SR_ERR;
        *data = g_variant_new_uint16(ch->offset);
        break;
    case SR_CONF_PROBE_HW_OFFSET:
        if (!ch)
            return SR_ERR;
        *data = g_variant_new_uint16(ch->hw_offset);
        break;
    case SR_CONF_TIMEBASE:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint64(devc->timebase);
        break;
    case SR_CONF_MAX_TIMEBASE:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint64(min(MAX_TIMEBASE,
                                         SR_SEC(1) *
                                         devc->profile->dev_caps.dso_depth /
                                         channel_modes[devc->ch_mode].num /
                                         channel_modes[devc->ch_mode].min_samplerate /
                                         DS_CONF_DSO_HDIVS));
        break;
    case SR_CONF_MIN_TIMEBASE:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint64(SR_SEC(1) / channel_modes[devc->ch_mode].hw_max_samplerate);
        break;
    case SR_CONF_PROBE_COUPLING:
        if (!ch)
            return SR_ERR;
        *data = g_variant_new_byte(ch->coupling);
        break;
    case SR_CONF_PROBE_EN:
        if (!ch)
            return SR_ERR;
        *data = g_variant_new_boolean(ch->enabled);
        break;
    case SR_CONF_TRIGGER_SLOPE:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_byte(devc->trigger_slope);
        break;
    case SR_CONF_TRIGGER_SOURCE:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_byte(devc->trigger_source&0x0f);
        break;
    case SR_CONF_TRIGGER_CHANNEL:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_byte(devc->trigger_source>>4);
        break;
    case SR_CONF_TRIGGER_VALUE:
        if (!ch)
            return SR_ERR;
        *data = g_variant_new_byte(ch->trig_value);
        break;
    case SR_CONF_HORIZ_TRIGGERPOS:
        if (!sdi)
            return SR_ERR;
        if (sdi->mode == DSO) {
            *data = g_variant_new_byte(devc->trigger_hrate);
        } else {
            *data = g_variant_new_byte(devc->trigger_hpos);
        }
        break;
    case SR_CONF_TRIGGER_HOLDOFF:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint64(devc->trigger_holdoff);
        break;
    case SR_CONF_TRIGGER_MARGIN:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_byte(devc->trigger_margin);
        break;
    case SR_CONF_HAVE_ZERO:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean((devc->profile->dev_caps.feature_caps & CAPS_FEATURE_ZERO) != 0);
        break;
    case SR_CONF_ZERO:
        if (!sdi)
            return SR_ERR;
        if (sdi->mode == DSO)
            *data = g_variant_new_boolean(devc->zero);
        else
            *data = g_variant_new_boolean(FALSE);
        break;
    case SR_CONF_ZERO_COMB_FGAIN:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean(devc->zero_comb_fgain);
        break;
    case SR_CONF_ROLL:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean(devc->roll);
        break;
    case SR_CONF_UNIT_BITS:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_byte(channel_modes[devc->ch_mode].unit_bits);
        break;
    case SR_CONF_REF_MIN:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint32(devc->profile->dev_caps.ref_min);
        break;
    case SR_CONF_REF_MAX:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint32(devc->profile->dev_caps.ref_max);
        break;
    case SR_CONF_PROBE_MAP_DEFAULT:
        if (!sdi || !ch)
            return SR_ERR;
        *data = g_variant_new_boolean(ch->map_default);
        break;
    case SR_CONF_PROBE_MAP_UNIT:
        if (!sdi || !ch)
            return SR_ERR;
        *data = g_variant_new_string(ch->map_unit);
        break;
    case SR_CONF_PROBE_MAP_MIN:
        if (!sdi || !ch)
            return SR_ERR;
        *data = g_variant_new_double(ch->map_min);
        break;
    case SR_CONF_PROBE_MAP_MAX:
        if (!sdi || !ch)
            return SR_ERR;
        *data = g_variant_new_double(ch->map_max);
        break;
    case SR_CONF_ACTUAL_SAMPLES:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_uint64(devc->actual_samples);
        break;
    case SR_CONF_BANDWIDTH:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean((devc->profile->dev_caps.feature_caps & CAPS_FEATURE_20M) != 0);
        break;
    case SR_CONF_LA_CH32:
        if (!sdi)
            return SR_ERR;
        *data = g_variant_new_boolean((devc->profile->dev_caps.feature_caps & CAPS_FEATURE_LA_CH32) != 0);
        break;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

SR_PRIV int dsl_config_set(int id, GVariant *data, struct sr_dev_inst *sdi,
                      struct sr_channel *ch,
                      struct sr_channel_group *cg )
{
    (void)cg;
    struct DSL_context *devc = sdi->priv;
    int ret = SR_OK;

    if (id == SR_CONF_ZERO_COMB) {
        devc->zero_comb = g_variant_get_boolean(data);
    } else if (id == SR_CONF_PROBE_MAP_DEFAULT) {
        ch->map_default = g_variant_get_boolean(data);
        if (ch->map_default) {
            ch->map_unit = probeMapUnits[0];
            ch->map_min = -(ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0);
            ch->map_max = ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0;
        }
    } else if (id == SR_CONF_PROBE_MAP_UNIT) {
        if (ch->map_default)
            ch->map_unit = probeMapUnits[0];
        else
            ch->map_unit = g_variant_get_string(data, NULL);
    } else if (id == SR_CONF_PROBE_MAP_MIN) {
        if (ch->map_default)
            ch->map_min = -(ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0);
        else
            ch->map_min = g_variant_get_double(data);
    } else if (id == SR_CONF_PROBE_MAP_MAX) {
        if (ch->map_default)
            ch->map_max = ch->vdiv * ch->vfactor * DS_CONF_DSO_VDIVS / 2000.0;
        else
            ch->map_max = g_variant_get_double(data);
    } else {
        ret = SR_ERR_NA;
    }

    return ret;
}

SR_PRIV int dsl_config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
                       const struct sr_channel_group *cg)
{
    struct DSL_context *devc;
    GVariant *gvar;
    GVariantBuilder gvb;
    int i;

    (void)cg;
    assert(sdi->priv);
    
    devc = sdi->priv;

    switch (key) {
    case SR_CONF_SAMPLERATE:
        GVariant *list;
        g_variant_builder_init(&gvb, G_VARIANT_TYPE("at"));
        if(devc->clock_type) {
            g_variant_builder_add(&gvb, "t", devc->ext_samplerate);
        }
        for(int i = devc->samplerates_min_index; i <= devc->samplerates_max_index; i++) {
            g_variant_builder_add(&gvb, "t", devc->profile->dev_caps.samplerates[i]);
        }
        list = g_variant_builder_end(&gvb);
        g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&gvb, "{sv}", "samplerates", list);
        *data = g_variant_builder_end(&gvb);
        break;

    case SR_CONF_PROBE_CONFIGS:
        *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
                probeOptions, ARRAY_SIZE(probeOptions)*sizeof(int32_t), TRUE, NULL, NULL);
        break;
 
    case SR_CONF_PROBE_VDIV:
        g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
        for (i = 0; devc->profile->dev_caps.vdivs[i]; i++);
        gvar = g_variant_new_from_data(G_VARIANT_TYPE("at"),
                devc->profile->dev_caps.vdivs, i*sizeof(uint64_t), TRUE, NULL, NULL);
        g_variant_builder_add(&gvb, "{sv}", "vdivs", gvar);
        *data = g_variant_builder_end(&gvb);
        break;
    case SR_CONF_PROBE_COUPLING:
        g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
        gvar = g_variant_new_from_data(G_VARIANT_TYPE("ay"),
                probeCoupling, ARRAY_SIZE(probeCoupling)*sizeof(uint8_t), TRUE, NULL, NULL);
        g_variant_builder_add(&gvb, "{sv}", "coupling", gvar);
        *data = g_variant_builder_end(&gvb);
        break;
    case SR_CONF_PROBE_MAP_UNIT:
        *data = g_variant_new_strv(probeMapUnits, ARRAY_SIZE(probeMapUnits));
        break;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

SR_PRIV int dsl_dev_open(struct sr_dev_driver *di, struct sr_dev_inst *sdi, gboolean *fpga_done)
{
    struct sr_usb_dev_inst *usb;
    struct DSL_context *devc;
    int ret;
    uint8_t hw_info;
    struct ctl_rd_cmd rd_cmd;
    int fdError = 0;

    devc = sdi->priv;
    usb = sdi->conn;
 
    /*
     * If the firmware was recently uploaded, no dev_open operation should be called.
     * Just wait for renumerate -> detach -> attach
     */
    ret = SR_ERR;
    ret = hw_dev_open(di, sdi);

    if (ret != SR_OK) {
        sr_err("%s: Unable to open device.", __func__);
        return SR_ERR;
    }

    assert(usb->devhdl);
  
    ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);

    if (ret != LIBUSB_SUCCESS) {
        switch(ret) {
        case LIBUSB_ERROR_BUSY:
            sr_err("%s: Unable to claim USB interface. Another "
                   "program or driver has already claimed it.", __func__);
            break;
        case LIBUSB_ERROR_NO_DEVICE:
            sr_err("%s: Device has been disconnected.", __func__);
            break;
        case LIBUSB_ERROR_NOT_FOUND:
            {
                sr_err("%s: Unable to claim interface, try again: LIBUSB_ERROR_NOT_FOUND.", __func__);
                ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
                fdError = 1;
            }
            break;
        default:
            sr_err("%s: Unable to claim interface, try again: %s.",
                   __func__, libusb_error_name(ret));
            break;
        }

        if (ret != LIBUSB_SUCCESS && fdError == 1){
             sr_err("%s: Unable to claim interface, the second time: %s.",
                   __func__, libusb_error_name(ret));
        }

        if (ret != LIBUSB_SUCCESS){
            dsl_dev_close(sdi);
            ds_set_last_error(SR_ERR_DEVICE_IS_EXCLUSIVE);
            return SR_ERR;
        } 
    }

    rd_cmd.header.dest = DSL_CTL_HW_STATUS;
    rd_cmd.header.size = 1;
    hw_info = 0;
    rd_cmd.data = &hw_info;

    if ((ret = command_ctl_rd(usb->devhdl, rd_cmd)) != SR_OK) {
        sr_err("%s:%d, Failed to get hardware information.",
            __func__, __LINE__);
        ds_set_last_error(SR_ERR_DEVICE_USB_IO_ERROR);
        return SR_ERR;
    }
    *fpga_done = (hw_info & bmFPGA_DONE) != 0;

    if (sdi->status == SR_ST_ACTIVE) {
        if (!(*fpga_done)) {
            char *fpga_bit;
            char *res_path = DS_RES_PATH;

            if (!(fpga_bit = g_try_malloc0(strlen(res_path)+strlen(devc->profile->fpga_bit33) + 5))) {
                sr_err("fpag_bit path malloc error!");
                return SR_ERR_MALLOC;
            }
            strcpy(fpga_bit, res_path);
            strcat(fpga_bit, "/");

            switch(devc->th_level) {
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
                sr_err("%s:%d, Configure FPGA failed!",
                    __func__, __LINE__);
                ds_set_last_error(SR_ERR_DEVICE_USB_IO_ERROR);
                return SR_ERR;
            }
        }
        else {
            ret = dsl_wr_reg(sdi, CTR0_ADDR, bmNONE); // dessert clear
            /* Check HDL version */
            ret = dsl_hdl_version(sdi, &hw_info);

            if ((ret != SR_OK)) {
               sr_err("%s:%d, Failed to get FPGA bin version!",
                        __func__, __LINE__);
               ds_set_last_error(SR_ERR_DEVICE_USB_IO_ERROR);
               sdi->status = SR_ST_INACTIVE;
               return SR_ERR;
            }

            if (hw_info != DSL_HDL_VERSION) {
               sr_err("%s: HDL verison incompatible! device:%02X, target:%02X", __func__, 
                            hw_info, DSL_HDL_VERSION);
               ds_set_last_error(SR_ERR_DEVICE_FIRMWARE_VERSION_LOW);
               sdi->status = SR_ST_INCOMPATIBLE;
               return SR_ERR;
            }
        }
    }

     // check security
    uint16_t encryption[SECU_STEPS];
    ret = dsl_wr_reg(sdi, CTR0_ADDR, bmNONE); // dessert clear
    if (dsl_rd_nvm(sdi, (unsigned char *)encryption, SECU_EEP_ADDR, SECU_STEPS*2) != SR_OK) {
        sr_err("%s:%d, Read EEPROM content failed!",
            __func__, __LINE__);
        ds_set_last_error(SR_ERR_DEVICE_USB_IO_ERROR);
        return SR_ERR;
    }

    if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_SECURITY) {
        ret = dsl_secuCheck(sdi, encryption, SECU_STEPS);
        if (ret != SR_OK){
            sr_err("Security check failed!");
        } else {
            sr_err("Security check pass!");
        }
    }

    return SR_OK;
}

SR_PRIV int dsl_dev_close(struct sr_dev_inst *sdi)
{
    struct sr_usb_dev_inst *usb;

    assert(sdi);

    usb = sdi->conn;

    if (usb == NULL){
        sdi->status = SR_ST_INACTIVE;
        return SR_OK;
    }
 
    if (usb->devhdl == NULL){
        sr_detail("dsl_dev_close(),libusb_device_handle is null.");
        return SR_ERR;
    }

    sr_info("%s: Closing device %d on %d.%d interface %d.",
        sdi->driver->name, sdi->index, usb->bus, usb->address, USB_INTERFACE);
    
    if (usb->devhdl != NULL){
        libusb_release_interface(usb->devhdl, USB_INTERFACE);
        libusb_close(usb->devhdl);
    }
    //sr_info("------------Close the libusb_device_handle:%p, struct:%p", usb->devhdl, usb);

    usb->devhdl = NULL;
    sdi->status = SR_ST_INACTIVE;

    return SR_OK;
}

SR_PRIV int dsl_dev_acquisition_stop(const struct sr_dev_inst *sdi, void *cb_data)
{
    (void)cb_data;

    struct DSL_context *devc;
    struct sr_usb_dev_inst *usb;
    int ret;
    struct ctl_wr_cmd wr_cmd;

    devc = sdi->priv;
    usb = sdi->conn;

    if (!devc->abort) {
        devc->abort = TRUE;
        dsl_wr_reg(sdi, CTR0_ADDR, bmFORCE_RDY);
        sr_info("Send command:\"bmFORCE_RDY\"");
    }
    else if (devc->status == DSL_FINISH) {
        /* Stop GPIF acquisition */
        wr_cmd.header.dest = DSL_CTL_STOP;
        wr_cmd.header.size = 0;
        if ((ret = command_ctl_wr(usb->devhdl, wr_cmd)) != SR_OK)
            sr_err("%s: Sent acquisition stop command failed!", __func__);
        else
            sr_info("%s: Sent acquisition stop command!", __func__);

        /* check a real stop of FPGA status*/

        uint8_t hw_status = 1;
        dsl_rd_reg(sdi, HW_STATUS_ADDR, &hw_status);

        /* adc power down*/
        if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_HMCAD1511) {
            dsl_config_adc(sdi, adc_power_down);
        }        
    }

    return SR_OK;
}

SR_PRIV int dsl_dev_status_get(const struct sr_dev_inst *sdi, struct sr_status *status, gboolean prg)
{
    int ret = SR_ERR;

    if (sdi) {
        struct DSL_context *devc;
        devc = sdi->priv;
        if (prg || devc->mstatus_valid) {
            *status = devc->mstatus;
            ret = SR_OK;
        }
    }

    return ret;
}

static unsigned int get_single_buffer_time(const struct DSL_context *devc)
{
    if (devc->profile->usb_speed == LIBUSB_SPEED_SUPER)
        return 10;
    else
        return 20;
}

static unsigned int get_total_buffer_time(const struct DSL_context *devc)
{
    if (devc->profile->usb_speed == LIBUSB_SPEED_SUPER)
        return 40;
    else
        return 100;
}

static unsigned int to_bytes_per_ms(struct DSL_context *devc)
{
    struct sr_dev_inst *sdi = devc->cb_data;
    if (sdi->mode == LOGIC) {
        return ceil(devc->cur_samplerate / 1000.0 * dsl_en_ch_num(sdi) / 8);
    } else {
        if (devc->cur_samplerate > SR_MHZ(100))
            return SR_MHZ(100) / 1000.0 * dsl_en_ch_num(sdi);
        else
            return ceil(max(devc->cur_samplerate, channel_modes[devc->ch_mode].hw_min_samplerate) / 1000.0 * dsl_en_ch_num(sdi));
    }
}

SR_PRIV int dsl_header_size(const struct DSL_context *devc)
{
    int size;

    if (devc->profile->dev_caps.feature_caps & CAPS_FEATURE_USB30)
        size = SR_KB(1);
    else
        size = SR_B(512);
    return size;
}

static size_t get_buffer_size(const struct sr_dev_inst *sdi)
{
    size_t s;
    struct DSL_context *devc;
    devc = sdi->priv;

    /*
     * The buffer should be large enough to hold 10ms of data and
     * a multiple of 512.
     */
    if (sdi->mode == DSO) {
        s = (devc->instant) ? devc->profile->dev_caps.dso_depth : devc->actual_samples * dsl_en_ch_num(sdi) + dsl_header_size(devc);
    } else {
        s = (devc->stream) ? get_single_buffer_time(devc) * to_bytes_per_ms(devc) : 1024*1024;
    }

    if (devc->profile->usb_speed == LIBUSB_SPEED_SUPER)
        return (s + 1023ULL) & ~1023ULL;
    else
        return (s + 511ULL) & ~511ULL;
}

static unsigned int get_number_of_transfers(const struct sr_dev_inst *sdi)
{
    unsigned int n;
    struct DSL_context *devc;
    devc = sdi->priv;

    #ifndef _WIN32
    /* Total buffer size should be able to hold about 100ms of data. */
    n = (devc->stream) ? ceil(get_total_buffer_time(devc) * 1.0f * to_bytes_per_ms(devc) / get_buffer_size(sdi)) : 1;
    #else
    n = (devc->stream) ? ceil(get_total_buffer_time(devc) * 1.0f * to_bytes_per_ms(devc) / get_buffer_size(sdi)) :
        (devc->profile->usb_speed == LIBUSB_SPEED_SUPER) ? 16 : 4;
    #endif

    if (n > NUM_SIMUL_TRANSFERS)
        return NUM_SIMUL_TRANSFERS;

    return n;
}

SR_PRIV unsigned int dsl_get_timeout(const struct sr_dev_inst *sdi)
{
    size_t total_size;
    unsigned int timeout;
    struct DSL_context *devc;
    devc = sdi->priv;

    total_size = get_buffer_size(sdi) * get_number_of_transfers(sdi);
    timeout = total_size / to_bytes_per_ms(devc);

    if (devc->stream)
        return timeout + timeout / 4; /* Leave a headroom of 25% percent. */
    else
        return 20;
}

static void finish_acquisition(struct DSL_context *devc)
{
    struct sr_datafeed_packet packet;

    sr_info("%s: send SR_DF_END packet", __func__);
    /* Terminate session. */
    packet.type = SR_DF_END;
    packet.status = SR_PKT_OK;
    ds_data_forward(devc->cb_data, &packet);

    if (devc->num_transfers != 0) {
        devc->num_transfers = 0;
        g_free(devc->transfers);
    }

    devc->status = DSL_FINISH;
}

static void free_transfer(struct libusb_transfer *transfer, int force)
{
    struct DSL_context *devc;
    unsigned int i;

    devc = transfer->user_data;

    g_free(transfer->buffer);
    transfer->buffer = NULL;
    libusb_free_transfer(transfer);

    for (i = 0; i < devc->num_transfers; i++) {
        if (devc->transfers[i] == transfer) {
            devc->transfers[i] = NULL;
            break;
        }
    }

    if (!devc->is_loop || devc->status != DSL_DATA || force)
        devc->submitted_transfers--;

    if (devc->submitted_transfers == 0){
        sr_info("Call finish_acquisition()");
        finish_acquisition(devc);
    }
}

static void resubmit_transfer(struct libusb_transfer *transfer)
{
    int ret;

    if ((ret = libusb_submit_transfer(transfer)) == LIBUSB_SUCCESS)
        return;

    free_transfer(transfer, 0);
    /* TODO: Stop session? */

    sr_err("%s: %s", __func__, libusb_error_name(ret));
}

static void get_measure(const struct sr_dev_inst *sdi, uint8_t *buf, uint32_t offset)
{
    uint64_t u64_tmp;
    struct DSL_context *devc = sdi->priv;
    GSList *l;

    devc->mstatus.pkt_id = *((const uint16_t*)buf + offset);
    devc->mstatus.vlen = *((const uint32_t*)buf + offset/2 + 2/2) & 0x0fffffff;
    devc->mstatus.stream_mode = (*((const uint32_t*)buf + offset/2 + 2/2) & 0x80000000) != 0;
    devc->mstatus.measure_valid = *((const uint32_t*)buf + offset/2 + 2/2) & 0x40000000;
    devc->mstatus.sample_divider = *((const uint32_t*)buf + offset/2 + 4/2) & 0x00ffffff;
    devc->mstatus.sample_divider_tog = (*((const uint32_t*)buf + offset/2 + 4/2) & 0x80000000) != 0;
    devc->mstatus.trig_flag = (*((const uint32_t*)buf + offset/2 + 4/2) & 0x40000000) != 0;
    devc->mstatus.trig_ch = (*((const uint8_t*)buf + offset*2 + 5*2+1) & 0x38) >> 3;
    devc->mstatus.trig_offset = *((const uint8_t*)buf + offset*2 + 5*2+1) & 0x07;

    devc->mstatus.ch0_max = *((const uint8_t*)buf + offset*2 + 33*2);
    devc->mstatus.ch0_min = *((const uint8_t*)buf + offset*2 + 33*2+1);
    devc->mstatus.ch0_cyc_cnt = *((const uint32_t*)buf + offset/2 + 34/2);
    devc->mstatus.ch0_cyc_tlen = *((const uint32_t*)buf + offset/2 + 36/2);
    devc->mstatus.ch0_cyc_plen = *((const uint32_t*)buf + offset/2 + 38/2);
    devc->mstatus.ch0_cyc_llen = *((const uint32_t*)buf + offset/2 + 40/2);
    devc->mstatus.ch0_level_valid = (*((const uint32_t*)buf + offset/2 + 42/2) & 0x00008000) != 0;
    devc->mstatus.ch0_plevel = (*((const uint32_t*)buf + offset/2 + 42/2) & 0x00004000) != 0;
    devc->mstatus.ch0_high_level = *((const uint8_t*)buf + offset*2 + 43*2);
    devc->mstatus.ch0_low_level = *((const uint8_t*)buf + offset*2 + 43*2+1);
    devc->mstatus.ch0_cyc_rlen = *((const uint32_t*)buf + offset/2 + 44/2);
    devc->mstatus.ch0_cyc_flen = *((const uint32_t*)buf + offset/2 + 46/2);
    devc->mstatus.ch0_acc_square = *((const uint64_t*)buf + offset/4 + 48/4) & 0x0000FFFFFFFFFFFF;
    devc->mstatus.ch0_acc_mean = *((const uint32_t*)buf + offset/2 + 52/2);
    devc->mstatus.ch0_acc_mean_p1 = *((const uint32_t*)buf + offset/2 + 54/2);
    devc->mstatus.ch0_acc_mean_p2 = *((const uint32_t*)buf + offset/2 + 56/2);
    devc->mstatus.ch0_acc_mean_p3 = *((const uint32_t*)buf + offset/2 + 58/2);

    devc->mstatus.ch1_max = *((const uint8_t*)buf + offset*2 + 65*2);
    devc->mstatus.ch1_min = *((const uint8_t*)buf + offset*2 + 65*2+1);
    devc->mstatus.ch1_cyc_cnt = *((const uint32_t*)buf + offset/2 + 66/2);
    devc->mstatus.ch1_cyc_tlen = *((const uint32_t*)buf + offset/2 + 68/2);
    devc->mstatus.ch1_cyc_plen = *((const uint32_t*)buf + offset/2 + 70/2);
    devc->mstatus.ch1_cyc_llen = *((const uint32_t*)buf + offset/2 + 72/2);
    devc->mstatus.ch1_level_valid = (*((const uint32_t*)buf + offset/2 + 74/2) & 0x00008000) != 0;
    devc->mstatus.ch1_plevel = (*((const uint32_t*)buf + offset/2 + 74/2) & 0x00004000) != 0;
    devc->mstatus.ch1_high_level = *((const uint8_t*)buf + offset*2 + 75*2);
    devc->mstatus.ch1_low_level = *((const uint8_t*)buf + offset*2 + 75*2+1);
    devc->mstatus.ch1_cyc_rlen = *((const uint32_t*)buf + offset/2 + 76/2);
    devc->mstatus.ch1_cyc_flen = *((const uint32_t*)buf + offset/2 + 78/2);
    devc->mstatus.ch1_acc_square = *((const uint64_t*)buf + offset/4 + 80/4) & 0x0000FFFFFFFFFFFF;
    devc->mstatus.ch1_acc_mean = *((const uint32_t*)buf + offset/2 + 84/2);
    devc->mstatus.ch1_acc_mean_p1 = *((const uint32_t*)buf + offset/2 + 86/2);
    devc->mstatus.ch1_acc_mean_p2 = *((const uint32_t*)buf + offset/2 + 88/2);
    devc->mstatus.ch1_acc_mean_p3 = *((const uint32_t*)buf + offset/2 + 90/2);

    if (!devc->zero_branch) {
        devc->mstatus.ch0_acc_mean += devc->mstatus.ch0_acc_mean_p1;
        devc->mstatus.ch0_acc_mean += devc->mstatus.ch0_acc_mean_p2;
        devc->mstatus.ch0_acc_mean += devc->mstatus.ch0_acc_mean_p3;

        devc->mstatus.ch1_acc_mean += devc->mstatus.ch1_acc_mean_p1;
        devc->mstatus.ch1_acc_mean += devc->mstatus.ch1_acc_mean_p2;
        devc->mstatus.ch1_acc_mean += devc->mstatus.ch1_acc_mean_p3;

        if (1 == dsl_en_ch_num(sdi)) {
            u64_tmp = devc->mstatus.ch0_acc_square + devc->mstatus.ch1_acc_square;
            devc->mstatus.ch0_acc_square = u64_tmp;
            devc->mstatus.ch1_acc_square = u64_tmp;
            u64_tmp = devc->mstatus.ch0_acc_mean + devc->mstatus.ch1_acc_mean;
            devc->mstatus.ch0_acc_mean = u64_tmp;
            devc->mstatus.ch1_acc_mean = u64_tmp;
        }
    }

    devc->mstatus_valid = FALSE;
    const uint32_t divider = devc->zero ? 0x1 : (uint32_t)ceil(channel_modes[devc->ch_mode].max_samplerate * 1.0 / devc->cur_samplerate / dsl_en_ch_num(sdi));
    if (devc->instant) {
        devc->mstatus_valid = (devc->mstatus.pkt_id == DSO_PKTID);
    } else if (devc->mstatus.pkt_id == DSO_PKTID &&
               devc->mstatus.sample_divider == divider &&
               devc->mstatus.vlen != 0) {
        devc->mstatus_valid = TRUE;
    }

    if (devc->mstatus_valid) {
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *probe = (struct sr_channel *)l->data;
            probe->hw_offset = *((const uint8_t*)buf + offset*2 + (51 + 32*probe->index)*2);
        }
    }
}

static void receive_transfer(struct libusb_transfer *transfer)
{
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    struct sr_datafeed_dso dso;
    struct sr_datafeed_analog analog;
    uint64_t cur_sample_count = 0;

    uint8_t *cur_buf = transfer->buffer;
    struct DSL_context *devc = transfer->user_data;
    struct sr_dev_inst *sdi = devc->cb_data;

    if (devc->status == DSL_START)
        devc->status = DSL_DATA;

    if (devc->abort)
        devc->status = DSL_STOP;

    sr_detail("%llu: receive_transfer(): status %d; timeout %d; received %d bytes.",
        (u64_t)g_get_monotonic_time(), transfer->status, transfer->timeout, transfer->actual_length);

    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
    case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though. */
        break;
    default:
        devc->status = DSL_ERROR;
        break;
    }

    packet.status = SR_PKT_OK;

    if (devc->status == DSL_DATA &&
        transfer->actual_length != 0) {
        /* Send the incoming transfer to the session bus. */
        // check packet type
        if (sdi->mode == LOGIC) {
            packet.type = SR_DF_LOGIC;
            packet.payload = &logic;
            cur_sample_count = transfer->actual_length * 8 / dsl_en_ch_num(sdi) ;
            logic.length = transfer->actual_length;
            logic.format = LA_CROSS_DATA;
            logic.data_error = 0;
            logic.data = cur_buf;
        }
        else if (sdi->mode == DSO) {
            if (!devc->instant) {
                const uint32_t offset = devc->actual_samples / (channel_modes[devc->ch_mode].num/dsl_en_ch_num(sdi));
                get_measure(sdi, cur_buf, offset);
            }
            else {
                devc->mstatus.vlen = get_buffer_size(sdi) / channel_modes[devc->ch_mode].num;
                devc->mstatus.trig_offset = 0;
                devc->mstatus.sample_divider_tog = FALSE;
                devc->mstatus_valid = TRUE;
            }

            if (devc->mstatus_valid) {
                devc->roll = (devc->mstatus.stream_mode != 0);
                packet.type = SR_DF_DSO;
                packet.payload = &dso;
                dso.probes = sdi->channels;
                cur_sample_count = min(channel_modes[devc->ch_mode].num * devc->mstatus.vlen / dsl_en_ch_num(sdi), devc->limit_samples);
                dso.num_samples = cur_sample_count;
                dso.mq = SR_MQ_VOLTAGE;
                dso.unit = SR_UNIT_VOLT;
                dso.mqflags = SR_MQFLAG_AC;
                dso.samplerate_tog = (devc->mstatus.sample_divider_tog != 0);
                dso.trig_flag = (devc->mstatus.trig_flag != 0);
                dso.trig_ch = devc->mstatus.trig_ch;
                dso.data = cur_buf + (devc->zero ? 0 : 2*devc->mstatus.trig_offset);
            }
            else {
                packet.type = SR_DF_DSO;
                packet.status = SR_PKT_DATA_ERROR;
            }
        }
        else if (sdi->mode == ANALOG) {
            packet.type = SR_DF_ANALOG;
            packet.payload = &analog;
            analog.probes = sdi->channels;
            cur_sample_count = transfer->actual_length / (((channel_modes[devc->ch_mode].unit_bits + 7) / 8) * g_slist_length(analog.probes));
            analog.num_samples = cur_sample_count;
            analog.unit_bits = channel_modes[devc->ch_mode].unit_bits;;
            analog.unit_pitch = devc->unit_pitch;
            analog.mq = SR_MQ_VOLTAGE;
            analog.unit = SR_UNIT_VOLT;
            analog.mqflags = SR_MQFLAG_AC;
            analog.data = cur_buf;
        }

        if ((devc->limit_samples && (devc->num_bytes < devc->actual_bytes || devc->is_loop) )
           || sdi->mode != LOGIC)
        {
            if (!devc->is_loop){
                const uint64_t remain_length= devc->actual_bytes - devc->num_bytes;
                logic.length = min(logic.length, remain_length);
            }

            /* send data to session bus */
            if (packet.status == SR_PKT_OK)
                ds_data_forward(sdi, &packet);
        }

        devc->num_samples += cur_sample_count;
        devc->num_bytes += logic.length;
        if (sdi->mode == LOGIC &&
            devc->limit_samples &&
            !devc->is_loop &&
            devc->num_bytes >= devc->actual_bytes) {
            devc->status = DSL_STOP;
        } else if ((sdi->mode == DSO && devc->instant) &&
                   devc->limit_samples &&
                   devc->num_samples >= devc->actual_samples) {
            int over_bytes = (devc->num_samples - devc->actual_samples) * dsl_en_ch_num(sdi);
            if (over_bytes >= devc->instant_tail_bytes) {
                const uint32_t offset = (transfer->actual_length - over_bytes) / 2;
                get_measure(sdi, cur_buf, offset);
                devc->status = DSL_STOP;
            } else {

            }
        }
    }

    if (devc->status == DSL_DATA)
        resubmit_transfer(transfer);
    else
        free_transfer(transfer, 0);

    devc->trf_completed = 1;
}

static void receive_header(struct libusb_transfer *transfer)
{
    struct DSL_context *devc;
    struct sr_datafeed_packet packet;
    struct ds_trigger_pos *trigger_pos;
    const struct sr_dev_inst *sdi;
    uint64_t remain_cnt;

    packet.status = SR_PKT_OK;
    devc = transfer->user_data;
    sdi = devc->cb_data;
    trigger_pos = (struct ds_trigger_pos *)transfer->buffer;

    if (devc->status != DSL_ABORT)
        devc->status = DSL_ERROR;
    if (!devc->abort && transfer->status == LIBUSB_TRANSFER_COMPLETED &&
        trigger_pos->check_id == TRIG_CHECKID) {
        sr_info("%llu: receive_trigger_pos(): status %d; timeout %d; received %d bytes.",
            (u64_t)g_get_monotonic_time(), transfer->status, transfer->timeout, transfer->actual_length);
        remain_cnt = trigger_pos->remain_cnt_h;
        remain_cnt = (remain_cnt << 32) + trigger_pos->remain_cnt_l;
        if (transfer->actual_length == dsl_header_size(devc)) {
            if (sdi->mode != LOGIC ||
                devc->stream ||
                remain_cnt < devc->limit_samples) {
                if (sdi->mode == LOGIC && (!devc->stream || (devc->status == DSL_ABORT))) {
                    devc->actual_samples = (devc->limit_samples - remain_cnt) & ~SAMPLES_ALIGN;
                    devc->actual_bytes = devc->actual_samples / DSLOGIC_ATOMIC_SAMPLES * dsl_en_ch_num(sdi) * DSLOGIC_ATOMIC_SIZE;
                    devc->actual_samples = devc->actual_bytes / dsl_en_ch_num(sdi) * 8;
                }

                packet.type = SR_DF_TRIGGER;
                packet.payload = trigger_pos;
                ds_data_forward(sdi, &packet);

                devc->status = DSL_DATA;
            }
        }
    } else if (!devc->abort) {
        sr_err("%s: trigger packet data error.", __func__);
        packet.type = SR_DF_TRIGGER;
        packet.payload = trigger_pos;
        packet.status = SR_PKT_DATA_ERROR;
        ds_data_forward(sdi, &packet);
    }

    free_transfer(transfer, 1);
}

SR_PRIV int dsl_start_transfers(const struct sr_dev_inst *sdi)
{
    struct DSL_context *devc;
    struct sr_usb_dev_inst *usb;
    struct libusb_transfer *transfer;
    unsigned int i, num_transfers;
    int ret;
    unsigned char *buf;
    size_t size;
    struct ds_trigger_pos *trigger_pos;

    devc = sdi->priv;
    usb = sdi->conn;

    num_transfers = get_number_of_transfers(sdi);
    size = get_buffer_size(sdi);

    /* trigger packet transfer */
    if (!(trigger_pos = g_try_malloc0(dsl_header_size(devc)))) {
        sr_err("%s: USB trigger_pos buffer malloc failed.", __func__);
        return SR_ERR_MALLOC;
    }

    devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * (num_transfers + 1));
    if (!devc->transfers) {
        sr_err("%s: USB transfer malloc failed.", __func__);
        return SR_ERR_MALLOC;
    }
    transfer = libusb_alloc_transfer(0);
    libusb_fill_bulk_transfer(transfer, usb->devhdl,
            6 | LIBUSB_ENDPOINT_IN, (unsigned char *)trigger_pos, dsl_header_size(devc),
            (libusb_transfer_cb_fn)receive_header, devc, 0);
    if ((ret = libusb_submit_transfer(transfer)) != 0) {
        sr_err("%s: Failed to submit trigger_pos transfer: %s.",
               __func__, libusb_error_name(ret));
        libusb_free_transfer(transfer);
        g_free(trigger_pos);
        devc->status = DSL_ERROR;
        return SR_ERR;
    } else {
        devc->num_transfers++;
        devc->transfers[0] = transfer;
        devc->submitted_transfers++;
    }

    /* data packet transfer */
    for (i = 1; i <= num_transfers; i++) {
        if (!(buf = g_try_malloc0(size))) {
            sr_err("%s: USB transfer buffer malloc failed.", __func__);
            return SR_ERR_MALLOC;
        }
        transfer = libusb_alloc_transfer(0);
        libusb_fill_bulk_transfer(transfer, usb->devhdl,
                6 | LIBUSB_ENDPOINT_IN, buf, size,
                (libusb_transfer_cb_fn)receive_transfer, devc, 0);
        if ((ret = libusb_submit_transfer(transfer)) != 0) {
            sr_err("%s: Failed to submit transfer: %s.",
                   __func__, libusb_error_name(ret));
            libusb_free_transfer(transfer);
            g_free(buf);
            devc->status = DSL_ERROR;
            devc->abort = TRUE;
            return SR_ERR;
        }
        devc->transfers[i] = transfer;
        devc->submitted_transfers++;
        devc->num_transfers++;
    }

    return SR_OK;
}


SR_PRIV int dsl_destroy_device(struct sr_dev_inst *sdi)
{ 
    assert(sdi);

    struct sr_dev_driver *driver;
    driver = sdi->driver;

    if (driver->dev_close){
		driver->dev_close(sdi);
    }

    if (sdi->conn) {
        if (sdi->dev_type == DEV_TYPE_USB)
            sr_usb_dev_inst_free(sdi->conn);
        else if (sdi->dev_type == DEV_TYPE_SERIAL)
            sr_serial_dev_inst_free(sdi->conn);
    }

    sr_dev_inst_free(sdi);
}

SR_PRIV int sr_option_value_to_code(int config_id, const char *value, const struct lang_text_map_item *array, int num)
{
    int i;
    struct lang_text_map_item *p;

    assert(array);
    assert(value);

    p = (struct lang_text_map_item*)array;

    for (i = 0; i < num; i++){
        if (p->config_id == config_id){
            if (strcmp(value, p->en_name) == 0){
                return p->id;
            }
            if (p->cn_name != NULL && strcmp(value, p->cn_name) == 0){
                return p->id;
            }
        }
        p++;
    }

    sr_err("Unkown lang text value:%s,config id:%d", value, config_id);

    return -1;
}


/*
 * security low level operations
 */
SR_PRIV int dsl_secuReset(const struct sr_dev_inst *sdi)
{
    if (dsl_wr_reg(sdi, SEC_CTRL_ADDR, 0) != SR_OK) goto Err;
    if (dsl_wr_reg(sdi, SEC_CTRL_ADDR + 1, 0) != SR_OK) goto Err;
    g_usleep(10*1000);
    if (dsl_wr_reg(sdi, SEC_CTRL_ADDR, 1) != SR_OK) goto Err;
    if (dsl_wr_reg(sdi, SEC_CTRL_ADDR + 1, 0) != SR_OK) goto Err;

    return SR_OK;
Err:
    sr_err("Sent dsl_wr_reg(SEC_XXX_ADDR) command failed.");
    return SR_ERR;
}

SR_PRIV int dsl_secuWrite(const struct sr_dev_inst *sdi, uint16_t cmd, uint16_t din)
{
    if (dsl_wr_reg(sdi, SEC_DATA_ADDR, din) != SR_OK) goto Err;
    if (dsl_wr_reg(sdi, SEC_DATA_ADDR + 1, (din >> 8)) != SR_OK) goto Err;
    if (dsl_wr_reg(sdi, SEC_CTRL_ADDR, cmd) != SR_OK) goto Err;
    if (dsl_wr_reg(sdi, SEC_CTRL_ADDR + 1, (cmd >> 8)) != SR_OK) goto Err;

    return SR_OK;
Err:
    sr_err("Sent dsl_wr_reg(SEC_XXX_ADDR) command failed.");
    return SR_ERR;
}

SR_PRIV gboolean dsl_isSecuReady(const struct sr_dev_inst *sdi)
{
    uint8_t temp;
    if (dsl_rd_reg(sdi, SEC_CTRL_ADDR, &temp) != SR_OK) goto Err;

    if (temp & bmSECU_READY)
        return TRUE;
    else
        return FALSE;
Err:
    sr_err("Sent dsl_rd_reg(SEC_XXX_ADDR) command failed.");
    return FALSE;
}

SR_PRIV gboolean dsl_isSecuPass(const struct sr_dev_inst *sdi)
{
    uint8_t temp;
    if (dsl_rd_reg(sdi, SEC_CTRL_ADDR, &temp) != SR_OK) goto Err;

    if (temp & bmSECU_PASS)
        return TRUE;
    else
        return FALSE;
Err:
    sr_err("Sent dsl_rd_reg(SEC_XXX_ADDR) command failed.");
    return FALSE;
}

SR_PRIV uint16_t dsl_secuRead(const struct sr_dev_inst *sdi)
{
    uint16_t sec;
    if (dsl_rd_reg(sdi, SEC_DATA_ADDR + 1, (uint8_t*)&sec) != SR_OK) goto Err;
    sec <<= 8;
    if (dsl_rd_reg(sdi, SEC_DATA_ADDR, (uint8_t*)&sec) != SR_OK) goto Err;

    return sec;
Err:
    sr_err("Sent dsl_rd_reg(SEC_XXX_ADDR) command failed.");
    return 0;
}


/*
 * security API interface
 */
SR_PRIV int dsl_secuCheck(const struct sr_dev_inst *sdi, uint16_t* encryption, int steps)
{
    int tryCnt = SECU_TRY_CNT;

    dsl_secuReset(sdi);
    if (dsl_isSecuPass(sdi))
        return SR_ERR;
    dsl_secuWrite(sdi, SECU_START, 0);
    while(steps--) {
        if (dsl_isSecuPass(sdi))
            return SR_ERR;
        while(!dsl_isSecuReady(sdi)) {
            if (tryCnt-- == 0) {
                sr_err("Get security ready failed.");
                return SR_ERR;
            }
        }
        if (dsl_secuRead(sdi) != 0)
            return SR_ERR;
        dsl_secuWrite(sdi, SECU_CHECK, encryption[steps]);
    }

    return SR_OK;
}
