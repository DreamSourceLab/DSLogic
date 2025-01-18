/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 * Copyright (C) 2013 DreamSourceLab <support@dreamsourcelab.com>
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

#include "deviceoptions.h"

#include <boost/bind.hpp>
#include <QObject>
#include <stdint.h>
#include "../bool.h"
#include "../double.h"
#include "../enum.h"
#include "../int.h"
#include "../../config/appconfig.h"
#include "../../log.h"
#include "../../appcontrol.h"
#include "../../sigsession.h"
#include "../../deviceagent.h"
#include "../../ui/langresource.h"
 
using namespace std;

namespace pv {
namespace prop {
namespace binding {

DeviceOptions::DeviceOptions()
{
	GVariant *gvar_opts, *gvar_list;
	gsize num_opts;

	SigSession *session = AppControl::Instance()->GetSession();
	_device_agent = session->get_device();

	gvar_opts = _device_agent->get_config_list(NULL, SR_CONF_DEVICE_OPTIONS);
	 
    if (gvar_opts == NULL)
		/* Driver supports no device instance options. */
		return;

	const int *const options = (const int32_t *)g_variant_get_fixed_array(
		gvar_opts, &num_opts, sizeof(int32_t));
	
	for (unsigned int i = 0; i < num_opts; i++) {
		const struct sr_config_info *const info =
			_device_agent->get_config_info(options[i]);

		if (!info)
			continue;

		const int key = info->key;

		gvar_list = _device_agent->get_config_list(NULL, key);

        const QString name(info->name);
        const char *label_char = info->name;
        QString label(label_char);

		switch(key)
		{
		case SR_CONF_SAMPLERATE:
            bind_samplerate(name, label, gvar_list);
			break;

		case SR_CONF_CAPTURE_RATIO:
            bind_int(name, label, key, "%", pair<int64_t, int64_t>(0, 100));
			break;

		case SR_CONF_PATTERN_MODE:
		case SR_CONF_BUFFERSIZE:
		case SR_CONF_TRIGGER_SOURCE:		
        case SR_CONF_MAX_HEIGHT:
        case SR_CONF_MAX_HEIGHT_VALUE:
        case SR_CONF_PROBE_COUPLING:
        case SR_CONF_PROBE_EN:
        case SR_CONF_ZERO:
        case SR_CONF_STREAM:
        case SR_CONF_TEST:
        case SR_CONF_STATUS:
        case SR_CONF_PROBE_FACTOR:
            bind_enum(name, label, key, gvar_list);
			break;

		case SR_CONF_OPERATION_MODE:
        case SR_CONF_BUFFER_OPTIONS:
        case SR_CONF_THRESHOLD:
		case SR_CONF_FILTER: 
			bind_list(name, label, key, gvar_list);
			break;

        case SR_CONF_VTH:
            bind_double(name, label, key, "V", pair<double, double>(0.0, 5.0), 1, 0.1);
            break;

		case SR_CONF_RLE:
        case SR_CONF_RLE_SUPPORT:
        case SR_CONF_CLOCK_TYPE:
        case SR_CONF_CLOCK_EDGE:
        case SR_CONF_INSTANT:
            bind_bool(name, label, key);
            break;

        case SR_CONF_EXT_SAMPLERATE:
            bind_int(name, label, key, "", pair<int64_t, int64_t>(10000, 1000000000));
            break;

		case SR_CONF_TIMEBASE:
            bind_enum(name, label, key, gvar_list, print_timebase);
			break;

        case SR_CONF_PROBE_VDIV:
            bind_enum(name, label, key, gvar_list, print_vdiv);
            break;

        case SR_CONF_BANDWIDTH_LIMIT:
            bind_bandwidths(name, label, key, gvar_list);
            break;

        default:
            gvar_list = NULL;
		}

		if (gvar_list)
			g_variant_unref(gvar_list);
	}
    if (gvar_opts)
        g_variant_unref(gvar_opts);
}

GVariant* DeviceOptions::config_getter(int key)
{ 
	SigSession *session = AppControl::Instance()->GetSession();
	DeviceAgent *_device_agent = session->get_device();	
	return _device_agent->get_config(key);
}

void DeviceOptions::config_setter(int key, GVariant* value)
{
	SigSession *session = AppControl::Instance()->GetSession();
	DeviceAgent *_device_agent = session->get_device();
    _device_agent->set_config(key, value);
}

void DeviceOptions::bind_bool(const QString &name, const QString label, int key)
{
	QString text = LangResource::Instance()->get_lang_text(STR_PAGE_DSL, label.toLocal8Bit().data(), label.toLocal8Bit().data());
	_properties.push_back(
        new Bool(name, text, bind(config_getter, key),
			bind(config_setter, key, _1)));
}

void DeviceOptions::bind_enum(const QString &name, const QString label, int key,
    GVariant *const gvar_list, boost::function<QString (GVariant*)> printer)
{
	GVariant *gvar;
	GVariantIter iter;
	std::vector< pair<GVariant*, QString> > values;

	assert(gvar_list);

	g_variant_iter_init (&iter, gvar_list);

	while ((gvar = g_variant_iter_next_value (&iter)))
	{
		QString v = printer(gvar);
		values.push_back(make_pair(gvar, v));
	}

	_properties.push_back(
        new Enum(name, label, values,
			bind(config_getter, key),
			bind(config_setter, key, _1)));
}

void DeviceOptions::bind_int(const QString &name, const QString label, int key, QString suffix,
    boost::optional< std::pair<int64_t, int64_t> > range)
{
	_properties.push_back(
        new Int(name, label, suffix, range,
			bind(config_getter, key),
			bind(config_setter, key, _1)));
}

void DeviceOptions::bind_double(const QString &name, const QString label, int key, QString suffix,
    boost::optional< std::pair<double, double> > range,
    int decimals, boost::optional<double> step)
{
    _properties.push_back(
        new Double(name, label, decimals, suffix, range, step,
            bind(config_getter, key),
            bind(config_setter, key, _1)));
}

QString DeviceOptions::print_gvariant(GVariant *const gvar)
{
	QString s;

	if (g_variant_is_of_type(gvar, G_VARIANT_TYPE("s"))){
        s = QString::fromUtf8(g_variant_get_string(gvar, NULL));
	}
	else
	{
		gchar *const text = g_variant_print(gvar, FALSE);
        s = QString::fromUtf8(text);
		g_free(text);
	}

	return s;
}

void DeviceOptions::bind_samplerate(const QString &name, const QString label,
    GVariant *const gvar_list)
{
	GVariant *gvar_list_samplerates;

	assert(gvar_list);

	if ((gvar_list_samplerates = g_variant_lookup_value(gvar_list,
			"samplerate-steps", G_VARIANT_TYPE("at"))))
	{
		gsize num_elements;
		const uint64_t *const elements =
			(const uint64_t *)g_variant_get_fixed_array(
				gvar_list_samplerates, &num_elements, sizeof(uint64_t));

		assert(num_elements == 3);

		_properties.push_back(
			//tr
            new Double(name, label, 0, L_S(STR_PAGE_DLG, S_ID(IDS_DLG_HZ), "Hz"),
				make_pair((double)elements[0], (double)elements[1]),
						(double)elements[2],
				bind(samplerate_double_getter),
				bind(samplerate_double_setter, _1)));

		g_variant_unref(gvar_list_samplerates);
	}
	else if ((gvar_list_samplerates = g_variant_lookup_value(gvar_list,
			"samplerates", G_VARIANT_TYPE("at"))))
	{
        bind_enum(name, label, SR_CONF_SAMPLERATE,
			gvar_list_samplerates, print_samplerate);
		g_variant_unref(gvar_list_samplerates);
	}
}

QString DeviceOptions::print_samplerate(GVariant *const gvar)
{
	char *const s = sr_samplerate_string(g_variant_get_uint64(gvar));
	const QString qstring(s);
	g_free(s);
	return qstring;
}

GVariant* DeviceOptions::samplerate_double_getter()
{
    GVariant *const gvar = config_getter(SR_CONF_SAMPLERATE);

	if(!gvar)
		return NULL;

	GVariant *const gvar_double = g_variant_new_double(
		g_variant_get_uint64(gvar));

	g_variant_unref(gvar);

	return gvar_double;
}

void DeviceOptions::samplerate_double_setter(GVariant *value)
{
	GVariant *const gvar = g_variant_new_uint64(
		g_variant_get_double(value));
	config_setter(SR_CONF_SAMPLERATE, gvar);
}

QString DeviceOptions::print_timebase(GVariant *const gvar)
{
	uint64_t p, q;
	g_variant_get(gvar, "(tt)", &p, &q);
	char *pstr = sr_period_string(p * q);
	QString qstr(pstr);
	g_free(pstr);
	return qstr;
}

QString DeviceOptions::print_vdiv(GVariant *const gvar)
{
	uint64_t p, q;
	g_variant_get(gvar, "(tt)", &p, &q);
	char *pstr = sr_voltage_string(p, q);
	QString qstr(pstr);
	g_free(pstr);
	return qstr;
}

void DeviceOptions::bind_bandwidths(const QString &name, const QString label, int key,
    GVariant *const gvar_list, boost::function<QString (GVariant*)> printer)
{
	(void)printer;

	bool bw_limit = false;
	GVariant *gvar;
	std::vector< pair<GVariant*, QString> > values;
	struct sr_list_item *plist;

	assert(gvar_list);
	plist = (struct sr_list_item*)g_variant_get_uint64(gvar_list);
	assert(plist);

	_device_agent->get_config_bool(SR_CONF_BANDWIDTH, bw_limit);

    if (bw_limit == false){
        return;
	}

	while (plist && plist->id >= 0)
	{ 
		QString v = LangResource::Instance()->get_lang_text(STR_PAGE_DSL, plist->name, plist->name);
		gvar = g_variant_new_int16(plist->id);
		values.push_back(make_pair(gvar, v));
        plist++;
	}

	_properties.push_back(
        new Enum(name, label, values,
			bind(config_getter, key),
			bind(config_setter, key, _1)));
}

void DeviceOptions::bind_list(const QString &name, const QString label, int key, GVariant *const gvar_list)
{
	GVariant *gvar;
	std::vector< pair<GVariant*, QString> > values;
	struct sr_list_item *plist;

	assert(gvar_list);
	plist = (struct sr_list_item*)g_variant_get_uint64(gvar_list);
	assert(plist);

	while (plist && plist->id >= 0)
	{ 
		QString v = LangResource::Instance()->get_lang_text(STR_PAGE_DSL, plist->name, plist->name);
		gvar = g_variant_new_int16(plist->id);
		values.push_back(make_pair(gvar, v));
        plist++;
	}

	_properties.push_back(
        new Enum(name, label, values,
			bind(config_getter, key),
			bind(config_setter, key, _1)));
}

} // binding
} // prop
} // pv

