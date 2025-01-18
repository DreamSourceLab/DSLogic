/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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
 
#include "libsigrok-internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <glib.h>
#include "config.h" /* Needed for HAVE_LIBUSB_1_0 and others. */
#include "log.h"
#include <assert.h>

#undef LOG_PREFIX
#define LOG_PREFIX "hwdriver: "

/**
 * @file
 *
 * Hardware driver handling in libsigrok.
 */

/**
 * @defgroup grp_driver Hardware drivers
 *
 * Hardware driver handling in libsigrok.
 *
 * @{
 */
static struct sr_config_info sr_config_info_data[] = {
    {SR_CONF_CONN, SR_T_CHAR, "Connection"},    
	{SR_CONF_SERIALCOMM, SR_T_CHAR,"Serial communication"},
	{SR_CONF_SAMPLERATE, SR_T_UINT64,"Sample rate"},
    {SR_CONF_LIMIT_SAMPLES, SR_T_UINT64,"Sample count"},
    {SR_CONF_ACTUAL_SAMPLES, SR_T_UINT64,"Sample count-actual"},
    {SR_CONF_CLOCK_TYPE, SR_T_BOOL,"Using External Clock"},
		{SR_CONF_EXT_SAMPLERATE, SR_T_UINT64, "Ext. Clock Frequency [Hz]"},
    {SR_CONF_CLOCK_EDGE, SR_T_BOOL, "Using Clock Negedge"},
    {SR_CONF_CAPTURE_RATIO, SR_T_UINT64,"Pre-trigger capture ratio"},
    {SR_CONF_PATTERN_MODE, SR_T_CHAR,"Operation Mode"},
	{SR_CONF_RLE, SR_T_BOOL,"Run Length Encoding"},
    {SR_CONF_WAIT_UPLOAD, SR_T_BOOL,"Wait Buffer Upload"},
    {SR_CONF_TRIGGER_SLOPE, SR_T_UINT8,"Trigger slope"},
    {SR_CONF_TRIGGER_SOURCE, SR_T_UINT8,"Trigger source"},
    {SR_CONF_TRIGGER_CHANNEL, SR_T_UINT8,"Trigger channel"},
    {SR_CONF_HORIZ_TRIGGERPOS, SR_T_UINT8,"Horizontal trigger position"},
    {SR_CONF_TRIGGER_HOLDOFF, SR_T_UINT64,"Trigger hold off"},
    {SR_CONF_TRIGGER_MARGIN, SR_T_UINT8,"Trigger margin"},
    {SR_CONF_BUFFERSIZE, SR_T_UINT64,"Buffer size"},
    {SR_CONF_TIMEBASE, SR_T_UINT64,"Time base"},
    {SR_CONF_MAX_HEIGHT, SR_T_CHAR,"Max Height"},
    {SR_CONF_MAX_HEIGHT_VALUE, SR_T_UINT8,"Max Height value"},
	{SR_CONF_FILTER, SR_T_LIST,"Filter Targets"},
	{SR_CONF_DATALOG, SR_T_BOOL,"Datalog"},
    {SR_CONF_OPERATION_MODE, SR_T_LIST,"Operation Mode"},
    {SR_CONF_BUFFER_OPTIONS, SR_T_LIST,"Stop Options"},
    {SR_CONF_CHANNEL_MODE, SR_T_LIST,"Channel Mode"},
    {SR_CONF_THRESHOLD, SR_T_LIST,"Threshold Levels"},
    {SR_CONF_VTH, SR_T_FLOAT,"Threshold Level"},
    {SR_CONF_RLE_SUPPORT, SR_T_BOOL,"Enable RLE Compress"},
    {SR_CONF_BANDWIDTH_LIMIT, SR_T_LIST,"Bandwidth Limit"},
    {SR_CONF_PROBE_COUPLING, SR_T_CHAR,"Coupling"},
    {SR_CONF_PROBE_VDIV, SR_T_RATIONAL_VOLT,"Volts/div"},
    {SR_CONF_PROBE_FACTOR, SR_T_UINT64,"Probe Factor"},
    {SR_CONF_PROBE_MAP_DEFAULT, SR_T_BOOL,"Map Default"},
    {SR_CONF_PROBE_MAP_UNIT, SR_T_CHAR,"Map Unit"},
    {SR_CONF_PROBE_MAP_MIN, SR_T_FLOAT,"Map Min"},
    {SR_CONF_PROBE_MAP_MAX, SR_T_FLOAT,"Map Max"},
    {0, 0, NULL},
};


/** @cond PRIVATE */
#ifdef HAVE_LA_DEMO
extern SR_PRIV struct sr_dev_driver demo_driver_info;
#endif
#ifdef HAVE_DSL_DEVICE
extern SR_PRIV struct sr_dev_driver DSLogic_driver_info;
extern SR_PRIV struct sr_dev_driver DSCope_driver_info;
#endif
/** @endcond */

static struct sr_dev_driver *drivers_list[] = {
#ifdef HAVE_LA_DEMO
	&demo_driver_info,
#endif
#ifdef HAVE_DSL_DEVICE
    &DSLogic_driver_info,
    &DSCope_driver_info,
#endif
	NULL,
};

/**
 * Return the list of supported hardware drivers.
 *
 * @return Pointer to the NULL-terminated list of hardware driver pointers.
 */
SR_PRIV struct sr_dev_driver **sr_driver_list(void)
{
	return drivers_list;
}


/**
 * Initialize a hardware driver.
 *
 * This usually involves memory allocations and variable initializations
 * within the driver, but _not_ scanning for attached devices.
 *
 * @param ctx A libsigrok context object allocated by a previous call to
 *            sr_init(). Must not be NULL.
 * @param driver The driver to initialize. This must be a pointer to one of
 *               the entries returned by sr_driver_list(). Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid parameters,
 *         SR_ERR_BUG upon internal errors, or another negative error code
 *         upon other errors.
 */
SR_PRIV int sr_driver_init(struct sr_context *ctx, struct sr_dev_driver *driver)
{
	int ret;

	if (!ctx) {
		sr_err("Invalid libsigrok context, can't initialize.");
		return SR_ERR_ARG;
	}

	if (!driver) {
		sr_err("Invalid driver, can't initialize.");
		return SR_ERR_ARG;
	}

	sr_detail("Initializing driver '%s'.", driver->name);
	if ((ret = driver->init(ctx)) < 0)
		sr_err("Failed to initialize the driver: %d.", ret);

	return ret;
}

/** @private */
SR_PRIV void sr_hw_cleanup_all(void)
{
	int i;
	struct sr_dev_driver **drivers;

	drivers = sr_driver_list();
	for (i = 0; drivers[i]; i++) {
		if (drivers[i]->cleanup)
			drivers[i]->cleanup();
	}
}

/** A floating reference can be passed in for data. */
SR_PRIV struct sr_config *sr_config_new(int key, GVariant *data)
{
	struct sr_config *src;
	assert(data);

	if (!(src = g_try_malloc0(sizeof(struct sr_config)))){
		sr_err("%s,ERROR:failed to alloc memory.", __func__);
		return NULL;
	}
	//not need init.

	src->key = key;
	src->data = g_variant_ref_sink(data);
	return src;
}

SR_PRIV void sr_config_free(struct sr_config *src)
{

	if (!src || !src->data) {
		sr_err("%s: invalid data!", __func__);
		return;
	}

	g_variant_unref(src->data);
	g_free(src);

}

/**
 * Returns information about the given driver or device instance.
 *
 * @param driver The sr_dev_driver struct to query.
 * @param key The configuration key (SR_CONF_*).
 * @param data Pointer to a GVariant where the value will be stored. Must
 *             not be NULL. The caller is given ownership of the GVariant
 *             and must thus decrease the refcount after use. However if
 *             this function returns an error code, the field should be
 *             considered unused, and should not be unreferenced.
 * @param sdi (optional) If the key is specific to a device, this must
 *            contain a pointer to the struct sr_dev_inst to be checked.
 *            Otherwise it must be NULL.
 *
 * @return SR_OK upon success or SR_ERR in case of error. Note SR_ERR_ARG
 *         may be returned by the driver indicating it doesn't know that key,
 *         but this is not to be flagged as an error by the caller; merely
 *         as an indication that it's not applicable.
 */
SR_PRIV int sr_config_get(const struct sr_dev_driver *driver,
                         const struct sr_dev_inst *sdi,
                         const struct sr_channel *ch,
                         const struct sr_channel_group *cg,
                         int key, GVariant **data)
{
	int ret;

	if (!driver || !data)
		return SR_ERR;

	if (!driver->config_get)
		return SR_ERR_ARG;

    if ((ret = driver->config_get(key, data, sdi, ch, cg)) == SR_OK) {
		/* Got a floating reference from the driver. Sink it here,
		 * caller will need to unref when done with it. */
		assert(*data);
		g_variant_ref_sink(*data);
	}

	return ret;
}

/**
 * Set a configuration key in a device instance.
 *
 * @param sdi The device instance.
 * @param key The configuration key (SR_CONF_*).
 * @param data The new value for the key, as a GVariant with GVariantType
 *        appropriate to that key. A floating reference can be passed
 *        in; its refcount will be sunk and unreferenced after use.
 *
 * @return SR_OK upon success or SR_ERR in case of error. Note SR_ERR_ARG
 *         may be returned by the driver indicating it doesn't know that key,
 *         but this is not to be flagged as an error by the caller; merely
 *         as an indication that it's not applicable.
 */
SR_PRIV int sr_config_set(struct sr_dev_inst *sdi,
                         struct sr_channel *ch,
                         struct sr_channel_group *cg,
                         int key, GVariant *data)
{
	int ret;

	assert(data);
	g_variant_ref_sink(data);

	if (!sdi || !sdi->driver || !data){
		ret = SR_ERR;
	}
	else if (!sdi->driver->config_set){
		ret = SR_ERR_ARG;
	}
	else {
        ret = sdi->driver->config_set(key, data, sdi, ch, cg);
	}

	g_variant_unref(data);

	return ret;
}

/**
 * List all possible values for a configuration key.
 *
 * @param driver The sr_dev_driver struct to query.
 * @param key The configuration key (SR_CONF_*).
 * @param data A pointer to a GVariant where the list will be stored. The
 *             caller is given ownership of the GVariant and must thus
 *             unref the GVariant after use. However if this function
 *             returns an error code, the field should be considered
 *             unused, and should not be unreferenced.
 * @param sdi (optional) If the key is specific to a device, this must
 *            contain a pointer to the struct sr_dev_inst to be checked.
 *
 * @return SR_OK upon success or SR_ERR in case of error. Note SR_ERR_ARG
 *         may be returned by the driver indicating it doesn't know that key,
 *         but this is not to be flagged as an error by the caller; merely
 *         as an indication that it's not applicable.
 */
SR_PRIV int sr_config_list(const struct sr_dev_driver *driver,
                          const struct sr_dev_inst *sdi,
                          const struct sr_channel_group *cg,
                          int key, GVariant **data)
{
	int ret;

	if (!driver || !data){
		ret = SR_ERR;
	}
	else if (!driver->config_list){
		ret = SR_ERR_ARG;
	}
    else if ((ret = driver->config_list(key, data, sdi, cg)) == SR_OK){
		assert(*data);
		g_variant_ref_sink(*data);
	}

	return ret;
}

/**
 * Get information about a configuration key.
 *
 * @param key The configuration key.
 *
 * @return A pointer to a struct sr_config_info, or NULL if the key
 *         was not found.
 */
SR_PRIV const struct sr_config_info *sr_config_info_get(int key)
{
	int i;

	for (i = 0; sr_config_info_data[i].key; i++) {
		if (sr_config_info_data[i].key == key)
			return &sr_config_info_data[i];
	}

	return NULL;
}

/**
 * Get status about an acquisition
 *
 * @param sdi The device instance.
 * @param status A pointer to a struct sr_capture_status.
 *
 * @return SR_OK upon success or SR_ERR in case of error. Note SR_ERR_ARG
 *         may be returned by the driver indicating it doesn't know that key,
 *         but this is not to be flagged as an error by the caller; merely
 *         as an indication that it's not applicable.
 */
SR_PRIV int sr_status_get(const struct sr_dev_inst *sdi,
                         struct sr_status *status, gboolean prg)
{
    int ret;

    if (!sdi->driver)
        ret = SR_ERR;
    else if (!sdi->driver->dev_status_get)
        ret = SR_ERR_ARG;
    else
        ret = sdi->driver->dev_status_get(sdi, status, prg);

    return ret;
}

/* Unnecessary level of indirection follows. */

/** @private */
SR_PRIV int sr_source_remove(int fd)
{
	return sr_session_source_remove(fd);
}

/** @private */
SR_PRIV int sr_source_add(int fd, int events, int timeout,
			  sr_receive_data_callback_t cb, void *cb_data)
{
    return sr_session_source_add(fd, events, timeout, cb, cb_data);
}

SR_PRIV int ds_scan_all_device_list(libusb_context *usb_ctx,struct libusb_device **list_buf, int size, int *count)
{	
	libusb_device **devlist;
	int i;
	int wr;
    int ret;
	struct libusb_device_descriptor des;

	assert(list_buf);
	assert(count);
	assert(usb_ctx);

	devlist = NULL;
	wr = 0;
	libusb_get_device_list(usb_ctx, &devlist);

	if (devlist == NULL){
        sr_info("%s: Failed to call libusb_get_device_list(), it returns a null list.", __func__);
        return SR_ERR_CALL_STATUS;
    }

	for (i = 0; devlist[i]; i++) 
	{ 
        ret = libusb_get_device_descriptor(devlist[i], &des);
        if (ret != 0) {
            sr_warn("Failed to get device descriptor: %s.",  libusb_error_name(ret));
            continue;
        }

		if (des.idVendor == DS_VENDOR_ID){
			if (wr >= size){
				sr_err("ds_scan_all_device_list(), buffer length is too short.");
				break;
			}

			list_buf[wr] = devlist[i];
			wr++;
		}
	}

	*count = wr;

	libusb_free_device_list(devlist, 0);

	return SR_OK;
}

/** @} */
