/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 * 
 * Copyright (C) 2022 DreamSourceLab <support@dreamsourcelab.com>
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

#include "deviceagent.h"
#include <assert.h>
#include "log.h"


DeviceAgent::DeviceAgent()
{
    _dev_handle = NULL_HANDLE;
    _di = NULL;
    _dev_type = 0;
    _callback = NULL;
    _is_new_device = false;
}

void DeviceAgent::update()
{
    _dev_handle = NULL_HANDLE;
    _dev_name = "";
    _path = "";
    _di = NULL;
    _dev_type = 0;
    _is_new_device = false;

    struct ds_device_full_info info;

    if (ds_get_actived_device_info(&info) == SR_OK)
    {
        _dev_handle = info.handle;
        _dev_type = info.dev_type;
        _di = info.di;
        _is_new_device = info.actived_times == 1;

        _dev_name = QString::fromLocal8Bit(info.name);
        _driver_name = QString::fromLocal8Bit(info.driver_name);

        if (info.path[0] != '\0'){
            _path = QString::fromLocal8Bit(info.path);
        } 
    }
}

 sr_dev_inst* DeviceAgent::inst()
 {
    assert(_dev_handle);
    return _di;
 }

bool DeviceAgent::enable_probe(const sr_channel *probe, bool enable)
{
    assert(_dev_handle);

    if (ds_enable_device_channel(probe, enable) == SR_OK){
        config_changed();
        return true;
    }
    return false;
}

bool DeviceAgent::enable_probe(int probe_index, bool enable)
{
     assert(_dev_handle);

     if (ds_enable_device_channel_index(probe_index, enable) == SR_OK){
        config_changed();
        return true;
    }
    return false;
}

bool DeviceAgent::set_channel_name(int ch_index, const char *name)
{
    assert(_dev_handle);
    
    if (ds_set_device_channel_name(ch_index, name) == SR_OK){
        config_changed();
        return true;
    }
    return false;
}

uint64_t DeviceAgent::get_sample_limit()
{
    assert(_dev_handle);

    uint64_t v;
    GVariant* gvar = NULL;

    ds_get_actived_device_config(NULL, NULL, SR_CONF_LIMIT_SAMPLES, &gvar);

	if (gvar != NULL) {
        v = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
	}
    else {
		v = 0U;
	}

	return v;
}

uint64_t DeviceAgent::get_sample_rate()
{
    assert(_dev_handle);

    uint64_t v;
    GVariant* gvar = NULL;

    int key;

    if(is_external_clock()) {
        key = SR_CONF_EXT_SAMPLERATE;
    } else {
        key = SR_CONF_SAMPLERATE;
    }

    ds_get_actived_device_config(NULL, NULL, key, &gvar);
	if (gvar != NULL) {
        v = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
	}
    else {
		v = 0U;
	}

	return v;
}

bool DeviceAgent::is_external_clock()
{
    assert(_dev_handle);

    bool ct = false;
    GVariant* gvar = NULL;

    ds_get_actived_device_config(NULL, NULL, SR_CONF_CLOCK_TYPE, &gvar);
    if (gvar != NULL) {
        ct = g_variant_get_boolean(gvar);
        g_variant_unref(gvar);
    }

    return ct;
}

uint64_t DeviceAgent::get_time_base()
{
    assert(_dev_handle);

    uint64_t v;
    GVariant* gvar = NULL;

    ds_get_actived_device_config(NULL, NULL, SR_CONF_TIMEBASE, &gvar);

	if (gvar != NULL) {
        v = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
	}
    else {
		v = 0U;
	}

	return v;
}

double DeviceAgent::get_sample_time()
{
    assert(_dev_handle);

    uint64_t sample_rate = get_sample_rate();
    uint64_t sample_limit = get_sample_limit();
    double sample_time;

    if (sample_rate == 0)
        sample_time = 0;
    else
        sample_time = sample_limit * 1.0 / sample_rate;

    return sample_time;
}

const GSList* DeviceAgent::get_device_mode_list()
{
    assert(_dev_handle);
    return ds_get_actived_device_mode_list();
}

bool DeviceAgent::is_trigger_enabled()
{
    assert(_dev_handle);
    if (ds_trigger_is_enabled() > 0){
        return true;
    }
    return false;
}

bool DeviceAgent::start()
{
    assert(_dev_handle);

    if (ds_start_collect() == SR_OK){
        return true;
    }
    return false;
}

bool DeviceAgent::stop()
{
    assert(_dev_handle);

    if (ds_stop_collect() == SR_OK){
        return true;
    }
    return false;
}

void DeviceAgent::release()
{
    ds_release_actived_device();
}

bool DeviceAgent::have_enabled_channel()
{
    assert(_dev_handle);
    return ds_channel_is_enabled() > 0;
}

void DeviceAgent::config_changed()
{
    if (_callback != NULL){
        _callback->DeviceConfigChanged();
    }
}

bool DeviceAgent::channel_is_enable(int index)
{  
    for (const GSList *l = get_channels(); l; l = l->next)
    {
        const sr_channel *const probe = (const sr_channel *)l->data;
        if (probe->index == index)
            return probe->enabled;          
    }

    return false;
}
 
//---------------device config-----------/

int DeviceAgent::get_work_mode()
{
    return ds_get_actived_device_mode();
}

const struct sr_config_info *DeviceAgent::get_config_info(int key)
{
    return ds_get_actived_device_config_info(key);
}

bool DeviceAgent::get_device_status(struct sr_status &status, gboolean prg)
{   
    assert(_dev_handle);

    if (ds_get_actived_device_status(&status, prg) == SR_OK)
    {
        return true;
    }
    return false;
}

struct sr_config *DeviceAgent::new_config(int key, GVariant *data)
{
    return ds_new_config(key, data);
}

void DeviceAgent::free_config(struct sr_config *src)
{
    ds_free_config(src);
}

bool DeviceAgent::is_collecting()
{
    return ds_is_collecting() > 0;
}

GSList *DeviceAgent::get_channels()
{
    assert(_dev_handle);
    return ds_get_actived_device_channels();
}

 int DeviceAgent::get_hardware_operation_mode()
 {
    assert(_dev_handle);

    int mode_val = 0;
    if (get_config_int16(SR_CONF_OPERATION_MODE, mode_val)){                  
        return mode_val;
    }
    return -1;
 }

 bool DeviceAgent::is_stream_mode()
 { 
    return get_hardware_operation_mode() == LO_OP_STREAM;
 }

 bool DeviceAgent::check_firmware_version()
 {
    assert(_dev_handle);

    int st = -1;
    if (ds_get_actived_device_init_status(&st) == SR_OK){
        if (st == SR_ST_INCOMPATIBLE){
            return false;
        }
    }
    return true;
 }

 QString DeviceAgent::get_demo_operation_mode()
 {
    assert(_dev_handle);

    if (is_demo() == false){
        assert(false);
    }        
    
    QString pattern_mode;
    if(get_config_string(SR_CONF_PATTERN_MODE, pattern_mode) == false)
    {
        assert(false);
    }
    return pattern_mode;
 }

GVariant* DeviceAgent::get_config_list(const sr_channel_group *group, int key)
{
    assert(_dev_handle);

    GVariant *data = NULL;

    int ret = ds_get_actived_device_config_list(group, key, &data);
    if (ret != SR_OK){
        if (ret != SR_ERR_NA)
            dsv_detail("%s%d", "WARNING: Failed to get config list, key:", key); 
        
        if (data != NULL){
            dsv_warn("%s%d", "WARNING: Failed to get config list, but data is not null. key:", key); 
        }
        data = NULL;
    }

    return data;
}

GVariant* DeviceAgent::get_config(int key, const sr_channel *ch, const sr_channel_group *cg)
{
    assert(_dev_handle); 
    GVariant *data = NULL;
    
    int ret = ds_get_actived_device_config(ch, cg, key, &data);
    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR:DeviceAgent::get_config, Failed to get value of config id:", key);
    }
    return data;
}

bool DeviceAgent::have_config(int key, const sr_channel *ch, const sr_channel_group *cg)
{
    GVariant *gvar = get_config(key, ch, cg);

    if (gvar != NULL){
        g_variant_unref(gvar);
        return true;
    }
    return false;
}

bool DeviceAgent::set_config(int key, GVariant *data, const sr_channel *ch, const sr_channel_group *cg)
 {
    assert(_dev_handle);

    int ret = ds_set_actived_device_config(ch, cg, key, data);
    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR:DeviceAgent::set_config, Failed to set value of config id:", key);
        return false;
    }

    config_changed();
    return true;
 }

 bool DeviceAgent::get_config_int32(int key, int &value, const sr_channel *ch, const sr_channel_group *cg)
 {  
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        value = g_variant_get_int32(gvar);
        g_variant_unref(gvar);
        return true;
    }

    return false;
 }

 bool DeviceAgent::set_config_int32(int key, int value, const sr_channel *ch, const sr_channel_group *cg)
 {
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_int32(value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_int32, Failed to set value of config id:", key);
        return false;
    }
    return true;
 }

 bool DeviceAgent::get_config_string(int key, QString &value, const sr_channel *ch, const sr_channel_group *cg)
 {
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        const gchar *s = g_variant_get_string(gvar, NULL);
        value = QString(s);
        g_variant_unref(gvar);
        return true;
    }

    return false;
 }

 bool DeviceAgent::set_config_string(int key, const char *value, const sr_channel *ch, const sr_channel_group *cg)
 {
    assert(value);
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_string(value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_string, Failed to set value of config id:", key);
        return false;
    }
    return true;
 }

bool DeviceAgent::get_config_bool(int key, bool &value, const sr_channel *ch, const sr_channel_group *cg)
{
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        gboolean v = g_variant_get_boolean(gvar);
        value = v > 0;
        g_variant_unref(gvar);
        return true;
    }

    return false;
}

bool DeviceAgent::set_config_bool(int key, bool value, const sr_channel *ch, const sr_channel_group *cg)
{
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_boolean(value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_bool, Failed to set value of config id:", key);
        return false;
    }
    return true;
}

bool DeviceAgent::get_config_uint64(int key, uint64_t &value, const sr_channel *ch, const sr_channel_group *cg)
{
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        value = g_variant_get_uint64(gvar);
        g_variant_unref(gvar);
        return true;
    }

    return false;
}

bool DeviceAgent::set_config_uint64(int key, uint64_t value, const sr_channel *ch, const sr_channel_group *cg)
{
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_uint64(value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_uint64, Failed to set value of config id:", key);
        return false;
    }
    return true;
}

bool DeviceAgent::get_config_uint16(int key, int &value, const sr_channel *ch, const sr_channel_group *cg)
{
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        value = g_variant_get_uint16(gvar);
        g_variant_unref(gvar);
        return true;
    }

    return false;
}

bool DeviceAgent::set_config_uint16(int key, int value, const sr_channel *ch, const sr_channel_group *cg)
{
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_uint16(value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_uint16, Failed to set value of config id:", key);
        return false;
    }
    return true;
}

bool DeviceAgent::get_config_uint32(int key, uint32_t &value, const sr_channel *ch, const sr_channel_group *cg)
{
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        value = g_variant_get_uint32(gvar);
        g_variant_unref(gvar);
        return true;
    }

    return false;
}

bool DeviceAgent::set_config_uint32(int key, uint32_t value, const sr_channel *ch, const sr_channel_group *cg)
{
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_uint32(value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_uint32, Failed to set value of config id:", key);
        return false;
    }
    return true;
}

bool DeviceAgent::get_config_int16(int key, int &value, const sr_channel *ch, const sr_channel_group *cg)
{
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        value = g_variant_get_int16(gvar);
        g_variant_unref(gvar);
        return true;
    }

    return false;
}

bool DeviceAgent::set_config_int16(int key, int value, const sr_channel *ch, const sr_channel_group *cg)
{
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_int16(value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_int16, Failed to set value of config id:", key);
        return false;
    }
    return true;
}

bool DeviceAgent::get_config_byte(int key, int &value, const sr_channel *ch, const sr_channel_group *cg)
{
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        value = g_variant_get_byte(gvar);
        g_variant_unref(gvar);
        return true;
    }

    return false;
}

bool DeviceAgent::set_config_byte(int key, int value, const sr_channel *ch, const sr_channel_group *cg)
{
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_byte((uint8_t)value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_byte, Failed to set value of config id:", key);
        return false;
    }
    return true;
}

bool DeviceAgent::get_config_double(int key, double &value, const sr_channel *ch, const sr_channel_group *cg)
{
    GVariant* gvar = get_config(key, ch, cg);
    
    if (gvar != NULL) {
        value = g_variant_get_double(gvar);
        g_variant_unref(gvar);
        return true;
    }

    return false;
}

bool DeviceAgent::set_config_double(int key, double value, const sr_channel *ch, const sr_channel_group *cg)
{
    assert(_dev_handle);

    GVariant *gvar = g_variant_new_double(value);
    int ret = ds_set_actived_device_config(ch, cg, key, gvar);

    if (ret != SR_OK)
    {
        if (ret != SR_ERR_NA)
            dsv_err("%s%d", "ERROR: DeviceAgent::set_config_double, Failed to set value of config id:", key);
        return false;
    }
    return true;
}

//---------------device config end -----------/

