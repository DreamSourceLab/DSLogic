/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2013 DreamSourceLab <support@dreamsourcelab.com>
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

#ifndef LIBDSL_HARDWARE_DSL_H
#define LIBDSL_HARDWARE_DSL_H

#include <glib.h> 
#include "../../libsigrok-internal.h"
#include "command.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include <sys/stat.h>
#include <inttypes.h>

 
#define USB_INTERFACE		0
#define USB_CONFIGURATION	1
#define NUM_TRIGGER_STAGES	16
#define NUM_SIMUL_TRANSFERS	64
#define MAX_EMPTY_POLL      16

#define DSL_REQUIRED_VERSION_MAJOR	2
#define DSL_REQUIRED_VERSION_MINOR	0
#define DSL_HDL_VERSION             0x0E

#define CAPS_FEATURE_NONE 0
// voltage threshold
#define CAPS_FEATURE_VTH (1 << 0)
// with external buffer
#define CAPS_FEATURE_BUF (1 << 1)
// pre offset control
#define CAPS_FEATURE_PREOFF (1 << 2)
// small startup eemprom
#define CAPS_FEATURE_SEEP (1 << 3)
// zero calibration ability
#define CAPS_FEATURE_ZERO (1 << 4)
// use HMCAD1511 adc chip
#define CAPS_FEATURE_HMCAD1511 (1 << 5)
// usb 3.0
#define CAPS_FEATURE_USB30 (1 << 6)
// pogopin panel
#define CAPS_FEATURE_POGOPIN (1 << 7)
// use ADF4360-7 vco chip
#define CAPS_FEATURE_ADF4360 (1 << 8)
// 20M bandwidth limitation
#define CAPS_FEATURE_20M (1 << 9)
// use startup flash (fx3)
#define CAPS_FEATURE_FLASH (1 << 10)
// 32 channels
#define CAPS_FEATURE_LA_CH32 (1 << 11)
// auto tunning vgain
#define CAPS_FEATURE_AUTO_VGAIN (1 << 12)
// max 2.5v fpga threshold
#define CAPS_FEATURE_MAX25_VTH (1 << 13)
// security check
#define CAPS_FEATURE_SECURITY (1 << 14)
/* end */


#define DSLOGIC_ATOMIC_BITS 6
#define DSLOGIC_ATOMIC_SAMPLES (1 << DSLOGIC_ATOMIC_BITS)
#define DSLOGIC_ATOMIC_SIZE (1 << (DSLOGIC_ATOMIC_BITS - 3))
#define DSLOGIC_ATOMIC_MASK (0xFFFF << DSLOGIC_ATOMIC_BITS)

/*
 * for basic configuration
 */
#define TRIG_EN_BIT 0
#define CLK_TYPE_BIT 1
#define CLK_EDGE_BIT 2
#define RLE_MODE_BIT 3
#define DSO_MODE_BIT 4
#define HALF_MODE_BIT 5
#define QUAR_MODE_BIT 6
#define ANALOG_MODE_BIT 7
#define FILTER_BIT 8
#define INSTANT_BIT 9
#define SLOW_ACQ_BIT 10
#define STRIG_MODE_BIT 11
#define STREAM_MODE_BIT 12
#define LPB_TEST_BIT 13
#define EXT_TEST_BIT 14
#define INT_TEST_BIT 15

#define bmNONE          0
#define bmEEWP          (1 << 0)
#define bmFORCE_RDY     (1 << 1)
#define bmFORCE_STOP    (1 << 2)
#define bmSCOPE_SET     (1 << 3)
#define bmSCOPE_CLR     (1 << 4)
#define bmBW20M_SET     (1 << 5)
#define bmBW20M_CLR     (1 << 6)

/*
 * packet content check
 */
#define TRIG_CHECKID 0x55555555
#define DSO_PKTID 0xa500

/*
 * zero configuration
 */
#define DSO_ZERO_PAGE 8
#define MAX_ACC_VARIANCE 0.0005
/*
 * for DSCope device
 * trans: x << 8 + y
 * x = vpos(coarse), each step(1024 total) indicate x(mv) at 1/20 attenuation, and x/10(mv) at 1/2 attenuation
 * y = voff(fine), each step(1024 total) indicate y/100(mv) at 1/20 attenuation, adn y/1000(mv) at 1/2 attenuation
 * voff: x << 10 + y
 * x = vpos(coarse) default bias
 * y = voff(fine) default bias
 * the final offset: x+DSCOPE_CONSTANT_BIAS->vpos(coarse); y->voff(fine)
 */
#define DSCOPE_CONSTANT_BIAS 160
#define DSCOPE_TRANS_CMULTI   10
#define DSCOPE_TRANS_FMULTI   100.0

/*
 * for DSCope20 device
 * trans: the whole windows offset map to the offset pwm(1024 total)
 * voff: offset pwm constant bias to balance circuit offset
 */
#define CALI_VGAIN_RANGE 200

struct DSL_caps {
    uint64_t mode_caps;
    uint64_t feature_caps;
    uint64_t channels;
    uint64_t total_ch_num;
    uint64_t hw_depth;
    uint64_t dso_depth;
    uint8_t intest_channel;
    const uint64_t *vdivs;
    const uint64_t *samplerates;
    uint8_t vga_id;
    uint16_t default_channelmode;
    uint64_t default_samplerate;
    uint64_t default_samplelimit;
    uint16_t default_pwmtrans;
    uint16_t default_pwmmargin;
    uint32_t ref_min;
    uint32_t ref_max;
    uint16_t default_comb_comp;
    uint64_t half_samplerate;
    uint64_t quarter_samplerate;
};

struct DSL_profile {
    uint16_t vid;
    uint16_t pid;
    enum libusb_speed usb_speed;

    const char *vendor;
    const char *model; //product name
    const char *model_version;

    const char *firmware;

    const char *fpga_bit33;
    const char *fpga_bit50;

    struct DSL_caps dev_caps;
};

static const uint64_t vdivs10to2000[] = {
    SR_mV(10),
    SR_mV(20),
    SR_mV(50),
    SR_mV(100),
    SR_mV(200),
    SR_mV(500),
    SR_V(1),
    SR_V(2),
    0,
};

static const uint64_t samplerates100[] = {
    SR_HZ(10),
    SR_HZ(20),
    SR_HZ(50),
    SR_HZ(100),
    SR_HZ(200),
    SR_HZ(500),
    SR_KHZ(1),
    SR_KHZ(2),
    SR_KHZ(5),
    SR_KHZ(10),
    SR_KHZ(20),
    SR_KHZ(40),
    SR_KHZ(50),
    SR_KHZ(100),
    SR_KHZ(200),
    SR_KHZ(400),
    SR_KHZ(500),
    SR_MHZ(1),
    SR_MHZ(2),
    SR_MHZ(4),
    SR_MHZ(5),
    SR_MHZ(10),
    SR_MHZ(20),
    SR_MHZ(25),
    SR_MHZ(50),
    SR_MHZ(100),
    0,
};

static const uint64_t samplerates400[] = {
    SR_HZ(10),
    SR_HZ(20),
    SR_HZ(50),
    SR_HZ(100),
    SR_HZ(200),
    SR_HZ(500),
    SR_KHZ(1),
    SR_KHZ(2),
    SR_KHZ(5),
    SR_KHZ(10),
    SR_KHZ(20),
    SR_KHZ(40),
    SR_KHZ(50),
    SR_KHZ(100),
    SR_KHZ(200),
    SR_KHZ(400),
    SR_KHZ(500),
    SR_MHZ(1),
    SR_MHZ(2),
    SR_MHZ(4),
    SR_MHZ(5),
    SR_MHZ(10),
    SR_MHZ(20),
    SR_MHZ(25),
    SR_MHZ(50),
    SR_MHZ(100),
    SR_MHZ(200),
    SR_MHZ(400),
    0,
};

static const uint64_t samplerates1000[] = {
    SR_HZ(10),
    SR_HZ(20),
    SR_HZ(50),
    SR_HZ(100),
    SR_HZ(200),
    SR_HZ(500),
    SR_KHZ(1),
    SR_KHZ(2),
    SR_KHZ(5),
    SR_KHZ(10),
    SR_KHZ(20),
    SR_KHZ(40),
    SR_KHZ(50),
    SR_KHZ(100),
    SR_KHZ(200),
    SR_KHZ(400),
    SR_KHZ(500),
    SR_MHZ(1),
    SR_MHZ(2),
    SR_MHZ(4),
    SR_MHZ(5),
    SR_MHZ(10),
    SR_MHZ(20),
    SR_MHZ(25),
    SR_MHZ(50),
    SR_MHZ(100),
    SR_MHZ(125),
    SR_MHZ(250),
    SR_MHZ(500),
    SR_GHZ(1),
    0,
};

struct DSL_vga {
    uint8_t id;
    uint64_t key;
    uint64_t vgain;
    uint16_t preoff;
    uint16_t preoff_comp;
};
static const struct DSL_vga vga_defaults[] = {
    {1, 10,   0x162400, (32<<10)+558, (32<<10)+558},
    {1, 20,   0x14C000, (32<<10)+558, (32<<10)+558},
    {1, 50,   0x12E800, (32<<10)+558, (32<<10)+558},
    {1, 100,  0x118000, (32<<10)+558, (32<<10)+558},
    {1, 200,  0x102400, (32<<10)+558, (32<<10)+558},
    {1, 500,  0x2E800,  (32<<10)+558, (32<<10)+558},
    {1, 1000, 0x18000,  (32<<10)+558, (32<<10)+558},
    {1, 2000, 0x02400,  (32<<10)+558, (32<<10)+558},

    {2, 10,   0x1DA800, 45, 1024-920-45},
    {2, 20,   0x1A7200, 45, 1024-920-45},
    {2, 50,   0x164200, 45, 1024-920-45},
    {2, 100,  0x131800, 45, 1024-920-45},
    {2, 200,  0xBD000,  45, 1024-920-45},
    {2, 500,  0x7AD00,  45, 1024-920-45},
    {2, 1000, 0x48800,  45, 1024-920-45},
    {2, 2000, 0x12000,  45, 1024-920-45},

    {3, 10,   0x1C5C00, 45, 1024-920-45},
    {3, 20,   0x19EB00, 45, 1024-920-45},
    {3, 50,   0x16AE00, 45, 1024-920-45},
    {3, 100,  0x143D00, 45, 1024-920-45},
    {3, 200,  0xB1000,  45, 1024-920-45},
    {3, 500,  0x7F000,  45, 1024-920-45},
    {3, 1000, 0x57200,  45, 1024-920-45},
    {3, 2000, 0x2DD00,  45, 1024-920-45},

    {4, 10,   0x1C6C00, 60, 1024-900-60},
    {4, 20,   0x19E000, 60, 1024-900-60},
    {4, 50,   0x16A800, 60, 1024-900-60},
    {4, 100,  0x142800, 60, 1024-900-60},
    {4, 200,  0xC7F00,  60, 1024-900-60},
    {4, 500,  0x94000,  60, 1024-900-60},
    {4, 1000, 0x6CF00,  60, 1024-900-60},
    {4, 2000, 0x44F00,  60, 1024-900-60},

    {5, 10,   0x1C3400, 60, 1024-900-60},
    {5, 20,   0x19BD00, 60, 1024-900-60},
    {5, 50,   0x167400, 60, 1024-900-60},
    {5, 100,  0x13F300, 60, 1024-900-60},
    {5, 200,  0xC4F00,  60, 1024-900-60},
    {5, 500,  0x91B00,  60, 1024-900-60},
    {5, 1000, 0x69D00,  60, 1024-900-60},
    {5, 2000, 0x41D00,  60, 1024-900-60},

    {0, 0, 0, 0, 0}
};

struct DSL_channels {
    enum DSL_CHANNEL_ID id;
    enum OPERATION_MODE mode;
    enum CHANNEL_TYPE type;
    gboolean stream;
    uint16_t num;
    uint16_t vld_num;
    uint8_t unit_bits;
    uint64_t min_samplerate;
    uint64_t max_samplerate;
    uint64_t hw_min_samplerate;
    uint64_t hw_max_samplerate;
    uint8_t pre_div;
    const char *descr;
};

static const struct DSL_channels channel_modes[] = {
    // LA Stream
    {DSL_STREAM20x16,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE,  16, 16, 1, SR_KHZ(10), SR_MHZ(20),
        SR_KHZ(10), SR_MHZ(100), 1, "Use 16 Channels (Max 20MHz)"},
    {DSL_STREAM25x12,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE,  16, 12, 1, SR_KHZ(10), SR_MHZ(25),
        SR_KHZ(10), SR_MHZ(100), 1, "Use 12 Channels (Max 25MHz)"},
    {DSL_STREAM50x6,   LOGIC,  SR_CHANNEL_LOGIC,  TRUE,  16, 6,  1, SR_KHZ(10), SR_MHZ(50),
        SR_KHZ(10), SR_MHZ(100), 1, "Use 6 Channels (Max 50MHz)"},
    {DSL_STREAM100x3,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE,  16, 3,  1, SR_KHZ(10), SR_MHZ(100),
        SR_KHZ(10), SR_MHZ(100), 1, "Use 3 Channels (Max 100MHz)"},

    {DSL_STREAM20x16_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 16, 16, 1, SR_KHZ(10), SR_MHZ(20),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 16 Channels (Max 20MHz)"},
    {DSL_STREAM25x12_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 16, 12, 1, SR_KHZ(10), SR_MHZ(25),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 12 Channels (Max 25MHz)"},
    {DSL_STREAM50x6_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 16, 6, 1, SR_KHZ(10), SR_MHZ(50),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 6 Channels (Max 50MHz)"},
    {DSL_STREAM100x3_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 16, 3, 1, SR_KHZ(10), SR_MHZ(100),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 3 Channels (Max 100MHz)"},

    {DSL_STREAM10x32_32_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 32, 32, 1, SR_KHZ(10), SR_MHZ(10),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 32 Channels (Max 10MHz)"},
    {DSL_STREAM20x16_32_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 32, 16, 1, SR_KHZ(10), SR_MHZ(20),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 16 Channels (Max 20MHz)"},
    {DSL_STREAM25x12_32_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 32, 12, 1, SR_KHZ(10), SR_MHZ(25),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 12 Channels (Max 25MHz)"},
    {DSL_STREAM50x6_32_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 32, 6, 1, SR_KHZ(10), SR_MHZ(50),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 6 Channels (Max 50MHz)"},
    {DSL_STREAM100x3_32_3DN2,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 32, 3, 1, SR_KHZ(10), SR_MHZ(100),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 3 Channels (Max 100MHz)"},

    {DSL_STREAM50x32,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 32, 32, 1, SR_KHZ(10), SR_MHZ(50),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 32 Channels (Max 50MHz)"},
    {DSL_STREAM100x30,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 32, 30, 1, SR_KHZ(10), SR_MHZ(100),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 30 Channels (Max 100MHz)"},
    {DSL_STREAM250x12,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 32, 12, 1, SR_KHZ(10), SR_MHZ(250),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 12 Channels (Max 250MHz)"},
    {DSL_STREAM125x16_16,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 16, 16, 1, SR_KHZ(10), SR_MHZ(125),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 16 Channels (Max 125MHz)"},
    {DSL_STREAM250x12_16,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 16, 12, 1, SR_KHZ(10), SR_MHZ(250),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 12 Channels (Max 250MHz)"},
    {DSL_STREAM500x6,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE,  16, 6,  1, SR_KHZ(10), SR_MHZ(500),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 6 Channels (Max 500MHz)"},
    {DSL_STREAM1000x3,  LOGIC,  SR_CHANNEL_LOGIC,  TRUE, 8, 3,  1, SR_KHZ(10), SR_GHZ(1),
        SR_KHZ(10), SR_MHZ(500), 5, "Use 3 Channels (Max 1GHz)"},

    // LA Buffer
    {DSL_BUFFER100x16, LOGIC,  SR_CHANNEL_LOGIC,  FALSE, 16, 16, 1, SR_KHZ(10), SR_MHZ(100),
        SR_KHZ(10), SR_MHZ(100), 1, "Use Channels 0~15 (Max 100MHz)"},
    {DSL_BUFFER200x8,  LOGIC,  SR_CHANNEL_LOGIC,  FALSE, 8, 8,  1, SR_KHZ(10), SR_MHZ(200),
        SR_KHZ(10), SR_MHZ(100), 1, "Use Channels 0~7 (Max 200MHz)"},
    {DSL_BUFFER400x4,  LOGIC,  SR_CHANNEL_LOGIC,  FALSE, 4, 4,  1, SR_KHZ(10), SR_MHZ(400),
        SR_KHZ(10), SR_MHZ(100), 1, "Use Channels 0~3 (Max 400MHz)"},

    {DSL_BUFFER250x32,  LOGIC,  SR_CHANNEL_LOGIC,  FALSE, 32, 32,  1, SR_KHZ(10), SR_MHZ(250),
        SR_KHZ(10), SR_MHZ(500), 5, "Use Channels 0~31 (Max 250MHz)"},
    {DSL_BUFFER500x16,  LOGIC,  SR_CHANNEL_LOGIC,  FALSE, 16, 16,  1, SR_KHZ(10), SR_MHZ(500),
        SR_KHZ(10), SR_MHZ(500), 5, "Use Channels 0~15 (Max 500MHz)"},
    {DSL_BUFFER1000x8,  LOGIC,  SR_CHANNEL_LOGIC,  FALSE, 8, 8,  1, SR_KHZ(10), SR_GHZ(1),
        SR_KHZ(10), SR_MHZ(500), 5, "Use Channels 0~7 (Max 1GHz)"},

    // DAQ
    {DSL_ANALOG10x2,   ANALOG, SR_CHANNEL_ANALOG, TRUE,  2, 2,  8, SR_HZ(10),  SR_MHZ(10),
        SR_KHZ(10), SR_MHZ(100), 1, "Use Channels 0~1 (Max 10MHz)"},
    {DSL_ANALOG10x2_500,   ANALOG, SR_CHANNEL_ANALOG, TRUE,  2, 2,  8, SR_HZ(10),  SR_MHZ(10),
        SR_KHZ(10), SR_MHZ(500), 1, "Use Channels 0~1 (Max 10MHz)"},

    // OSC
    {DSL_DSO200x2,     DSO,    SR_CHANNEL_DSO,    FALSE, 2, 2,  8, SR_KHZ(10), SR_MHZ(200),
        SR_KHZ(10), SR_MHZ(100), 1, "Use Channels 0~1 (Max 200MHz)"},
    {DSL_DSO1000x2,    DSO,    SR_CHANNEL_DSO,    FALSE, 2, 2,  8, SR_KHZ(10), SR_GHZ(1),
        SR_KHZ(10), SR_MHZ(500), 1, "Use Channels 0~1 (Max 1GHz)"}
};

/* hardware Capabilities */
#define CAPS_MODE_LOGIC     (1 << 0)
#define CAPS_MODE_ANALOG    (1 << 1)
#define CAPS_MODE_DSO       (1 << 2)

static const struct DSL_profile supported_DSLogic[] = {
    /*
     * DSLogic
     */
    {DS_VENDOR_ID, 0x0001, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic", NULL,
     "DSLogic.fw",
     "DSLogic33.bin",
     "DSLogic50.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_SEEP | CAPS_FEATURE_BUF,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16) | (1 << DSL_BUFFER200x8) | (1 << DSL_BUFFER400x4) |
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      16,
      SR_MB(256),
      SR_Mn(2),
      DSL_BUFFER100x16,
      vdivs10to2000,
      samplerates400,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },

    {DS_VENDOR_ID, 0x0003, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic Pro", NULL,
     "DSLogicPro.fw",
     "DSLogicPro.bin",
     "DSLogicPro.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_SEEP | CAPS_FEATURE_VTH | CAPS_FEATURE_BUF,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16) | (1 << DSL_BUFFER200x8) | (1 << DSL_BUFFER400x4),
      16,
      SR_MB(256),
      0,
      DSL_BUFFER100x16,
      0,
      samplerates400,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },

    {DS_VENDOR_ID, 0x0020, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic PLus", NULL,
     "DSLogicPlus.fw",
     "DSLogicPlus.bin",
     "DSLogicPlus.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16) | (1 << DSL_BUFFER200x8) | (1 << DSL_BUFFER400x4),
      16,
      SR_MB(256),
      0,
      DSL_BUFFER100x16,
      0,
      samplerates400,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },

    {DS_VENDOR_ID, 0x0021, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic Basic", NULL,
     "DSLogicBasic.fw",
     "DSLogicBasic.bin",
     "DSLogicBasic.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16) | (1 << DSL_BUFFER200x8) | (1 << DSL_BUFFER400x4),
      16,
      SR_KB(256),
      0,
      DSL_STREAM20x16,
      0,
      samplerates400,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },

    {DS_VENDOR_ID, 0x0029, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic U2Basic", NULL,
     "DSLogicU2Basic.fw",
     "DSLogicU2Basic.bin",
     "DSLogicU2Basic.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16),
      16,
      SR_MB(64),
      0,
      DSL_BUFFER100x16,
      0,
      samplerates100,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },

    {DS_VENDOR_ID, 0x002A, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic U3Pro16", NULL,
     "DSLogicU3Pro16.fw",
     "DSLogicU3Pro16.bin",
     "DSLogicU3Pro16.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_USB30  | CAPS_FEATURE_ADF4360,
      (1 << DSL_STREAM20x16_3DN2) | (1 << DSL_STREAM25x12_3DN2) | (1 << DSL_STREAM50x6_3DN2) | (1 << DSL_STREAM100x3_3DN2) |
      (1 << DSL_BUFFER500x16) | (1 << DSL_BUFFER1000x8),
      16,
      SR_GB(2),
      0,
      DSL_BUFFER500x16,
      0,
      samplerates1000,
      0,
      DSL_STREAM20x16_3DN2,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(500),
      SR_GHZ(1)}
    },

    {DS_VENDOR_ID, 0x002A, LIBUSB_SPEED_SUPER, "DreamSourceLab", "DSLogic U3Pro16", NULL,
     "DSLogicU3Pro16.fw",
     "DSLogicU3Pro16.bin",
     "DSLogicU3Pro16.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_USB30 | CAPS_FEATURE_ADF4360,
      (1 << DSL_STREAM125x16_16) | (1 << DSL_STREAM250x12_16) | (1 << DSL_STREAM500x6) | (1 << DSL_STREAM1000x3) |
      (1 << DSL_BUFFER500x16) | (1 << DSL_BUFFER1000x8),
      16,
      SR_GB(2),
      0,
      DSL_BUFFER500x16,
      0,
      samplerates1000,
      0,
      DSL_STREAM125x16_16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(500),
      SR_GHZ(1)}
    },

    {DS_VENDOR_ID, 0x002C, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic U3Pro32", NULL,
     "DSLogicU3Pro32.fw",
     "DSLogicU3Pro32.bin",
     "DSLogicU3Pro32.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_USB30  | CAPS_FEATURE_ADF4360 | CAPS_FEATURE_LA_CH32,
      (1 << DSL_STREAM10x32_32_3DN2) | (1 << DSL_STREAM20x16_32_3DN2) | (1 << DSL_STREAM25x12_32_3DN2) | (1 << DSL_STREAM50x6_32_3DN2) | (1 << DSL_STREAM100x3_32_3DN2) |
      (1 << DSL_BUFFER250x32) | (1 << DSL_BUFFER500x16) | (1 << DSL_BUFFER1000x8),
      32,
      SR_GB(2),
      0,
      DSL_BUFFER250x32,
      0,
      samplerates1000,
      0,
      DSL_STREAM10x32_32_3DN2,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(500),
      SR_GHZ(1)}
    },

    {DS_VENDOR_ID, 0x002C, LIBUSB_SPEED_SUPER, "DreamSourceLab", "DSLogic U3Pro32", NULL,
     "DSLogicU3Pro32.fw",
     "DSLogicU3Pro32.bin",
     "DSLogicU3Pro32.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_USB30 | CAPS_FEATURE_ADF4360 | CAPS_FEATURE_LA_CH32,
      (1 << DSL_STREAM50x32) | (1 << DSL_STREAM100x30) | (1 << DSL_STREAM250x12) | (1 << DSL_STREAM500x6) | (1 << DSL_STREAM1000x3) |
      (1 << DSL_BUFFER250x32) | (1 << DSL_BUFFER500x16) | (1 << DSL_BUFFER1000x8),
      32,
      SR_GB(2),
      0,
      DSL_BUFFER250x32,
      0,
      samplerates1000,
      0,
      DSL_STREAM50x32,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(500),
      SR_GHZ(1)}
    },

    {DS_VENDOR_ID, 0x002D, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic U2Pro16", NULL,
     "DSLogicU2Pro16.fw",
     "DSLogicU2Pro16.bin",
     "DSLogicU2Pro16.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_ADF4360 | CAPS_FEATURE_SECURITY,
      (1 << DSL_STREAM20x16_3DN2) | (1 << DSL_STREAM25x12_3DN2) | (1 << DSL_STREAM50x6_3DN2) | (1 << DSL_STREAM100x3_3DN2) |
      (1 << DSL_BUFFER500x16) | (1 << DSL_BUFFER1000x8),
      16,
      SR_GB(4),
      0,
      DSL_BUFFER500x16,
      0,
      samplerates1000,
      0,
      DSL_STREAM20x16_3DN2,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(500),
      SR_GHZ(1)}
    },

    {0x2A0E, 0x0030, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic PLus", NULL,
     "DSLogicPlus.fw",
     "DSLogicPlus-pgl12.bin",
     "DSLogicPlus-pgl12.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_MAX25_VTH | CAPS_FEATURE_SECURITY,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16) | (1 << DSL_BUFFER200x8) | (1 << DSL_BUFFER400x4),
      16,
      SR_MB(256),
      0,
      DSL_BUFFER100x16,
      0,
      samplerates400,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },

    {0x2A0E, 0x0031, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic U2Basic", NULL,
     "DSLogicU2Basic.fw",
     "DSLogicU2Basic-pgl12.bin",
     "DSLogicU2Basic-pgl12.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_MAX25_VTH | CAPS_FEATURE_SECURITY,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16),
      16,
      SR_MB(64),
      0,
      DSL_BUFFER100x16,
      0,
      samplerates100,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },

    {0x2A0E, 0x0034, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic PLus", NULL,
     "DSLogicPlus-pgl12-2.fw",
     "DSLogicPlus-pgl12-2.bin",
     "DSLogicPlus-pgl12-2.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_MAX25_VTH | CAPS_FEATURE_SECURITY,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16) | (1 << DSL_BUFFER200x8) | (1 << DSL_BUFFER400x4),
      16,
      SR_MB(256),
      0,
      DSL_BUFFER100x16,
      0,
      samplerates400,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },

    {0x2A0E, 0x0035, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic U2Basic", NULL,
     "DSLogicU2Basic-pgl12-2.fw",
     "DSLogicU2Basic-pgl12-2.bin",
     "DSLogicU2Basic-pgl12-2.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_VTH | CAPS_FEATURE_BUF | CAPS_FEATURE_MAX25_VTH | CAPS_FEATURE_SECURITY,
      (1 << DSL_STREAM20x16) | (1 << DSL_STREAM25x12) | (1 << DSL_STREAM50x6) | (1 << DSL_STREAM100x3) |
      (1 << DSL_BUFFER100x16),
      16,
      SR_MB(64),
      0,
      DSL_BUFFER100x16,
      0,
      samplerates100,
      0,
      DSL_STREAM20x16,
      SR_MHZ(1),
      SR_Mn(1),
      0,
      0,
      0,
      0,
      0,
      SR_MHZ(200),
      SR_MHZ(400)}
    },


    { 0, 0, LIBUSB_SPEED_UNKNOWN, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}
};

static const struct DSL_profile supported_DSCope[] = {
    /*
     * DSCope
     */
    {DS_VENDOR_ID, 0x0002, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope", NULL,
     "DSCope.fw",
     "DSCope.bin",
     "DSCope.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_PREOFF | CAPS_FEATURE_SEEP | CAPS_FEATURE_BUF,
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      2,
      SR_MB(256),
      SR_Mn(2),
      0,
      vdivs10to2000,
      samplerates400,
      1,
      DSL_DSO200x2,
      SR_MHZ(100),
      SR_Mn(1),
      (129<<8)+167,
      1024-920,
      1,
      255,
      0,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x0004, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope20", NULL,
     "DSCope20.fw",
     "DSCope20.bin",
     "DSCope20.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_SEEP | CAPS_FEATURE_BUF,
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      2,
      SR_MB(256),
      SR_Mn(2),
      0,
      vdivs10to2000,
      samplerates400,
      2,
      DSL_DSO200x2,
      SR_MHZ(100),
      SR_Mn(1),
      920,
      1024-920,
      1,
      255,
      0,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x0022, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope B20", NULL,
     "DSCopeB20.fw",
     "DSCope20.bin",
     "DSCope20.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_BUF,
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      2,
      SR_MB(256),
      SR_Mn(2),
      0,
      vdivs10to2000,
      samplerates400,
      2,
      DSL_DSO200x2,
      SR_MHZ(100),
      SR_Mn(1),
      920,
      1024-920,
      1,
      255,
      0,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x0023, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope C20", NULL,
     "DSCopeC20.fw",
     "DSCopeC20P.bin",
     "DSCopeC20P.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_BUF,
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      2,
      SR_MB(256),
      SR_Mn(2),
      0,
      vdivs10to2000,
      samplerates400,
      3,
      DSL_DSO200x2,
      SR_MHZ(100),
      SR_Mn(1),
      920,
      1024-920,
      1,
      255,
      0,
      SR_HZ(0),
      SR_HZ(0)}
    },


    {DS_VENDOR_ID, 0x0024, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope C20P", NULL,
     "DSCopeC20P.fw",
     "DSCopeC20P.bin",
     "DSCopeC20P.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_BUF | CAPS_FEATURE_POGOPIN,
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      2,
      SR_MB(256),
      SR_Mn(2),
      0,
      vdivs10to2000,
      samplerates400,
      3,
      DSL_DSO200x2,
      SR_MHZ(100),
      SR_Mn(1),
      920,
      1024-920,
      1,
      255,
      0,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x0025, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope C20", NULL,
     "DSCopeC20B.fw",
     "DSCopeC20B.bin",
     "DSCopeC20B.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO,
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      2,
      SR_KB(256),
      SR_Kn(20),
      0,
      vdivs10to2000,
      samplerates400,
      3,
      DSL_DSO200x2,
      SR_MHZ(100),
      SR_Kn(10),
      920,
      1024-920,
      1,
      255,
      0,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x0026, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope U2B20", NULL,
     "DSCopeU2B20.fw",
     "DSCopeU2B20.bin",
     "DSCopeU2B20.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_AUTO_VGAIN,
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      2,
      SR_KB(256),
      SR_Kn(20),
      0,
      vdivs10to2000,
      samplerates400,
      4,
      DSL_DSO200x2,
      SR_MHZ(100),
      SR_Kn(10),
      930,
      1024-930,
      10,
      245,
      22,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x0027, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope U2P20", NULL,
     "DSCopeU2P20.fw",
     "DSCopeU2P20.bin",
     "DSCopeU2P20.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_BUF | CAPS_FEATURE_POGOPIN | CAPS_FEATURE_AUTO_VGAIN,
      (1 << DSL_ANALOG10x2) |
      (1 << DSL_DSO200x2),
      2,
      SR_MB(256),
      SR_Mn(2),
      0,
      vdivs10to2000,
      samplerates400,
      4,
      DSL_DSO200x2,
      SR_MHZ(100),
      SR_Mn(1),
      930,
      1024-930,
      10,
      245,
      22,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x0028, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope U2B100", NULL,
     "DSCopeU2B100.fw",
     "DSCopeU2B100.bin",
     "DSCopeU2B100.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_HMCAD1511 | CAPS_FEATURE_20M,
      (1 << DSL_ANALOG10x2_500) |
      (1 << DSL_DSO1000x2),
      2,
      SR_KB(256),
      SR_Kn(20),
      0,
      vdivs10to2000,
      samplerates1000,
      4,
      DSL_DSO1000x2,
      SR_MHZ(500),
      SR_Kn(10),
      835,
      1024-835,
      10,
      245,
      60,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x002B, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSCope U3P100", NULL,
     "DSCopeU3P100.fw",
     "DSCopeU3P100.bin",
     "DSCopeU3P100.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_POGOPIN | CAPS_FEATURE_FLASH | CAPS_FEATURE_USB30 | CAPS_FEATURE_HMCAD1511 | CAPS_FEATURE_20M,
      (1 << DSL_ANALOG10x2_500) |
      (1 << DSL_DSO1000x2),
      2,
      SR_GB(2),
      SR_Mn(2),
      0,
      vdivs10to2000,
      samplerates1000,
      5,
      DSL_DSO1000x2,
      SR_MHZ(500),
      SR_Mn(1),
      780,
      1024-780,
      10,
      245,
      60,
      SR_HZ(0),
      SR_HZ(0)}
    },

    {DS_VENDOR_ID, 0x002B, LIBUSB_SPEED_SUPER, "DreamSourceLab", "DSCope U3P100", NULL,
     "DSCopeU3P100.fw",
     "DSCopeU3P100.bin",
     "DSCopeU3P100.bin",
     {CAPS_MODE_ANALOG | CAPS_MODE_DSO,
      CAPS_FEATURE_ZERO | CAPS_FEATURE_POGOPIN | CAPS_FEATURE_FLASH | CAPS_FEATURE_USB30 | CAPS_FEATURE_HMCAD1511 | CAPS_FEATURE_20M,
      (1 << DSL_ANALOG10x2_500) |
      (1 << DSL_DSO1000x2),
      2,
      SR_GB(2),
      SR_Mn(2),
      0,
      vdivs10to2000,
      samplerates1000,
      5,
      DSL_DSO1000x2,
      SR_MHZ(500),
      SR_Mn(1),
      780,
      1024-780,
      10,
      245,
      60,
      SR_HZ(0),
      SR_HZ(0)}
    },

    { 0, 0, LIBUSB_SPEED_UNKNOWN, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}
};



enum {
    DSL_ERROR = -1,
    DSL_INIT = 0,
    DSL_START = 1,
    DSL_READY = 2,
    DSL_TRIGGERED = 3,
    DSL_DATA = 4,
    DSL_STOP = 5,
    DSL_FINISH = 7,
    DSL_ABORT = 8,
};

struct DSL_context {
    const struct DSL_profile *profile;
	/*
     * Since we can't keep track of an DSL device after upgrading
	 * the firmware (it renumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	int64_t fw_updated;

	/* Device/capture settings */
	uint64_t cur_samplerate;
	uint64_t limit_samples;
    uint64_t actual_samples;
    uint64_t actual_bytes;

	/* Operational settings */
    gboolean clock_type;
    gboolean clock_edge;
    gboolean rle_mode;
    gboolean rle_support;
    gboolean instant;
    uint16_t op_mode;
    gboolean stream;
    uint8_t  test_mode;
    uint16_t buf_options;
    enum DSL_CHANNEL_ID ch_mode;
    uint16_t samplerates_min_index;
    uint16_t samplerates_max_index;
    uint16_t th_level;
    double vth;
    uint16_t filter;
	uint16_t trigger_mask[NUM_TRIGGER_STAGES];
	uint16_t trigger_value[NUM_TRIGGER_STAGES];
	int trigger_stage;
	uint16_t trigger_buffer[NUM_TRIGGER_STAGES];
    uint64_t timebase;
    uint8_t max_height;
    uint8_t trigger_channel;
    uint8_t trigger_slope;
    uint8_t trigger_source;
    uint8_t trigger_hrate;
    uint32_t trigger_hpos;
    uint64_t trigger_holdoff;
    uint8_t trigger_margin;
    gboolean zero;
    gboolean cali;
    gboolean tune;
    int16_t tune_index;
    int zero_stage;
    int zero_pcnt;
    gboolean zero_branch;
    gboolean zero_comb_fgain;
    gboolean zero_comb;
    int tune_stage;
    int tune_pcnt;
    struct sr_channel *tune_probe;
    gboolean roll;
    gboolean data_lock;
    uint16_t unit_pitch;

    uint64_t num_samples;
    uint64_t num_bytes;
	int submitted_transfers;
	int empty_transfer_count;
    int instant_tail_bytes;

	void *cb_data;
	unsigned int num_transfers;
	struct libusb_transfer **transfers;
	int *usbfd;

    int pipe_fds[2];
    GIOChannel *channel;

    int status;
    int trf_completed;
    gboolean mstatus_valid;
    struct sr_status mstatus;
    gboolean abort;
    gboolean overflow;
    int bw_limit;
    int empty_poll_count;

    int is_loop;

    uint64_t ext_samplerate;
};

/*
 * hardware setting for each capture
 */
struct DSL_setting {
    uint32_t sync;

    uint16_t mode_header;                   // 0
    uint16_t mode;
    uint16_t divider_header;                // 1-2
    uint16_t div_l;
    uint16_t div_h;
    uint16_t count_header;                  // 3-4
    uint16_t cnt_l;
    uint16_t cnt_h;
    uint16_t trig_pos_header;               // 5-6
    uint16_t tpos_l;
    uint16_t tpos_h;
    uint16_t trig_glb_header;               // 7
    uint16_t trig_glb;
    uint16_t dso_count_header;              // 8-9
    uint16_t dso_cnt_l;
    uint16_t dso_cnt_h;
    uint16_t ch_en_header;                  // 10-11
    uint16_t ch_en_l;
    uint16_t ch_en_h;
    uint16_t fgain_header;                  // 12
    uint16_t fgain;

    uint16_t trig_header;                   // 64
    uint16_t trig_mask0[NUM_TRIGGER_STAGES];
    uint16_t trig_mask1[NUM_TRIGGER_STAGES];
    uint16_t trig_value0[NUM_TRIGGER_STAGES];
    uint16_t trig_value1[NUM_TRIGGER_STAGES];
    uint16_t trig_edge0[NUM_TRIGGER_STAGES];
    uint16_t trig_edge1[NUM_TRIGGER_STAGES];
    uint16_t trig_logic0[NUM_TRIGGER_STAGES];
    uint16_t trig_logic1[NUM_TRIGGER_STAGES];
    uint32_t trig_count[NUM_TRIGGER_STAGES];

    uint32_t end_sync;
};

struct DSL_setting_ext32 {
    uint32_t sync;

    uint16_t trig_header;                   // 96
    uint16_t trig_mask0[NUM_TRIGGER_STAGES];
    uint16_t trig_mask1[NUM_TRIGGER_STAGES];
    uint16_t trig_value0[NUM_TRIGGER_STAGES];
    uint16_t trig_value1[NUM_TRIGGER_STAGES];
    uint16_t trig_edge0[NUM_TRIGGER_STAGES];
    uint16_t trig_edge1[NUM_TRIGGER_STAGES];

    uint16_t align_bytes;
    uint32_t end_sync;
};

struct DSL_adc_config {
    uint8_t dest;
    uint8_t cnt;
    uint8_t delay;
    uint8_t byte[4];
};
static const struct DSL_adc_config adc_single_ch0[] = {
    {ADCC_ADDR+1, 3,   0,   {0x03, 0x01, 0x00, 0x00}}, // reset & power down
    {ADCC_ADDR,   4,   0,   {0x00, 0x01, 0x00, 0x31}}, // 1x channel 1/1 clock
    {ADCC_ADDR+1, 1,   0,   {0x01, 0x00, 0x00, 0x00}}, // power up
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x02, 0x3A}}, // adc0: ch0 adc1: ch0
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x02, 0x3B}}, // adc2: ch0 adc3: ch0
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x00, 0x42}}, // phase_ddr: 270 deg
    {ADCC_ADDR,   4,   0,   {0x00, 0x34, 0x00, 0x50}}, // adc core current: -40% (lower performance) / VCM: +-700uA
    {ADCC_ADDR,   4,   0,   {0x00, 0x22, 0x02, 0x11}}, // lvds drive strength: 1.5mA(RSDS)
    {ADCC_ADDR,   4,   0,   {0x00, 0x7F, 0x00, 0x24}}, // invert all channel
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x00, 0x55}}, // full-scale range: -10%
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x00, 0x33}}, // x-gain disabled / fine-gain enabled
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x03, 0x2B}}, // coarse_gain: 3dB(1.4125x)
    {0, 0, 0, {0, 0, 0, 0}}
};
static const struct DSL_adc_config adc_single_ch3[] = {
    {ADCC_ADDR+1, 3,   0,   {0x03, 0x01, 0x00, 0x00}}, // reset & power down
    {ADCC_ADDR,   4,   0,   {0x00, 0x01, 0x00, 0x31}}, // 1x channel 1/1 clock
    {ADCC_ADDR+1, 1,   0,   {0x01, 0x00, 0x00, 0x00}}, // power up
    {ADCC_ADDR,   4,   0,   {0x00, 0x10, 0x10, 0x3A}}, // adc0: ch3 adc1: ch3
    {ADCC_ADDR,   4,   0,   {0x00, 0x10, 0x10, 0x3B}}, // adc2: ch3 adc3: ch3
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x00, 0x42}}, // phase_ddr: 270 deg
    {ADCC_ADDR,   4,   0,   {0x00, 0x34, 0x00, 0x50}}, // adc core current: -40% (lower performance) / VCM: +-700uA
    {ADCC_ADDR,   4,   0,   {0x00, 0x22, 0x02, 0x11}}, // lvds drive strength: 1.5mA(RSDS)
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x00, 0x24}}, // invert none channel
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x00, 0x55}}, // full-scale range: -10%
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x00, 0x33}}, // x-gain disabled / fine-gain enabled
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x03, 0x2B}}, // coarse_gain: 3dB(1.4125x)
    {0, 0, 0, {0, 0, 0, 0}}
};
static const struct DSL_adc_config adc_dual_ch03[] = {
    {ADCC_ADDR+1, 3,   0,   {0x03, 0x01, 0x00, 0x00}}, // reset & power down
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x01, 0x31}}, // 2x channel 1/2 clock
    {ADCC_ADDR+1, 1,   0,   {0x01, 0x00, 0x00, 0x00}}, // power up
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x02, 0x3A}}, // adc0: ch0 adc1: ch0
    {ADCC_ADDR,   4,   0,   {0x00, 0x10, 0x10, 0x3B}}, // adc2: ch3 adc3: ch3
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x00, 0x42}}, // phase_ddr: 270 deg
    {ADCC_ADDR,   4,   0,   {0x00, 0x34, 0x00, 0x50}}, // adc core current: -40% (lower performance) / VCM: +-700uA
    {ADCC_ADDR,   4,   0,   {0x00, 0x22, 0x02, 0x11}}, // lvds drive strength: 1.5mA(RSDS)
    {ADCC_ADDR,   4,   0,   {0x00, 0x11, 0x00, 0x24}}, // invert 0 channel
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x00, 0x55}}, // full-scale range: -10%
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x00, 0x33}}, // x-gain disabled / fine-gain enabled
    {ADCC_ADDR,   4,   0,   {0x00, 0x33, 0x00, 0x2B}}, // coarse_gain: 3dB(1.4125x)
//    {ADCC_ADDR,   4,   0,   {0x00, 0x03, 0x00, 0x33}}, // x-gain enabled / fine-gain enabled
//    {ADCC_ADDR,   4,   0,   {0x00, 0x11, 0x00, 0x2B}}, // coarse_gain: 1.25x
    //{ADCC_ADDR,   4,   0,   {0x00, 0x40, 0x00, 0x34}}, // fine_gain: 1.0077x
    //{ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x60, 0x35}}, // fine_gain: 1.0077x
    //{ADCC_ADDR,   4,   0,   {0x00, 0x10, 0x00, 0x25}}, // fix pattern test
    //{ADCC_ADDR,   4,   0,   {0x00, 0x00, 0xab, 0x26}}, // test pattern
    {0, 0, 0, {0, 0, 0, 0}}
};
static const struct DSL_adc_config adc_init_fix[] = {
    {ADCC_ADDR+1, 3,   0,   {0x03, 0x01, 0x00, 0x00}}, // reset & power down
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x01, 0x31}}, // 2x channel 1/2 clock
    {ADCC_ADDR+1, 1,   0,   {0x01, 0x00, 0x00, 0x00}}, // power up
    {ADCC_ADDR,   4,   0,   {0x00, 0x02, 0x02, 0x3A}}, // adc0: ch0 adc1: ch0
    {ADCC_ADDR,   4,   0,   {0x00, 0x10, 0x10, 0x3B}}, // adc2: ch3 adc3: ch3
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x00, 0x42}}, // phase_ddr: 270 deg
    {ADCC_ADDR,   4,   0,   {0x00, 0x34, 0x00, 0x50}}, // adc core current: -40% (lower performance) / VCM: +-700uA
    {ADCC_ADDR,   4,   0,   {0x00, 0x22, 0x02, 0x11}}, // lvds drive strength: 1.5mA(RSDS)
    {ADCC_ADDR,   4,   0,   {0x00, 0x10, 0x00, 0x25}}, // fix pattern test
    {ADCC_ADDR,   4,   0,   {0x00, 0x00, 0x55, 0x26}}, // test pattern
    {0, 0, 0, {0, 0, 0, 0}}
};
static const struct DSL_adc_config adc_clk_init_1g[] = {
    {ADCC_ADDR+2, 1,   0,   {0x01, 0x00, 0x00, 0x00}}, // power up
    {ADCC_ADDR,   4,   0,   {0x01, 0x61, 0x00, 0x30}}, //
    {ADCC_ADDR,   4,   0,   {0x01, 0x40, 0xF1, 0x46}}, //
    {ADCC_ADDR,   4,   10,  {0x01, 0x62, 0x3D, 0x00}}, //
    {0, 0, 0, {0, 0, 0, 0}}
};
static const struct DSL_adc_config adc_clk_init_500m[] = {
    {ADCC_ADDR+2, 1,   0,   {0x01, 0x00, 0x00, 0x00}}, // power up
    {ADCC_ADDR,   4,   0,   {0x01, 0x61, 0x00, 0x30}}, //
    {ADCC_ADDR,   4,   0,   {0x01, 0x40, 0xF1, 0x46}}, //
    {ADCC_ADDR,   4,   10,  {0x01, 0x62, 0x3D, 0x40}}, //
    {0, 0, 0, {0, 0, 0, 0}}
};
static const struct DSL_adc_config adc_power_down[] = {
    //{ADCC_ADDR+2, 1,   0,   {0x00, 0x00, 0x00, 0x00}}, // ADC_CLK power down
    {ADCC_ADDR+1, 1,   0,   {0x00, 0x00, 0x00, 0x00}}, // ADC power down
    {0, 0, 0, {0, 0, 0, 0}}
};
static const struct DSL_adc_config adc_power_up[] = {
    {ADCC_ADDR+1, 1,   0,   {0x01, 0x00, 0x00, 0x00}}, // ADC power up
    //{ADCC_ADDR+2, 1,   0,   {0x01, 0x00, 0x00, 0x00}}, // ADC_CLK power up
    {0, 0, 0, {0, 0, 0, 0}}
};

SR_PRIV int dsl_adjust_probes(struct sr_dev_inst *sdi, int num_probes);
SR_PRIV int dsl_setup_probes(struct sr_dev_inst *sdi, int num_probes);
SR_PRIV const GSList *dsl_mode_list(const struct sr_dev_inst *sdi);
SR_PRIV void dsl_adjust_samplerate(struct DSL_context *devc);

SR_PRIV int dsl_en_ch_num(const struct sr_dev_inst *sdi);
SR_PRIV gboolean dsl_check_conf_profile(libusb_device *dev);
SR_PRIV int dsl_configure_probes(const struct sr_dev_inst *sdi);
SR_PRIV uint64_t dsl_channel_depth(const struct sr_dev_inst *sdi);

SR_PRIV int dsl_wr_reg(const struct sr_dev_inst *sdi, uint8_t addr, uint8_t value);
SR_PRIV int dsl_rd_reg(const struct sr_dev_inst *sdi, uint8_t addr, uint8_t *value);
SR_PRIV int dsl_wr_ext(const struct sr_dev_inst *sdi, uint8_t addr, uint8_t value);
SR_PRIV int dsl_rd_ext(const struct sr_dev_inst *sdi, unsigned char *ctx, uint16_t addr, uint8_t len);

SR_PRIV int dsl_wr_dso(const struct sr_dev_inst *sdi, uint64_t cmd);
SR_PRIV int dsl_wr_nvm(const struct sr_dev_inst *sdi, unsigned char *ctx, uint16_t addr, uint8_t len);
SR_PRIV int dsl_rd_nvm(const struct sr_dev_inst *sdi, unsigned char *ctx, uint16_t addr, uint8_t len);
SR_PRIV int dsl_rd_probe(const struct sr_dev_inst *sdi, unsigned char *ctx, uint16_t addr, uint8_t len);

SR_PRIV int dsl_config_adc(const struct sr_dev_inst *sdi, const struct DSL_adc_config *config);
SR_PRIV double dsl_adc_code2fgain(uint8_t code);
SR_PRIV uint8_t dsl_adc_fgain2code(double gain);
SR_PRIV int dsl_config_adc_fgain(const struct sr_dev_inst *sdi, uint8_t branch, double gain0, double gain1);
SR_PRIV int dsl_config_fpga_fgain(const struct sr_dev_inst *sdi);
SR_PRIV int dsl_skew_fpga_fgain(const struct sr_dev_inst *sdi, gboolean comb, double skew[]);
SR_PRIV int dsl_probe_cali_fgain(struct DSL_context *devc, struct sr_channel *probe, double mean, gboolean comb, gboolean reset);
SR_PRIV gboolean dsl_probe_fgain_inrange(struct sr_channel *probe, gboolean comb, double skew[]);

SR_PRIV int dsl_fpga_arm(const struct sr_dev_inst *sdi);
SR_PRIV int dsl_fpga_config(struct libusb_device_handle *hdl, const char *filename);

SR_PRIV int dsl_config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
                      const struct sr_channel *ch,
                      const struct sr_channel_group *cg);
SR_PRIV int dsl_config_set(int id, GVariant *data, struct sr_dev_inst *sdi,
                      struct sr_channel *ch,
                      struct sr_channel_group *cg );
SR_PRIV int dsl_config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
                       const struct sr_channel_group *cg);

SR_PRIV int dsl_dev_open(struct sr_dev_driver *di, struct sr_dev_inst *sdi, gboolean *fpga_done);
SR_PRIV int dsl_dev_close(struct sr_dev_inst *sdi);
SR_PRIV int dsl_dev_acquisition_stop(const struct sr_dev_inst *sdi, void *cb_data);
SR_PRIV int dsl_dev_status_get(const struct sr_dev_inst *sdi, struct sr_status *status, gboolean prg);

SR_PRIV unsigned int dsl_get_timeout(const struct sr_dev_inst *sdi);
SR_PRIV int dsl_start_transfers(const struct sr_dev_inst *sdi);
SR_PRIV int dsl_header_size(const struct DSL_context *devc);

SR_PRIV int dsl_destroy_device(struct sr_dev_inst *sdi);

SR_PRIV int dsl_secuCheck(const struct sr_dev_inst *sdi, uint16_t* encryption, int steps);

#endif
