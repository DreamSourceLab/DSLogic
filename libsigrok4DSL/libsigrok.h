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

#ifndef LIBSIGROK_SIGROK_H
#define LIBSIGROK_SIGROK_H

#include <sys/time.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <glib.h>  
#include <log/xlog.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 *
 * The public libsigrok header file to be used by frontends.
 *
 * This is the only file that libsigrok users (frontends) are supposed to
 * use and \#include. There are other header files which get installed with
 * libsigrok, but those are not meant to be used directly by frontends.
 *
 * The correct way to get/use the libsigrok API functions is:
 *
 * @code{.c}
 *   #include <libsigrok/libsigrok.h>
 * @endcode
 */

/*
 * All possible return codes of libsigrok functions must be listed here.
 * Functions should never return hardcoded numbers as status, but rather
 * use these enum values. All error codes are negative numbers.
 *
 * The error codes are globally unique in libsigrok, i.e. if one of the
 * libsigrok functions returns a "malloc error" it must be exactly the same
 * return value as used by all other functions to indicate "malloc error".
 * There must be no functions which indicate two different errors via the
 * same return code.
 *
 * Also, for compatibility reasons, no defined return codes are ever removed
 * or reused for different errors later. You can only add new entries and
 * return codes, but never remove or redefine existing ones.
 */

#define SR_LIB_NAME		"libsigrok"

/** Status/error codes returned by libsigrok functions. */
enum {
	SR_OK             =  0, /**< No error. */
	SR_ERR            =  1, /**< Generic/unspecified error. */
	SR_ERR_MALLOC     =  2, /**< Malloc/calloc/realloc error. */
	SR_ERR_ARG        =  3, /**< Function argument error. */
	SR_ERR_BUG        =  4, /**< Errors hinting at internal bugs. */
	SR_ERR_SAMPLERATE =  5, /**< Incorrect samplerate. */
	SR_ERR_NA         =  6, /**< Not applicable. */
	SR_ERR_DEVICE_CLOSED =  7, /**< Device is closed, but needs to be open. */
	SR_ERR_CALL_STATUS 	 =  8, /**< Function call status error. */
	SR_ERR_HAVE_DONE  	 =  9, /**< The Function have called.*/
	SR_ERR_FIRMWARE_NOT_EXIST 	= 10, /**< The firmware file is not exist.*/
	SR_ERR_DEVICE_IS_EXCLUSIVE 	= 11, /**< The device is exclusive by other process.*/
	SR_ERR_DEVICE_FIRMWARE_VERSION_LOW = 12, /**< The firmware version is too low.*/
	SR_ERR_DEVICE_USB_IO_ERROR = 13, /**< The usb io error.*/
	SR_ERR_DEVICE_NO_DRIVER = 14, /**< The device have no driver.*/

	/*
	 * Note: When adding entries here, don't forget to also update the
	 * sr_error_str() and sr_error_name() functions in error.c.
	 */
};

#define SR_MAX_PROBENAME_LEN 32
#define DS_MAX_ANALOG_PROBES_NUM 4
#define DS_MAX_DSO_PROBES_NUM 2

#define TriggerStages 16
#define TriggerProbes 16
#define MaxTriggerProbes 32
#define TriggerCountBits 16
#define STriggerDataStage 3
#define DS_MAX_TRIG_PERCENT 90

#define DS_CONF_DSO_HDIVS 10
#define DS_CONF_DSO_VDIVS 10

#define SAMPLES_ALIGN 1023ULL

#define STR_ID(id) #id

/* Handy little macros */
#define SR_HZ(n)  (n)
#define SR_KHZ(n) ((n) * (uint64_t)(1000ULL))
#define SR_MHZ(n) ((n) * (uint64_t)(1000000ULL))
#define SR_GHZ(n) ((n) * (uint64_t)(1000000000ULL))

#define SR_HZ_TO_NS(n) ((uint64_t)(1000000000ULL) / (n))

#define SR_NS(n)   (n)
#define SR_US(n)   ((n) * (uint64_t)(1000ULL))
#define SR_MS(n)   ((n) * (uint64_t)(1000000ULL))
#define SR_SEC(n)  ((n) * (uint64_t)(1000000000ULL))
#define SR_MIN(n)  ((n) * (uint64_t)(60000000000ULL))
#define SR_HOUR(n) ((n) * (uint64_t)(3600000000000ULL))
#define SR_DAY(n)  ((n) * (uint64_t)(86400000000000ULL))

#define SR_n(n)  (n)
#define SR_Kn(n) ((n) * (uint64_t)(1000ULL))
#define SR_Mn(n) ((n) * (uint64_t)(1000000ULL))
#define SR_Gn(n) ((n) * (uint64_t)(1000000000ULL))

#define SR_B(n)  (n)
#define SR_KB(n) ((n) * (uint64_t)(1024ULL))
#define SR_MB(n) ((n) * (uint64_t)(1048576ULL))
#define SR_GB(n) ((n) * (uint64_t)(1073741824ULL))

#define SR_mV(n) (n)
#define SR_V(n)  ((n) * (uint64_t)(1000ULL))
#define SR_KV(n) ((n) * (uint64_t)(1000000ULL))
#define SR_MV(n) ((n) * (uint64_t)(1000000000ULL))
 
/*
 * Use SR_API to mark public API symbols, and SR_PRIV for private symbols.
 *
 * Variables and functions marked 'static' are private already and don't
 * need SR_PRIV. However, functions which are not static (because they need
 * to be used in other libsigrok-internal files) but are also not meant to
 * be part of the public libsigrok API, must use SR_PRIV.
 *
 * This uses the 'visibility' feature of gcc (requires gcc >= 4.0).
 *
 * This feature is not available on MinGW/Windows, as it is a feature of
 * ELF files and MinGW/Windows uses PE files.
 *
 * Details: http://gcc.gnu.org/wiki/Visibility
 */

/* Marks public libsigrok API symbols. */
#ifndef _WIN32
#define SR_API __attribute__((visibility("default")))
#else
#define SR_API
#endif

/* Marks private, non-public libsigrok symbols (not part of the API). */
#ifndef _WIN32
#define SR_PRIV __attribute__((visibility("hidden")))
#else
#define SR_PRIV
#endif

enum sr_device_type{
	DEV_TYPE_UNKOWN = 0,
	DEV_TYPE_DEMO = 1,
	DEV_TYPE_FILELOG = 2,
	DEV_TYPE_USB = 3,
	DEV_TYPE_SERIAL = 4,
};
  
/** Data types used by sr_config_info(). */
enum {
	SR_T_UINT64 = 10000,
    SR_T_UINT8,
    SR_T_CHAR,
	SR_T_BOOL,
	SR_T_FLOAT,
	SR_T_RATIONAL_PERIOD,
	SR_T_RATIONAL_VOLT,
	SR_T_KEYVALUE,
	SR_T_LIST,
	SR_T_INT16
};

/** Value for sr_datafeed_packet.type. */
enum sr_datafeed_packet_type {
	SR_DF_HEADER = 10000,
	SR_DF_END,
	SR_DF_META,
	SR_DF_TRIGGER,
	SR_DF_LOGIC,
    SR_DF_DSO,
	SR_DF_ANALOG,
	SR_DF_FRAME_BEGIN,
	SR_DF_FRAME_END,
    SR_DF_OVERFLOW,
};

/** Values for sr_datafeed_analog.mq. */
enum {
	SR_MQ_VOLTAGE = 10000,
	SR_MQ_CURRENT,
	SR_MQ_RESISTANCE,
	SR_MQ_CAPACITANCE,
	SR_MQ_TEMPERATURE,
	SR_MQ_FREQUENCY,
	SR_MQ_DUTY_CYCLE,
	SR_MQ_CONTINUITY,
	SR_MQ_PULSE_WIDTH,
	SR_MQ_CONDUCTANCE,
	/** Electrical power, usually in W, or dBm. */
	SR_MQ_POWER,
	/** Gain (a transistor's gain, or hFE, for example). */
	SR_MQ_GAIN,
	/** Logarithmic representation of sound pressure relative to a
	 * reference value. */
	SR_MQ_SOUND_PRESSURE_LEVEL,
	SR_MQ_CARBON_MONOXIDE,
	SR_MQ_RELATIVE_HUMIDITY,
};

/** Values for sr_datafeed_analog.unit. */
enum {
	SR_UNIT_VOLT = 10000,
	SR_UNIT_AMPERE,
	SR_UNIT_OHM,
	SR_UNIT_FARAD,
	SR_UNIT_KELVIN,
	SR_UNIT_CELSIUS,
	SR_UNIT_FAHRENHEIT,
	SR_UNIT_HERTZ,
	SR_UNIT_PERCENTAGE,
	SR_UNIT_BOOLEAN,
	SR_UNIT_SECOND,
	/** Unit of conductance, the inverse of resistance. */
	SR_UNIT_SIEMENS,
	/**
	 * An absolute measurement of power, in decibels, referenced to
	 * 1 milliwatt (dBu).
	 */
	SR_UNIT_DECIBEL_MW,
	/** Voltage in decibel, referenced to 1 volt (dBV). */
	SR_UNIT_DECIBEL_VOLT,
	/**
	 * Measurements that intrinsically do not have units attached, such
	 * as ratios, gains, etc. Specifically, a transistor's gain (hFE) is
	 * a unitless quantity, for example.
	 */
	SR_UNIT_UNITLESS,
	/** Sound pressure level relative so 20 micropascals. */
	SR_UNIT_DECIBEL_SPL,
	/**
	 * Normalized (0 to 1) concentration of a substance or compound with 0
	 * representing a concentration of 0%, and 1 being 100%. This is
	 * represented as the fraction of number of particles of the substance.
	 */
	SR_UNIT_CONCENTRATION,
};

/** Values for sr_datafeed_analog.flags. */
enum {
	/** Voltage measurement is alternating current (AC). */
	SR_MQFLAG_AC = 0x01,
	/** Voltage measurement is direct current (DC). */
	SR_MQFLAG_DC = 0x02,
	/** This is a true RMS measurement. */
	SR_MQFLAG_RMS = 0x04,
	/** Value is voltage drop across a diode, or NAN. */
	SR_MQFLAG_DIODE = 0x08,
	/** Device is in "hold" mode (repeating the last measurement). */
	SR_MQFLAG_HOLD = 0x10,
	/** Device is in "max" mode, only updating upon a new max value. */
	SR_MQFLAG_MAX = 0x20,
	/** Device is in "min" mode, only updating upon a new min value. */
	SR_MQFLAG_MIN = 0x40,
	/** Device is in autoranging mode. */
	SR_MQFLAG_AUTORANGE = 0x80,
	/** Device is in relative mode. */
	SR_MQFLAG_RELATIVE = 0x100,
	/** Sound pressure level is A-weighted in the frequency domain,
	 * according to IEC 61672:2003. */
	SR_MQFLAG_SPL_FREQ_WEIGHT_A = 0x200,
	/** Sound pressure level is C-weighted in the frequency domain,
	 * according to IEC 61672:2003. */
	SR_MQFLAG_SPL_FREQ_WEIGHT_C = 0x400,
	/** Sound pressure level is Z-weighted (i.e. not at all) in the
	 * frequency domain, according to IEC 61672:2003. */
	SR_MQFLAG_SPL_FREQ_WEIGHT_Z = 0x800,
	/** Sound pressure level is not weighted in the frequency domain,
	 * albeit without standards-defined low and high frequency limits. */
	SR_MQFLAG_SPL_FREQ_WEIGHT_FLAT = 0x1000,
	/** Sound pressure level measurement is S-weighted (1s) in the
	 * time domain. */
	SR_MQFLAG_SPL_TIME_WEIGHT_S = 0x2000,
	/** Sound pressure level measurement is F-weighted (125ms) in the
	 * time domain. */
	SR_MQFLAG_SPL_TIME_WEIGHT_F = 0x4000,
	/** Sound pressure level is time-averaged (LAT), also known as
	 * Equivalent Continuous A-weighted Sound Level (LEQ). */
	SR_MQFLAG_SPL_LAT = 0x8000,
	/** Sound pressure level represented as a percentage of measurements
	 * that were over a preset alarm level. */
	SR_MQFLAG_SPL_PCT_OVER_ALARM = 0x10000,
};

enum DSLOGIC_OPERATION_MODE
{
    /** Buffer mode */
    LO_OP_BUFFER = 0,
    /** Stream mode */
    LO_OP_STREAM = 1,
    /** Internal pattern test mode */
    LO_OP_INTEST = 2,
    /** External pattern test mode */
    LO_OP_EXTEST = 3,
    /** SDRAM loopback test mode */
    LO_OP_LPTEST = 4,
};

enum DSO_MEASURE_TYPE {
    DSO_MS_BEGIN = 0,
    DSO_MS_FREQ,
    DSO_MS_PERD,
    DSO_MS_PDUT,
    DSO_MS_NDUT,
    DSO_MS_PCNT,
    DSO_MS_RISE,
    DSO_MS_FALL,
    DSO_MS_PWDT,
    DSO_MS_NWDT,
    DSO_MS_BRST,
    DSO_MS_AMPT,
    DSO_MS_VHIG,
    DSO_MS_VLOW,
    DSO_MS_VRMS,
    DSO_MS_VMEA,
    DSO_MS_VP2P,
    DSO_MS_VMAX,
    DSO_MS_VMIN,
    DSO_MS_POVR,
    DSO_MS_NOVR,
    DSO_MS_END,
};

enum {
    SR_PKT_OK,
    SR_PKT_SOURCE_ERROR,
    SR_PKT_DATA_ERROR,
};

struct sr_context; //hidden all field
struct sr_dev_inst;
struct sr_dev_driver;

struct sr_datafeed_packet {
	uint16_t type; //see enum sr_datafeed_packet_type
    uint16_t status;
	const void *payload;
	int bExportOriginalData;
};

struct sr_datafeed_header {
	int feed_version;
	struct timeval starttime;
};

struct sr_datafeed_meta {
	GSList *config;
};

enum LA_DATA_FORMAT {
    LA_CROSS_DATA,
    LA_SPLIT_DATA,
};

struct sr_datafeed_logic {
	uint64_t length;
    /** data format */
    int format;
    /** for LA_SPLIT_DATA, indicate the channel index */
    uint16_t index;
    uint16_t order;
	uint16_t unitsize;
    uint16_t data_error;
    uint64_t error_pattern;
	void *data;
};

struct sr_datafeed_dso {
    /** The probes for which data is included in this packet. */
    GSList *probes;
    int num_samples;
    /** Measured quantity (voltage, current, temperature, and so on). */
    int mq;
    /** Unit in which the MQ is measured. */
    int unit;
    /** Bitmap with extra information about the MQ. */
    uint64_t mqflags;
    /** samplerate different from last packet */
    gboolean samplerate_tog;
    /** trig flag */
    gboolean trig_flag;
    /** trig channel */
    uint8_t trig_ch;
    /** The analog value(s). The data is interleaved according to
     * the probes list. */
    void *data;
};

struct sr_datafeed_analog {
	/** The probes for which data is included in this packet. */
	GSList *probes;
	int num_samples;
    /** How many bits for each sample */
    uint8_t unit_bits;
    /** Interval between two valid samples */
    uint16_t unit_pitch;
	/** Measured quantity (voltage, current, temperature, and so on). */
	int mq;
	/** Unit in which the MQ is measured. */
	int unit;
	/** Bitmap with extra information about the MQ. */
	uint64_t mqflags;
	/** The analog value(s). The data is interleaved according to
	 * the probes list. */
    void *data;
};

/** Input (file) format struct. */
struct sr_input {
	/**
	 * A pointer to this input format's 'struct sr_input_format'.
	 * The frontend can use this to call the module's callbacks.
	 */
	struct sr_input_format *format;

	GHashTable *param;

	struct sr_dev_inst *sdi;

	void *internal;
};

struct sr_input_format {
	/** The unique ID for this input format. Must not be NULL. */
	char *id;

	/**
	 * A short description of the input format, which can (for example)
	 * be displayed to the user by frontends. Must not be NULL.
	 */
	char *description;

	/**
	 * Check if this input module can load and parse the specified file.
	 *
	 * @param filename The name (and path) of the file to check.
	 *
	 * @return TRUE if this module knows the format, FALSE if it doesn't.
	 */
	int (*format_match) (const char *filename);

	/**
	 * Initialize the input module.
	 *
	 * @param in A pointer to a valid 'struct sr_input' that the caller
	 *           has to allocate and provide to this function. It is also
	 *           the responsibility of the caller to free it later.
	 * @param filename The name (and path) of the file to use.
	 *
	 * @return SR_OK upon success, a negative error code upon failure.
	 */
	int (*init) (struct sr_input *in, const char *filename);

	/**
	 * Load a file, parsing the input according to the file's format.
     *
	 * This function will send datafeed packets to the session bus, so
	 * the calling frontend must have registered its session callbacks
	 * beforehand.
	 *
	 * The packet types sent across the session bus by this function must
	 * include at least SR_DF_HEADER, SR_DF_END, and an appropriate data
	 * type such as SR_DF_LOGIC. It may also send a SR_DF_TRIGGER packet
	 * if appropriate.
	 *
	 * @param in A pointer to a valid 'struct sr_input' that the caller
	 *           has to allocate and provide to this function. It is also
	 *           the responsibility of the caller to free it later.
	 * @param filename The name (and path) of the file to use.
	 *
     * @return SR_OK upon succcess, a negative error code upon failure.
	 */
	int (*loadfile) (struct sr_input *in, const char *filename);
};

/** Output (file) format struct. */
struct sr_output {
	/**
	 * A pointer to this output format's 'struct sr_output_format'.
	 * The frontend can use this to call the module's callbacks.
	 */
    const struct sr_output_module *module;

	/**
	 * The device for which this output module is creating output. This
	 * can be used by the module to find out probe names and numbers.
	 */
    const struct sr_dev_inst *sdi;

	/**
	 * An optional parameter which the frontend can pass in to the
	 * output module. How the string is interpreted is entirely up to
	 * the module.
	 */
	char *param;

	/**
	 * A generic pointer which can be used by the module to keep internal
	 * state between calls into its callback functions.
	 *
	 * For example, the module might store a pointer to a chunk of output
	 * there, and only flush it when it reaches a certain size.
	 */
	void *priv;

	uint64_t start_sample_index;

	char time_string[30];
};

/** Generic option struct used by various subsystems. */
struct sr_option {
	/* Short name suitable for commandline usage, [a-z0-9-]. */
	char *id;
	/* Short name suitable for GUI usage, can contain UTF-8. */
	char *name;
	/* Description of the option, in a sentence. */
	char *desc;
	/* Default value for this option. */
	GVariant *def;
	/* List of possible values, if this is an option with few values. */
	GSList *values;
};

/** Output module driver. */
struct sr_output_module {
	/**
	 * A unique ID for this output module, suitable for use in command-line
	 * clients, [a-z0-9-]. Must not be NULL.
	 */
	char *id;

	/**
	 * A unique name for this output module, suitable for use in GUI
	 * clients, can contain UTF-8. Must not be NULL.
	 */
	const char *name;

	/**
	 * A short description of the output module. Must not be NULL.
	 *
	 * This can be displayed by frontends, e.g. when selecting the output
	 * module for saving a file.
	 */
	char *desc;

	/**
	 * A NULL terminated array of strings containing a list of file name
	 * extensions typical for the input file format, or NULL if there is
	 * no typical extension for this file format.
	 */
	const char *const *exts;

	/**
	 * Returns a NULL-terminated list of options this module can take.
	 * Can be NULL, if the module has no options.
	 */
	const struct sr_option *(*options) (void);

	/**
	 * This function is called once, at the beginning of an output stream.
	 *
	 * The device struct will be available in the output struct passed in,
	 * as well as the param field -- which may be NULL or an empty string,
	 * if no parameter was passed.
	 *
	 * The module can use this to initialize itself, create a struct for
	 * keeping state and storing it in the <code>internal</code> field.
	 *
	 * @param o Pointer to the respective 'struct sr_output'.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*init) (struct sr_output *o, GHashTable *options);

	/**
	 * This function is passed a copy of every packed in the data feed.
	 * Any output generated by the output module in response to the
	 * packet should be returned in a newly allocated GString
	 * <code>out</code>, which will be freed by the caller.
	 *
	 * Packets not of interest to the output module can just be ignored,
	 * and the <code>out</code> parameter set to NULL.
	 *
	 * @param o Pointer to the respective 'struct sr_output'.
	 * @param sdi The device instance that generated the packet.
	 * @param packet The complete packet.
	 * @param out A pointer where a GString * should be stored if
	 * the module generates output, or NULL if not.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*receive) (const struct sr_output *o,
			const struct sr_datafeed_packet *packet, GString **out);

	/**
	 * This function is called after the caller is finished using
	 * the output module, and can be used to free any internal
	 * resources the module may keep.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*cleanup) (struct sr_output *o);
};


enum CHANNEL_TYPE {
    SR_CHANNEL_DECODER = 9998,
    SR_CHANNEL_GROUP = 9999,
    SR_CHANNEL_LOGIC = 10000,
    SR_CHANNEL_DSO,
    SR_CHANNEL_ANALOG,
    SR_CHANNEL_FFT,
    SR_CHANNEL_LISSAJOUS,
    SR_CHANNEL_MATH,
};

enum OPERATION_MODE {	
    LOGIC = 0,
    DSO = 1,
    ANALOG = 2,
	UNKNOWN_DSL_MODE = 99,
};

struct sr_channel {
    /* The index field will go: use g_slist_length(sdi->channels) instead. */
    uint16_t index;
    int type;
	gboolean enabled;
	char *name;
	char *trigger;
    uint8_t bits;
    uint64_t vdiv;
    uint64_t vfactor;
    uint16_t offset;
    uint16_t zero_offset;
    uint16_t hw_offset;
    uint16_t vpos_trans;
    uint8_t coupling;
    uint8_t trig_value;
    int8_t comb_diff_top;
    int8_t comb_diff_bom;
    int8_t comb_comp;
    uint16_t digi_fgain;

    double cali_fgain0;
    double cali_fgain1;
    double cali_fgain2;
    double cali_fgain3;
    double cali_comb_fgain0;
    double cali_comb_fgain1;
    double cali_comb_fgain2;
    double cali_comb_fgain3;

    gboolean map_default;
    const char *map_unit;
    double map_min;
    double map_max;
    struct DSL_vga *vga_ptr;
};

/** Structure for groups of channels that have common properties. */
struct sr_channel_group {
    /** Name of the channel group. */
    const char *name;
    /** List of sr_channel structs of the channels belonging to this group. */
    GSList *channels;
    /** Private data for driver use. */
    void *priv;
};

struct sr_config {
	int key;
	GVariant *data;
};

struct sr_config_info {
	int  key;
	int  datatype;
	const char *name;
};

struct sr_status {
    uint8_t trig_hit;
    uint8_t captured_cnt3;
    uint8_t captured_cnt2;
    uint8_t captured_cnt1;
    uint8_t captured_cnt0;

    uint16_t pkt_id;
    uint32_t vlen;
    gboolean stream_mode;
    gboolean measure_valid;
    uint32_t sample_divider;
    gboolean sample_divider_tog;
    gboolean trig_flag;
    uint8_t  trig_ch;
    uint8_t  trig_offset;

    uint8_t ch0_max;
    uint8_t ch0_min;
    uint32_t ch0_cyc_cnt;
    uint32_t ch0_cyc_tlen;
    uint32_t ch0_cyc_plen;
    uint32_t ch0_cyc_llen;
    gboolean ch0_level_valid;
    gboolean ch0_plevel;
    uint8_t ch0_low_level;
    uint8_t ch0_high_level;
    uint32_t ch0_cyc_rlen;
    uint32_t ch0_cyc_flen;
    uint64_t ch0_acc_square;
    uint32_t ch0_acc_mean;
    uint32_t ch0_acc_mean_p1;
    uint32_t ch0_acc_mean_p2;
    uint32_t ch0_acc_mean_p3;

    uint8_t ch1_max;
    uint8_t ch1_min;
    uint32_t ch1_cyc_cnt;
    uint32_t ch1_cyc_tlen;
    uint32_t ch1_cyc_plen;
    uint32_t ch1_cyc_llen;
    gboolean ch1_level_valid;
    gboolean ch1_plevel;
    uint8_t ch1_low_level;
    uint8_t ch1_high_level;
    uint32_t ch1_cyc_rlen;
    uint32_t ch1_cyc_flen;
    uint64_t ch1_acc_square;
    uint32_t ch1_acc_mean;
    uint32_t ch1_acc_mean_p1;
    uint32_t ch1_acc_mean_p2;
    uint32_t ch1_acc_mean_p3;
};

struct sr_list_item{
	int   id;
	const char *name;
};

enum sr_config_option_id
{
	/*--- Device classes ------------------------------------------------*/

	/** The device can act as logic analyzer. */
	SR_CONF_LOGIC_ANALYZER = 10000,

	/** The device can act as an oscilloscope. */
	SR_CONF_OSCILLOSCOPE = 10001,

	/** The device can act as a multimeter. */
	SR_CONF_MULTIMETER	= 10002,

	/** The device is a demo device. */
	SR_CONF_DEMO_DEV = 10003,

	/** The device can act as a sound level meter. */
	SR_CONF_SOUNDLEVELMETER = 10004,

	/** The device can measure temperature. */
	SR_CONF_THERMOMETER = 10005,

	/** The device can measure humidity. */
	SR_CONF_HYGROMETER = 10006,

	/*--- Driver scan options -------------------------------------------*/

	/**
	 * Specification on how to connect to a device.
	 *
	 * In combination with SR_CONF_SERIALCOMM, this is a serial port in
	 * the form which makes sense to the OS (e.g., /dev/ttyS0).
	 * Otherwise this specifies a USB device, either in the form of
	 * @verbatim <bus>.<address> @endverbatim (decimal, e.g. 1.65) or
	 * @verbatim <vendorid>.<productid> @endverbatim
	 * (hexadecimal, e.g. 1d6b.0001).
	 */
	SR_CONF_CONN = 20000,

	/**
	 * Serial communication specification, in the form:
	 *
	 *   @verbatim <baudrate>/<databits><parity><stopbits> @endverbatim
	 *
	 * Example: 9600/8n1
	 *
	 * The string may also be followed by one or more special settings,
	 * in the form "/key=value". Supported keys and their values are:
	 *
	 * rts    0,1    set the port's RTS pin to low or high
	 * dtr    0,1    set the port's DTR pin to low or high
	 * flow   0      no flow control
	 *        1      hardware-based (RTS/CTS) flow control
	 *        2      software-based (XON/XOFF) flow control
	 * 
	 * This is always an optional parameter, since a driver typically
	 * knows the speed at which the device wants to communicate.
	 */
	SR_CONF_SERIALCOMM = 20001,

	/*--- Device configuration ------------------------------------------*/

	/** The device supports setting its samplerate, in Hz. */
	SR_CONF_SAMPLERATE = 30000,

	/** The device supports setting a pre/post-trigger capture ratio. */
	SR_CONF_CAPTURE_RATIO = 30001,

    /** */
    SR_CONF_USB_SPEED = 30002,
    SR_CONF_USB30_SUPPORT = 30003,
    SR_CONF_DEVICE_MODE = 30004,
    SR_CONF_INSTANT = 30005,
    SR_CONF_STATUS = 30006,

	/** The device supports setting a pattern (pattern generator mode). */
	SR_CONF_PATTERN_MODE = 30007,

	/** The device supports Run Length Encoding. */
	SR_CONF_RLE = 30008,

    /** Need wait to uplad captured data */
    SR_CONF_WAIT_UPLOAD = 30009,

	/** The device supports setting trigger slope. */
	SR_CONF_TRIGGER_SLOPE = 30010,

	/** Trigger source. */
	SR_CONF_TRIGGER_SOURCE = 30011,

    /** Trigger channel */
    SR_CONF_TRIGGER_CHANNEL = 30012,

    /** Trigger Value. */
    SR_CONF_TRIGGER_VALUE = 30013,

	/** Horizontal trigger position. */
	SR_CONF_HORIZ_TRIGGERPOS = 30014,

    /** Trigger hold off time */
    SR_CONF_TRIGGER_HOLDOFF = 30015,

    /** Trigger Margin */
    SR_CONF_TRIGGER_MARGIN = 30016,

	/** Buffer size. */
	SR_CONF_BUFFERSIZE = 30017,

	/** Time base. */
    SR_CONF_MAX_TIMEBASE = 30018,
    SR_CONF_MIN_TIMEBASE = 30019,
	SR_CONF_TIMEBASE = 30020,

	/** Filter. */
	SR_CONF_FILTER = 30021,

    /** DSO configure sync */
    SR_CONF_DSO_SYNC = 30022,

    /** How many bits for each sample */
    SR_CONF_UNIT_BITS = 30023,
    SR_CONF_REF_MIN = 30024,
    SR_CONF_REF_MAX = 30025,

    /** Valid channel number */
    SR_CONF_TOTAL_CH_NUM = 30026,

    /** Valid channel number */
    SR_CONF_VLD_CH_NUM = 30027,

    /** 32 channel support */
    SR_CONF_LA_CH32 = 30028,

    /** Zero */
    SR_CONF_HAVE_ZERO = 30029,
    SR_CONF_ZERO = 30030,
    SR_CONF_ZERO_SET = 30031,
    SR_CONF_ZERO_LOAD = 30032,
    SR_CONF_ZERO_DEFAULT = 30033,
    SR_CONF_ZERO_COMB_FGAIN = 30034,
    SR_CONF_ZERO_COMB = 30035,
    SR_CONF_VOCM = 30036,
    SR_CONF_CALI = 30037,

    /** status for dso channel */
    SR_CONF_STATUS_PERIOD = 30038,
    SR_CONF_STATUS_PCNT = 30039,
    SR_CONF_STATUS_MAX = 30040,
    SR_CONF_STATUS_MIN = 30041,
    SR_CONF_STATUS_PLEN = 30042,
    SR_CONF_STATUS_LLEN = 30043,
    SR_CONF_STATUS_LEVEL = 30044,
    SR_CONF_STATUS_PLEVEL = 30045,
    SR_CONF_STATUS_LOW = 30046,
    SR_CONF_STATUS_HIGH = 30047,
    SR_CONF_STATUS_RLEN = 30048,
    SR_CONF_STATUS_FLEN = 30049,
    SR_CONF_STATUS_RMS = 30050,
    SR_CONF_STATUS_MEAN = 30051,

    /** Stream */
    SR_CONF_STREAM = 30052,

    /** DSO Roll */
    SR_CONF_ROLL = 30053,

    /** Test */
    SR_CONF_TEST = 30054,
    SR_CONF_EEPROM = 30055,
    SR_CONF_TUNE = 30056,
    SR_CONF_TUNE_SEL = 30057,
    SR_CONF_EXTEND_ID = 30058,
    SR_CONF_EXTEND_DATA = 30059,

	/** The device supports setting its sample interval, in ms. */
	SR_CONF_SAMPLE_INTERVAL = 30060,

	/** Number of timebases, as related to SR_CONF_TIMEBASE.  */
	SR_CONF_NUM_TIMEBASE = 30061,

    /** Number of vertical divisions, as related to SR_CONF_PROBE_VDIV.  */
	SR_CONF_NUM_VDIV = 30062,

    /** clock type (internal/external) */
    SR_CONF_CLOCK_TYPE = 30063,

    /** clock edge (posedge/negedge) */
    SR_CONF_CLOCK_EDGE = 30064,

    /** Device operation mode */
    SR_CONF_OPERATION_MODE = 30065,

    /** Device buffer options */
    SR_CONF_BUFFER_OPTIONS = 30066,

    /** Device channel mode */
    SR_CONF_CHANNEL_MODE = 30067,

    /** RLE compress support */
    SR_CONF_RLE_SUPPORT = 30068,

    /** Signal max height **/
    SR_CONF_MAX_HEIGHT = 30069,
    SR_CONF_MAX_HEIGHT_VALUE = 30070,

    /** Device sample threshold */
    SR_CONF_THRESHOLD = 30071,
    SR_CONF_VTH = 30072,

    /** Hardware capacity **/
    SR_CONF_MAX_DSO_SAMPLERATE = 30073,
    SR_CONF_MAX_DSO_SAMPLELIMITS = 30074,
    SR_CONF_HW_DEPTH = 30075,

    /** bandwidth */
    SR_CONF_BANDWIDTH = 30076,
    SR_CONF_BANDWIDTH_LIMIT = 30077,

    /*--- Probe configuration -------------------------------------------*/
    /** Probe options */
    SR_CONF_PROBE_CONFIGS = 30078,

    /** Enable */
    SR_CONF_PROBE_EN = 30080,

    /** Coupling */
    SR_CONF_PROBE_COUPLING = 30081,

    /** Volts/div */
    SR_CONF_PROBE_VDIV = 30082,

    /** Factor */
    SR_CONF_PROBE_FACTOR = 30083,

    /** Mapping */
    SR_CONF_PROBE_MAP_DEFAULT = 30084,
    SR_CONF_PROBE_MAP_UNIT = 30085,
    SR_CONF_PROBE_MAP_MIN = 30086,
    SR_CONF_PROBE_MAP_MAX = 30087,

    /** Vertical offset */
    SR_CONF_PROBE_OFFSET = 30088,
    SR_CONF_PROBE_HW_OFFSET = 30089,
    SR_CONF_PROBE_PREOFF = 30090,
    SR_CONF_PROBE_PREOFF_DEFAULT = 30091,
    SR_CONF_PROBE_PREOFF_MARGIN = 30092,

    /** VGain */
    SR_CONF_PROBE_VGAIN = 30093,
    SR_CONF_PROBE_VGAIN_DEFAULT = 30094,
    SR_CONF_PROBE_VGAIN_RANGE = 30095,
    SR_CONF_PROBE_COMB_COMP_EN = 30096,
    SR_CONF_PROBE_COMB_COMP = 30097,

	/*--- Special stuff -------------------------------------------------*/

	/** Device options for a particular device. */
	SR_CONF_DEVICE_OPTIONS = 30098,

    /** Sessions */
    SR_CONF_DEVICE_SESSIONS = 30099,

    /** Session file version */
    SR_CONF_FILE_VERSION = 30102,

	/** The device supports setting the number of probes. */
	SR_CONF_CAPTURE_NUM_PROBES = 30103,

    /** The device supports setting the number of data blocks. */
    SR_CONF_NUM_BLOCKS = 30104,

    SR_CONF_LOAD_DECODER = 30105,

    SR_CONF_DEMO_INIT = 30106,

    SR_CONF_DEMO_CHANGE = 30107,

	/*--- Acquisition modes ---------------------------------------------*/

	/**
	 * The device supports setting a sample time limit (how long
	 * the sample acquisition should run, in ms).
	 */
	SR_CONF_LIMIT_MSEC = 50000,

	/**
	 * The device supports setting a sample number limit (how many
	 * samples should be acquired).
	 */
	SR_CONF_LIMIT_SAMPLES = 50001,

    /**
     * Absolute time record for session driver
     */
    SR_CONF_TRIGGER_TIME = 50002,

    /**
     * Trigger position for session driver
     */
    SR_CONF_TRIGGER_POS = 50003,

    /**
     * The actual sample count received
     */
    SR_CONF_ACTUAL_SAMPLES = 50004,

	/**
	 * The device supports setting a frame limit (how many
	 * frames should be acquired).
	 */
	SR_CONF_LIMIT_FRAMES = 50005,

	/**
	 * The device supports continuous sampling. Neither a time limit
	 * nor a sample number limit has to be supplied, it will just acquire
	 * samples continuously, until explicitly stopped by a certain command.
	 */
	SR_CONF_CONTINUOUS = 50006,

	/** The device has internal storage, into which data is logged. This
	 * starts or stops the internal logging. */
	SR_CONF_DATALOG = 50007,

	/** Manually entered sample rate as supplement for external clock mode */
	SR_CONF_EXT_SAMPLERATE = 50008,

	SR_CONF_LOOP_MODE = 60001,
};

/** Device instance status. */
enum sr_device_status {
	/** The device instance was not found. */
	SR_ST_NOT_FOUND = 10000,
	/** The device instance was found, but is still booting. */
	SR_ST_INITIALIZING,
	/** The device instance is live, but not in use. */
	SR_ST_INACTIVE,
    /** The device instance has an imcompatible firmware */
    SR_ST_INCOMPATIBLE,
	/** The device instance is actively in use in a session. */
	SR_ST_ACTIVE,
	/** The device is winding down its session. */
	SR_ST_STOPPING,
};

/** Device test modes. */
enum {
    /** No test mode */
    SR_TEST_NONE,
    /** Internal pattern test mode */
    SR_TEST_INTERNAL,
    /** External pattern test mode */
    SR_TEST_EXTERNAL,
    /** SDRAM loopback test mode */
    SR_TEST_LOOPBACK,
};

/** Device threshold level. */
enum DSL_THRESHOLD_LEVEL{
    /** 1.8/2.5/3.3 level */
    SR_TH_3V3 = 0,
    /** 5.0 level */
    SR_TH_5V0 = 1,
};

/** Device input filter. */
enum {
    /** None */
    SR_FILTER_NONE = 0,
    /** One clock cycle */
    SR_FILTER_1T = 1,
};

/** Coupling. */
enum {
    /** DC */
    SR_DC_COUPLING = 0,
    /** AC */
    SR_AC_COUPLING = 1,
    /** Ground */
    SR_GND_COUPLING = 2,
};

struct sr_dev_mode {
    int mode;
	const char *name;
    const char *acronym;
};
 
enum {
    SIMPLE_TRIGGER = 0,
    ADV_TRIGGER = 1,
    SERIAL_TRIGGER = 2,
};

enum {
    DSO_TRIGGER_AUTO = 0,
    DSO_TRIGGER_CH0 = 1,
    DSO_TRIGGER_CH1 = 2,
    DSO_TRIGGER_CH0A1 = 3,
    DSO_TRIGGER_CH0O1 = 4,
};
enum {
    DSO_TRIGGER_RISING = 0,
    DSO_TRIGGER_FALLING = 1,
};

struct ds_trigger_pos {
    uint32_t check_id;
    uint32_t real_pos;
    uint32_t ram_saddr;
    uint32_t remain_cnt_l;
    uint32_t remain_cnt_h;
    uint32_t status;
};
  
enum DSL_CHANNEL_ID 
{
    DSL_STREAM20x16 = 0,
    DSL_STREAM25x12 = 1,
    DSL_STREAM50x6 = 2,
    DSL_STREAM100x3 = 3,

    DSL_STREAM20x16_3DN2 = 4,
    DSL_STREAM25x12_3DN2 = 5,
    DSL_STREAM50x6_3DN2 = 6,
    DSL_STREAM100x3_3DN2 = 7,

    DSL_STREAM10x32_32_3DN2 = 8,
    DSL_STREAM20x16_32_3DN2 = 9,
    DSL_STREAM25x12_32_3DN2 = 10,
    DSL_STREAM50x6_32_3DN2 = 11,
    DSL_STREAM100x3_32_3DN2 = 12,

    DSL_STREAM50x32 = 13,
    DSL_STREAM100x30 = 14,
    DSL_STREAM250x12 = 15,
    DSL_STREAM125x16_16 = 16,
    DSL_STREAM250x12_16 = 17,
    DSL_STREAM500x6 = 18,
    DSL_STREAM1000x3 = 19,

    DSL_BUFFER100x16 = 20,
    DSL_BUFFER200x8 = 21,
    DSL_BUFFER400x4 = 22,

    DSL_BUFFER250x32 = 23,
    DSL_BUFFER500x16 = 24,
    DSL_BUFFER1000x8 = 25,

    DSL_ANALOG10x2 = 26,
    DSL_ANALOG10x2_500 = 27,

    DSL_DSO200x2 = 28,
    DSL_DSO1000x2 = 29,
};

/*--- input/input.c ---------------------------------------------------------*/

SR_API struct sr_input_format **sr_input_list(void);

/*--- output/output.c -------------------------------------------------------*/

SR_API const struct sr_output_module **sr_output_list(void);

/*--- strutil.c -------------------------------------------------------------*/

SR_API char *sr_si_string_u64(uint64_t x, const char *unit);
SR_API char *sr_iec_string_u64(uint64_t x, const char *unit);
SR_API char *sr_samplerate_string(uint64_t samplerate);
SR_API char *sr_samplecount_string(uint64_t samplecount);
SR_API char *sr_period_string(uint64_t frequency);
SR_API char *sr_time_string(uint64_t time);
SR_API char *sr_voltage_string(uint64_t v_p, uint64_t v_q);
SR_API int sr_parse_sizestring(const char *sizestring, uint64_t *size);
SR_API uint64_t sr_parse_timestring(const char *timestring);
SR_API gboolean sr_parse_boolstring(const char *boolstring);
SR_API int sr_parse_period(const char *periodstr, uint64_t *p, uint64_t *q);
SR_API int sr_parse_voltage(const char *voltstr, uint64_t *p, uint64_t *q);

/*--- version.c -------------------------------------------------------------*/

SR_API const char *sr_get_lib_version_string();

/*--- error.c ---------------------------------------------------------------*/

SR_API const char *sr_error_str(int error_code);
SR_API const char *sr_error_name(int error_code);

/*--- trigger.c ------------------------------------------------------------*/
SR_API int ds_trigger_reset();
SR_API int ds_trigger_stage_set_value(uint16_t stage, uint16_t probes, char *trigger0, char *trigger1);
SR_API int ds_trigger_stage_set_logic(uint16_t stage, uint16_t probes, unsigned char trigger_logic);
SR_API int ds_trigger_stage_set_inv(uint16_t stage, uint16_t probes, unsigned char trigger0_inv, unsigned char trigger1_inv);
SR_API int ds_trigger_stage_set_count(uint16_t stage, uint16_t probes, uint32_t trigger0_count, uint32_t trigger1_count);
SR_API int ds_trigger_probe_set(uint16_t probe, unsigned char trigger0, unsigned char trigger1);
SR_API int ds_trigger_set_stage(uint16_t stages);
SR_API int ds_trigger_set_pos(uint16_t position);
SR_API uint16_t ds_trigger_get_pos();
SR_API int ds_trigger_set_en(uint16_t enable);
SR_API uint16_t ds_trigger_get_en();
SR_API int ds_trigger_set_mode(uint16_t mode);
 
/*--- log.c -----------------------------------------------------------------*/

/**
 * Use a shared context, and drop the private log context
 */
SR_API void ds_log_set_context(xlog_context *ctx); 

/**
 * Set the private log context level
 */
SR_API void ds_log_level(int level);


// A new device attached, user need to call ds_get_device_list to get the list,
// the last one is new.
// User can call ds_active_device() to switch to the current device.
//#define DS_EV_NEW_DEVICE_ATTACH         1

// The current device detached, user need to call ds_get_device_list to get the list,
// and call ds_active_device() to switch to the current device.
//#define DS_EV_CURRENT_DEVICE_DETACH     2

// A inactive device detached.
// User can call ds_get_device_list() to get the new list, and update the list view.
//#define DS_EV_INACTIVE_DEVICE_DETACH    3

// The collect task is ends.
#define DS_EV_COLLECT_TASK_START		101

// The collect task is ends.
#define DS_EV_COLLECT_TASK_END			102

// The device is running
#define DS_EV_DEVICE_RUNNING			103

// The device is stopped
#define DS_EV_DEVICE_STOPPED			104

#define DS_EV_COLLECT_TASK_END_BY_DETACHED	105

#define DS_EV_COLLECT_TASK_END_BY_ERROR		106

#define DS_EV_DEVICE_SPEED_NOT_MATCH	107

enum DS_DEVICE_EVENT_TYPE
{
	DS_EV_NEW_DEVICE_ATTACH = 1,
	DS_EV_CURRENT_DEVICE_DETACH = 2,
	DS_EV_INACTIVE_DEVICE_DETACH = 3,
};


typedef unsigned long long ds_device_handle;

#define NULL_HANDLE		0

/**
 * Device base info
 */
struct ds_device_base_info
{
	ds_device_handle handle;
	char 	name[150];
};

struct ds_device_full_info
{
	ds_device_handle handle;
	char 	name[150];
	char 	path[256]; //file path
	char 	driver_name[20];
	int 	dev_type; // enum sr_device_type
	int 	actived_times;
	struct sr_dev_inst *di;
};

struct ds_task_progress
{
	int progress;
	int is_end;
};

struct ds_store_extra_data
{
	char name[50];
	char *data;
	int  data_length;
};

/*---lib_main.c -----------------------------------------------*/

/**
 * event see enum libsigrok_event_type
 */
typedef void (*dslib_event_callback_t)(int event);

/**
 * Data forwarding callback collected by the device.
 */
typedef void (*ds_datafeed_callback_t)(const struct sr_dev_inst *sdi,
						const struct sr_datafeed_packet *packet);

/**
 * Must call first
 */
SR_API int ds_lib_init();

/**
 * Free all resource before program exits
 */
SR_API int ds_lib_exit(); 

/**
 * Set event callback, event type see enum libsigrok_event_type
 */
SR_API void ds_set_event_callback(dslib_event_callback_t cb);

/**
 * Set the data receive callback.
 */
SR_API void ds_set_datafeed_callback(ds_datafeed_callback_t cb);

/**
 * Set the firmware binary file directory,
 * User must call it to set the firmware resource directory
 */
SR_API void ds_set_firmware_resource_dir(const char *dir);

/**
 * Set user data directory.
*/
SR_API void ds_set_user_data_dir(const char *dir);

/**
 * Get the device list, if the field _handle is 0, the list visited to end.
 * User need call g_free() to release the buffer. If the list is empty, the out_list is null.
 */
SR_API int ds_get_device_list(struct ds_device_base_info** out_list, int *out_count);

/**
 * Active a device.
 */
SR_API int ds_active_device(ds_device_handle handle);

/**
 * Active a device,
 * if @index is -1, will select the last one.
 */
SR_API int ds_active_device_by_index(int index);

/**
 * Get the selected device index.
 */
SR_API int ds_get_actived_device_index();

/**
 * Detect whether the active device exists
 */
SR_API int ds_have_actived_device();

/**
 * Create a device from session file, and append to the list.
 */
SR_API int ds_device_from_file(const char *file_path);

/**
 * Remove one device from the list, and destory it.
 * User need to call ds_get_device_list() to get the new list.
 */
SR_API int ds_remove_device(ds_device_handle handle);

/**
 * Get the decive supports work mode, mode list: LOGIC、ANALOG、DSO
 * return type see struct sr_dev_mode.
 */
SR_API const GSList *ds_get_actived_device_mode_list();

/**
 * Get the actived device info.
 * If the actived device is not exists, the handle filed will be set null.
 */
SR_API int ds_get_actived_device_info(struct ds_device_full_info *fill_info);

/**
 * Get actived device work model. mode list:LOGIC、ANALOG、DSO
 */
SR_API int ds_get_actived_device_mode();

/**
 * Start collect data
 */
SR_API int ds_start_collect();

/**
 * Stop collect data, but not close the device.
 */
SR_API int ds_stop_collect();

/**
 * Check if the device is collecting.
 */
SR_API int ds_is_collecting();

/**
 * Close the actived device, and stop collect.
 */
SR_API int ds_release_actived_device();

SR_API int ds_get_last_error();

SR_API int ds_reload_device_list();

SR_API int ds_close_all_device();

/*---config -----------------------------------------------*/
SR_API int ds_get_actived_device_config(const struct sr_channel *ch,
                         const struct sr_channel_group *cg,
                         int key, GVariant **data);

SR_API int ds_set_actived_device_config(const struct sr_channel *ch,
                         const struct sr_channel_group *cg,
                         int key, GVariant *data);

SR_API int ds_get_actived_device_config_list(const struct sr_channel_group *cg,
                          int key, GVariant **data);

SR_API const struct sr_config_info* ds_get_actived_device_config_info(int key);

SR_API const struct sr_config_info* ds_get_actived_device_config_info_by_name(const char *optname);

SR_API int ds_get_actived_device_status(struct sr_status *status, gboolean prg);

SR_API struct sr_config *ds_new_config(int key, GVariant *data);

SR_API void ds_free_config(struct sr_config *src);

SR_API int ds_get_actived_device_init_status(int *status);

/**
  The session file options value text,convert to code.
*/
SR_API int ds_dsl_option_value_to_code(int work_mode, int config_id, const char *value);

/*----------channel----------*/
SR_API int ds_enable_device_channel(const struct sr_channel *ch, gboolean enable);

SR_API int ds_enable_device_channel_index(int ch_index, gboolean enable);

SR_API int ds_set_device_channel_name(int ch_index, const char *name);

/**
 *  heck that at least one probe is enabled
 */
int ds_channel_is_enabled();

GSList* ds_get_actived_device_channels();

/*-----------------trigger---------------*/
int ds_trigger_is_enabled();


#ifdef __cplusplus
}
#endif

#endif
