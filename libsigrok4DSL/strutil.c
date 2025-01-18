/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
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

#include "libsigrok-internal.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "log.h"

#undef LOG_PREFIX
#define LOG_PREFIX "strutil: "

/**
 * @file
 *
 * Helper functions for handling or converting libsigrok-related strings.
 */

/**
 * @defgroup grp_strutil String utilities
 *
 * Helper functions for handling or converting libsigrok-related strings.
 *
 * @{
 */

/**
 * Convert a numeric value value to its "natural" string representation.
 * in SI units
 *
 * E.g. a value of 3000000, with units set to "W", would be converted
 * to "3 MW", 20000 to "20 kW", 31500 would become "31.5 kW".
 *
 * @param x The value to convert.
 * @param unit The unit to append to the string, or NULL if the string
 *             has no units.
 *
 * @return A  g_try_malloc0()ed string representation of the samplerate value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_si_string_u64(uint64_t x, const char *unit)
{
	if (unit == NULL)
		unit = "";

    if ((x >= SR_GHZ(1)) && (x % SR_GHZ(1) == 0)) {
        return g_strdup_printf("%llu G%s", (u64_t)(x / SR_GHZ(1)), unit);
    } else if ((x >= SR_GHZ(1)) && (x % SR_GHZ(1) != 0)) {
        return g_strdup_printf("%llu.%llu G%s",
                       (u64_t)(x / SR_GHZ(1)), (u64_t)(x % SR_GHZ(1)), unit);
    } else if ((x >= SR_MHZ(1)) && (x % SR_MHZ(1) == 0)) {
        return g_strdup_printf("%llu M%s",
                       (u64_t)(x / SR_MHZ(1)), unit);
    } else if ((x >= SR_MHZ(1)) && (x % SR_MHZ(1) != 0)) {
        return g_strdup_printf("%llu.%llu M%s",
                    (u64_t)(x / SR_MHZ(1)), (u64_t)(x % SR_MHZ(1)), unit);
    } else if ((x >= SR_KHZ(1)) && (x % SR_KHZ(1) == 0)) {
        return g_strdup_printf("%llu k%s",
                    (u64_t)(x / SR_KHZ(1)), unit);
    } else if ((x >= SR_KHZ(1)) && (x % SR_KHZ(1) != 0)) {
        return g_strdup_printf("%llu.%llu k%s",
                    (u64_t)(x / SR_KHZ(1)), (u64_t)(x % SR_KHZ(1)), unit);
    } else {
        return g_strdup_printf("%llu %s", (u64_t)x, unit);
    }

	sr_err("%s: Error creating SI units string.", __func__);
	return NULL;
}

/**
 * Convert a numeric value value to its "natural" string representation.
 * in IEC units
 *
 * E.g. a value of 1024, with units set to "B", would be converted
 * to "1 kB", 16384 to "16 kB".
 *
 * @param x The value to convert.
 * @param unit The unit to append to the string, or NULL if the string
 *             has no units.
 *
 * @return A g_try_malloc0()ed string representation of the samplerate value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_iec_string_u64(uint64_t x, const char *unit)
{
    if (unit == NULL)
        unit = "";

    if ((x >= SR_GB(1)) && (x % SR_GB(1) == 0)) {
        return g_strdup_printf("%llu G%s", (u64_t)(x / SR_GB(1)), unit);
    } else if ((x >= SR_GB(1)) && (x % SR_GB(1) != 0)) {
        return g_strdup_printf("%llu.%llu G%s",
                       (u64_t)(x / SR_GB(1)), (u64_t)(x % SR_GB(1)), unit);
    } else if ((x >= SR_MB(1)) && (x % SR_MB(1) == 0)) {
        return g_strdup_printf("%llu M%s",
                       (u64_t)(x / SR_MB(1)), unit);
    } else if ((x >= SR_MB(1)) && (x % SR_MB(1) != 0)) {
        return g_strdup_printf("%llu.%llu M%s",
                    (u64_t)(x / SR_MB(1)), (u64_t)(x % SR_MB(1)), unit);
    } else if ((x >= SR_KB(1)) && (x % SR_KB(1) == 0)) {
        return g_strdup_printf("%llu k%s",
                    (u64_t)(x / SR_KB(1)), unit);
    } else if ((x >= SR_KB(1)) && (x % SR_KB(1) != 0)) {
        return g_strdup_printf("%llu.%llu K%s",
                    (u64_t)(x / SR_KB(1)), (u64_t)(x % SR_KB(1)), unit);
    } else {
        return g_strdup_printf("%llu %s", (u64_t)x, unit);
    }

    sr_err("%s: Error creating SI units string.", __func__);
    return NULL;
}

/**
 * Convert a numeric samplerate value to its "natural" string representation.
 *
 * E.g. a value of 3000000 would be converted to "3 MHz", 20000 to "20 kHz",
 * 31500 would become "31.5 kHz".
 *
 * @param samplerate The samplerate in Hz.
 *
 * @return A g_try_malloc0()ed string representation of the samplerate value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_samplerate_string(uint64_t samplerate)
{
	return sr_si_string_u64(samplerate, "Hz");
}

/**
 * Convert a numeric samplecount value to its "natural" string representation.
 *
 * E.g. a value of 16384 would be converted to "16 K"
 *
 * @param samplecount.
 *
 * @return A g_try_malloc0()ed string representation of the samplecount value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_samplecount_string(uint64_t samplecount)
{
    return sr_si_string_u64(samplecount, " Samples");
}

/**
 * Convert a numeric frequency value to the "natural" string representation
 * of its period.
 *
 * E.g. a value of 3000000 would be converted to "3 us", 20000 to "50 ms".
 *
 * @param frequency The frequency in Hz.
 *
 * @return A g_try_malloc0()ed string representation of the frequency value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_period_string(uint64_t frequency)
{
	char *o;
	int r;

	/* Allocate enough for a uint64_t as string + " ms". */
	if (!(o = g_try_malloc0(30 + 1))) {
		sr_err("%s: o malloc failed", __func__);
		return NULL;
	}

	if (frequency >= SR_GHZ(1))
		r = snprintf(o, 30, "%llu ns", (u64_t)(frequency / 1000000000));
	else if (frequency >= SR_MHZ(1))
		r = snprintf(o, 30, "%llu us", (u64_t)(frequency / 1000000));
	else if (frequency >= SR_KHZ(1))
		r = snprintf(o, 30, "%llu ms", (u64_t)(frequency / 1000));
	else
		r = snprintf(o, 30, "%llu s", (u64_t)frequency);

	if (r < 0) {
		/* Something went wrong... */
		g_free(o);
		return NULL;
	}

	return o;
}

/**
 * Convert a numeric time(ns) value to the "natural" string representation
 * of its period.
 *
 * E.g. a value of 3000000 would be converted to "3 ms", 20000 to "20 us".
 *
 * @param time The time in ns.
 *
 * @return A g_try_malloc0()ed string representation of the time value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_time_string(uint64_t time)
{
    char *o;
    int r;

    /* Allocate enough for a uint64_t as string + " ms". */
    if (!(o = g_try_malloc0(30 + 1))) {
        sr_err("%s: o malloc failed", __func__);
        return NULL;
    }

    if (time >= SR_DAY(1))
        r = snprintf(o, 30, "%0.2lf day", time * 1.0 / SR_DAY(1));
    else if (time >= SR_HOUR(1))
        r = snprintf(o, 30, "%0.2lf hour", time * 1.0 / SR_HOUR(1));
    else if (time >= SR_MIN(1))
        r = snprintf(o, 30, "%0.2lf min", time * 1.0  / SR_MIN(1));
    else if (time >= SR_SEC(1))
        r = snprintf(o, 30, "%0.2lf s", time * 1.0  / SR_SEC(1));
    else if (time >= SR_MS(1))
        r = snprintf(o, 30, "%0.2lf ms", time * 1.0  / SR_MS(1));
    else if (time >= SR_US(1))
        r = snprintf(o, 30, "%0.2lf us", time * 1.0  / SR_US(1));
    else
        r = snprintf(o, 30, "%llu ns", (u64_t)time);

    if (r < 0) {
        /* Something went wrong... */
        g_free(o);
        return NULL;
    }

    return o;
}

/**
 * Convert a numeric voltage value to the "natural" string representation
 * of its voltage value. The voltage is specified as a rational number's
 * numerator and denominator.
 *
 * E.g. a value of 300000 would be converted to "300mV", 2 to "2V".
 *
 * @param v_p The voltage numerator.
 * @param v_q The voltage denominator.
 *
 * @return A g_try_malloc0()ed string representation of the voltage value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_voltage_string(uint64_t v_p, uint64_t v_q)
{
	int r;
	char *o;

	if (!(o = g_try_malloc0(30 + 1))) {
		sr_err("%s: o malloc failed", __func__);
		return NULL;
	}

	if (v_q == 1000)
		r = snprintf(o, 30, "%llumV", (u64_t)v_p);
	else if (v_q == 1)
		r = snprintf(o, 30, "%lluV", (u64_t)v_p);
	else
		r = snprintf(o, 30, "%gV", (float)v_p / (float)v_q);

	if (r < 0) {
		/* Something went wrong... */
		g_free(o);
		return NULL;
	}

	return o;
}

/**
 * Convert a "natural" string representation of a size value to uint64_t.
 *
 * E.g. a value of "3k" or "3 K" would be converted to 3000, a value
 * of "15M" would be converted to 15000000.
 *
 * Value representations other than decimal (such as hex or octal) are not
 * supported. Only 'k' (kilo), 'm' (mega), 'g' (giga) suffixes are supported.
 * Spaces (but not other whitespace) between value and suffix are allowed.
 *
 * @param sizestring A string containing a (decimal) size value.
 * @param size Pointer to uint64_t which will contain the string's size value.
 *
 * @return SR_OK upon success, SR_ERR upon errors.
 */
SR_API int sr_parse_sizestring(const char *sizestring, uint64_t *size)
{
	int multiplier, done;
	char *s;

	*size = strtoull(sizestring, &s, 10);
	multiplier = 0;
	done = FALSE;
	while (s && *s && multiplier == 0 && !done) {
		switch (*s) {
		case ' ':
			break;
		case 'k':
		case 'K':
			multiplier = SR_KHZ(1);
			break;
		case 'm':
		case 'M':
			multiplier = SR_MHZ(1);
			break;
		case 'g':
		case 'G':
			multiplier = SR_GHZ(1);
			break;
		default:
			done = TRUE;
			s--;
		}
		s++;
	}
	if (multiplier > 0)
		*size *= multiplier;

	if (*s && strcasecmp(s, "Hz"))
		return SR_ERR;

	return SR_OK;
}

/**
 * Convert a "natural" string representation of a time value to an
 * uint64_t value in milliseconds.
 *
 * E.g. a value of "3s" or "3 s" would be converted to 3000, a value
 * of "15ms" would be converted to 15.
 *
 * Value representations other than decimal (such as hex or octal) are not
 * supported. Only lower-case "s" and "ms" time suffixes are supported.
 * Spaces (but not other whitespace) between value and suffix are allowed.
 *
 * @param timestring A string containing a (decimal) time value.
 * @return The string's time value as uint64_t, in milliseconds.
 *
 * @todo Add support for "m" (minutes) and others.
 * @todo Add support for picoseconds?
 * @todo Allow both lower-case and upper-case? If no, document it.
 */
SR_API uint64_t sr_parse_timestring(const char *timestring)
{
	uint64_t time_msec;
	char *s;

	/* TODO: Error handling, logging. */

	time_msec = strtoull(timestring, &s, 10);
	if (time_msec == 0 && s == timestring)
		return 0;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!strcmp(s, "s"))
			time_msec *= 1000;
		else if (!strcmp(s, "ms"))
			; /* redundant */
		else
			return 0;
	}

	return time_msec;
}

SR_API gboolean sr_parse_boolstring(const char *boolstr)
{
	if (!boolstr)
		return FALSE;

	if (!g_ascii_strncasecmp(boolstr, "true", 4) ||
	    !g_ascii_strncasecmp(boolstr, "yes", 3) ||
	    !g_ascii_strncasecmp(boolstr, "on", 2) ||
	    !g_ascii_strncasecmp(boolstr, "1", 1))
		return TRUE;

	return FALSE;
}

SR_API int sr_parse_period(const char *periodstr, uint64_t *p, uint64_t *q)
{
	char *s;

	*p = strtoull(periodstr, &s, 10);
	if (*p == 0 && s == periodstr)
		/* No digits found. */
		return SR_ERR_ARG;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!strcmp(s, "fs"))
			*q = 1000000000000000ULL;
		else if (!strcmp(s, "ps"))
			*q = 1000000000000ULL;
		else if (!strcmp(s, "ns"))
			*q = 1000000000ULL;
		else if (!strcmp(s, "us"))
			*q = 1000000;
		else if (!strcmp(s, "ms"))
			*q = 1000;
		else if (!strcmp(s, "s"))
			*q = 1;
		else
			/* Must have a time suffix. */
			return SR_ERR_ARG;
	}

	return SR_OK;
}


SR_API int sr_parse_voltage(const char *voltstr, uint64_t *p, uint64_t *q)
{
	char *s;

	*p = strtoull(voltstr, &s, 10);
	if (*p == 0 && s == voltstr)
		/* No digits found. */
		return SR_ERR_ARG;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!strcasecmp(s, "mv"))
			*q = 1000L;
		else if (!strcasecmp(s, "v"))
			*q = 1;
		else
			/* Must have a base suffix. */
			return SR_ERR_ARG;
	}

	return SR_OK;
}

/** @} */
