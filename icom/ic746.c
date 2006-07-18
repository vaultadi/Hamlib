/*
 *  Hamlib CI-V backend - description of IC-746 and variations
 *  Copyright (c) 2000-2003 by Stephane Fillod
 *
 *	$Id: ic746.c,v 1.6 2006-07-18 22:51:42 n0nb Exp $
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>  /* String function definitions */

#include <hamlib/rig.h>
#include "token.h"
#include "idx_builtin.h"

#include "icom.h"
#include "icom_defs.h"
#include "frame.h"
#include "bandplan.h"
#include "misc.h"


/*
 * IC-746 and IC-746pro
 *
 * TODO:
 * 	- advanced scanning functions
 * 	- fix: set_ant 0x12,[1|2]
 * 	- set_channel
 * 	- set_ctcss_tone/ctcss_sql
 * 	- set keyer?
 * 	- read IF filter setting?
 * 	- test all that stuff..
 */

#define IC746_ALL_RX_MODES (RIG_MODE_AM|RIG_MODE_CW|RIG_MODE_CWR|RIG_MODE_SSB|RIG_MODE_RTTY|RIG_MODE_RTTYR|RIG_MODE_FM)
#define IC746_1HZ_TS_MODES IC746_ALL_RX_MODES

/* 
 * 100W in all modes but AM (40W)
 * deleted rig_mode_tx_modes
 */ 
#define IC746_OTHER_TX_MODES (RIG_MODE_CW|RIG_MODE_SSB|RIG_MODE_RTTY|RIG_MODE_FM)
#define IC746_AM_TX_MODES (RIG_MODE_AM)

#define IC746_FUNC_ALL (RIG_FUNC_FAGC|RIG_FUNC_NB|RIG_FUNC_COMP|RIG_FUNC_VOX|RIG_FUNC_TONE|RIG_FUNC_TSQL|RIG_FUNC_SBKIN|RIG_FUNC_FBKIN|RIG_FUNC_NR|RIG_FUNC_MON|RIG_FUNC_MN|RIG_FUNC_RF|RIG_FUNC_ANF|RIG_FUNC_VSC|RIG_FUNC_RESUME)

#define IC746_LEVEL_ALL (RIG_LEVEL_AF|RIG_LEVEL_RF|RIG_LEVEL_PREAMP|RIG_LEVEL_ATT|RIG_LEVEL_AGC|RIG_LEVEL_COMP|RIG_LEVEL_BKINDL|RIG_LEVEL_NR|RIG_LEVEL_PBT_IN|RIG_LEVEL_PBT_OUT|RIG_LEVEL_CWPITCH|RIG_LEVEL_RFPOWER|RIG_LEVEL_MICGAIN|RIG_LEVEL_KEYSPD|RIG_LEVEL_NOTCHF|RIG_LEVEL_SQL|RIG_LEVEL_RAWSTR)

#define IC746_GET_PARM (RIG_PARM_BACKLIGHT|RIG_PARM_BEEP)
#define IC746_SET_PARM (RIG_PARM_BACKLIGHT|RIG_PARM_BEEP|RIG_PARM_ANN)

#define IC746_VFO_ALL (RIG_VFO_A|RIG_VFO_B)
#define IC746_ANTS (RIG_ANT_1|RIG_ANT_2)

#define IC746_VFO_OPS (RIG_OP_CPY|RIG_OP_XCHG|RIG_OP_FROM_VFO|RIG_OP_TO_VFO|RIG_OP_MCL|RIG_OP_TUNE)

#define IC746_SCAN_OPS (RIG_SCAN_VFO|RIG_SCAN_MEM|RIG_SCAN_SLCT|RIG_SCAN_PROG|RIG_SCAN_DELTA)

#define IC746_STR_CAL { 16, \
	{ \
		{   0, -60 }, \
		{  10, -55 }, \
		{  27, -49 }, \
		{  45, -42 }, \
		{  60, -35 }, \
		{  76, -28 }, \
		{  89, -21 }, \
		{ 100, -14 }, \
		{ 110, -7 }, \
		{ 120, 0 }, \
		{ 125, 10 }, \
		{ 129, 20 }, \
		{ 133, 30 }, \
		{ 138, 40 }, \
		{ 142, 50 }, \
		{ 146, 60 } \
	} }

#define IC746PRO_MEM_CAP {	\
	.freq = 1,	\
	.mode = 1,	\
	.width = 1,	\
	.tx_freq = 1,	\
	.tx_mode = 1,	\
	.tx_width = 1,	\
	.split = 1,	\
	.rptr_shift = 1,\
	.ctcss_tone = 1,\
	.ctcss_sql = 1, \
	.dcs_code = 1,	\
	.flags = 1,	\
	.channel_desc = 1, \
} 

/* Memory channel buffer structure for IC-746 pro and ?
 Note requires an ack_buff of 64 bytes and data length is 46.
 */


typedef struct {
	unsigned char freq[5];		/* little endian frequency */
	unsigned char mode;
	unsigned char pb;		/* passband or filter selection*/
	unsigned char data;		/* data port 0=off 1=on */
	unsigned char dup;		/* duplex, tone, tonesql and DTCS 
					Values in hex are "or"ed together
					00 = Simplex
					10 = -DUP
					20 = +DUP
					01 = ctcss tone on
					02 = tone_sql on
					03 = DTCS on */
	unsigned char tone[3];		/* ctcss tone bigendian first byte fixed 00 */
	unsigned char tone_sql[3];	/* tone squelch frequency as tone */
	struct	{
		unsigned char pol;	/* DTCS polarity by nibbles Tx pol | Rx pol; 0 = normal; 1 = rev */
		unsigned char code[2];	/* DTCS code bigendian */
	} dcs;
} channel_str_t;

	

typedef struct {
	char chan_flag; 	/* split 0x10 = on; scan select 0x01 = on */
	channel_str_t rx;
	channel_str_t tx;
	char name[9];		/*name 9 ascii no null terminator */
} mem_buf_t;

#define MAX_MEM_BUF_LEN sizeof(mem_buf_t)

/* IC-746 Pro has a 3 "band-stacking registers" defined for each hamband and general coverage. These  are updated and rotated when band is changed from front panel.  The most recent is rolled down and the oldest discarded.  The structure of the register is roughly half a memory buffer.
*/

typedef channel_str_t band_stack_reg_t;

static int ic746_set_parm(RIG *rig, setting_t parm, value_t val);
static int ic746_get_parm(RIG *rig, setting_t parm, value_t *val);
static int ic746pro_get_channel(RIG *rig, channel_t *chan);


/*
 * ic746 rig capabilities.
 */
static const struct icom_priv_caps ic746_priv_caps = { 
		0x56,	/* default address */
		0,		/* 731 mode */
		ic756pro_ts_sc_list
};

const struct rig_caps ic746_caps = {
.rig_model =  RIG_MODEL_IC746,
.model_name = "IC-746", 
.mfg_name =  "Icom", 
.version =  BACKEND_VER,
.copyright =  "LGPL",
.status =  RIG_STATUS_NEW,
.rig_type =  RIG_TYPE_TRANSCEIVER,
.ptt_type =  RIG_PTT_NONE,
.dcd_type =  RIG_DCD_NONE,
.port_type =  RIG_PORT_SERIAL,
.serial_rate_min =  300,
.serial_rate_max =  19200,
.serial_data_bits =  8,
.serial_stop_bits =  1,
.serial_parity =  RIG_PARITY_NONE,
.serial_handshake =  RIG_HANDSHAKE_NONE, 
.write_delay =  0,
.post_write_delay =  0,
.timeout =  200,
.retry =  3, 
.has_get_func =  IC746_FUNC_ALL,
.has_set_func =  IC746_FUNC_ALL, 
.has_get_level =  IC746_LEVEL_ALL,
.has_set_level =  RIG_LEVEL_SET(IC746_LEVEL_ALL),
.has_get_parm =  RIG_PARM_NONE,
.has_set_parm =  RIG_PARM_ANN,
.level_gran = {
	[LVL_RAWSTR] = { .min = { .i = 0 }, .max = { .i = 255 } },
	},
.parm_gran =  {},
.ctcss_list =  common_ctcss_list,
.dcs_list =  NULL,
.preamp =   { 10, 20, RIG_DBLST_END, },	/* FIXME: TBC */
.attenuator =   { 20, RIG_DBLST_END, },
.max_rit =  Hz(9999),
.max_xit =  Hz(0),
.max_ifshift =  Hz(0),
.targetable_vfo =  0,
.vfo_ops =  IC746_VFO_OPS,
.scan_ops =  IC746_SCAN_OPS,
.transceive =  RIG_TRN_RIG,
.bank_qty =   0,
.chan_desc_sz =  0,

.chan_list =  {
			   {   1,  99, RIG_MTYPE_MEM  },
			   { 100, 101, RIG_MTYPE_EDGE },    /* two by two */
			   RIG_CHAN_END,
		},

.rx_range_list1 =   {
		{kHz(30),MHz(60),IC746_ALL_RX_MODES,-1,-1,IC746_VFO_ALL,IC746_ANTS},
		{MHz(144),MHz(146),IC746_ALL_RX_MODES,-1,-1,IC746_VFO_ALL,IC746_ANTS},
		RIG_FRNG_END, },
.tx_range_list1 =  {
	FRQ_RNG_HF(1,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_6m(1,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_2m(1,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_HF(1,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	FRQ_RNG_6m(1,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	FRQ_RNG_2m(1,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	RIG_FRNG_END,
	},

/* most it2 rigs have 108-174 coverage*/
.rx_range_list2 =   {
		{kHz(30),MHz(60),IC746_ALL_RX_MODES,-1,-1,IC746_VFO_ALL,IC746_ANTS},
		{MHz(108),MHz(174),IC746_ALL_RX_MODES,-1,-1,IC746_VFO_ALL,IC746_ANTS},
		RIG_FRNG_END, },
.tx_range_list2 =  {
	FRQ_RNG_HF(2,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_6m(2,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_2m(2,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_HF(2,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	FRQ_RNG_6m(2,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	FRQ_RNG_2m(2,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	RIG_FRNG_END,
	},

.tuning_steps = 	{
	 {IC746_1HZ_TS_MODES,1},
	 {IC746_ALL_RX_MODES,100},
	 {IC746_ALL_RX_MODES,kHz(1)},
	 {IC746_ALL_RX_MODES,kHz(5)},
	 {IC746_ALL_RX_MODES,kHz(9)},
	 {IC746_ALL_RX_MODES,kHz(10)},
	 {IC746_ALL_RX_MODES,kHz(12.5)},
	 {IC746_ALL_RX_MODES,kHz(20)},
	 {IC746_ALL_RX_MODES,kHz(25)},
	 RIG_TS_END,
	},
	/* mode/filter list, remember: order matters! */
.filters = 	{
		{RIG_MODE_SSB|RIG_MODE_RTTYR|RIG_MODE_CWR|RIG_MODE_RTTY|RIG_MODE_CW, kHz(2.1)},
		{RIG_MODE_CW|RIG_MODE_CWR, Hz(500)},
		{RIG_MODE_FM, kHz(12)},
		{RIG_MODE_FM|RIG_MODE_AM, kHz(9)},
		RIG_FLT_END,
	},
.str_cal = IC746_STR_CAL,

.cfgparams =  icom_cfg_params,
.set_conf =  icom_set_conf,
.get_conf =  icom_get_conf,

.priv =  (void*)&ic746_priv_caps,
.rig_init =   icom_init,
.rig_cleanup =   icom_cleanup,
.rig_open =  NULL,
.rig_close =  NULL,

.set_freq =  icom_set_freq,
.get_freq =  icom_get_freq,
.set_mode =  icom_set_mode,
.get_mode =  icom_get_mode,
.set_vfo =  icom_set_vfo,
.set_ant =  icom_set_ant,
.get_ant =  icom_get_ant,

.decode_event =  icom_decode_event,
.set_level =  icom_set_level,
.get_level =  icom_get_level,
.set_func =  icom_set_func,
.get_func =  icom_get_func,
.set_parm = ic746_set_parm,
.set_mem =  icom_set_mem,
.vfo_op =  icom_vfo_op,
.scan =  icom_scan,
.get_dcd =  icom_get_dcd,
.set_ts =  icom_set_ts,
.get_ts =  icom_get_ts,
.set_rptr_shift =  icom_set_rptr_shift,
.get_rptr_shift =  icom_get_rptr_shift,
.set_rptr_offs =  icom_set_rptr_offs,
.get_rptr_offs =  icom_get_rptr_offs,
.set_split_freq =  icom_set_split_freq,
.get_split_freq =  icom_get_split_freq,
.set_split_mode =  icom_set_split_mode,
.get_split_mode =  icom_get_split_mode,
.set_split_vfo =  icom_set_split_vfo,
.get_split_vfo =  icom_get_split_vfo,

};


/*
 * ic746pro rig capabilities.
 */
static const struct icom_priv_caps ic746pro_priv_caps = { 
		0x66,	/* default address */
		0,		/* 731 mode */
		ic756pro_ts_sc_list
};

const struct rig_caps ic746pro_caps = {
.rig_model =  RIG_MODEL_IC746PRO,
.model_name = "IC-746PRO", 
.mfg_name =  "Icom", 
.version =  BACKEND_VER,
.copyright =  "LGPL",
.status =  RIG_STATUS_NEW,
.rig_type =  RIG_TYPE_TRANSCEIVER,
.ptt_type =  RIG_PTT_RIG,
.dcd_type =  RIG_DCD_RIG,
.port_type =  RIG_PORT_SERIAL,
.serial_rate_min =  300,
.serial_rate_max =  19200,
.serial_data_bits =  8,
.serial_stop_bits =  1,
.serial_parity =  RIG_PARITY_NONE,
.serial_handshake =  RIG_HANDSHAKE_NONE, 
.write_delay =  0,
.post_write_delay =  0,
.timeout =  200,
.retry =  3, 
.has_get_func =  IC746_FUNC_ALL,
.has_set_func =  IC746_FUNC_ALL, 
.has_get_level =  IC746_LEVEL_ALL,
.has_set_level =  RIG_LEVEL_SET(IC746_LEVEL_ALL),
.has_get_parm =  IC746_GET_PARM,
.has_set_parm =  IC746_SET_PARM,
.level_gran = {
	[LVL_RAWSTR] = { .min = { .i = 0 }, .max = { .i = 255 } },
	},
.parm_gran =  {},
.ctcss_list =  common_ctcss_list,
.dcs_list =  full_dcs_list,
.preamp =   { 10, 20, RIG_DBLST_END, },	/* FIXME: TBC */
.attenuator =   { 20, RIG_DBLST_END, },
.max_rit =  Hz(0),
.max_xit =  Hz(0),
.max_ifshift =  Hz(0),
.targetable_vfo =  0,
.vfo_ops =  IC746_VFO_OPS,
.scan_ops =  IC746_SCAN_OPS,
.transceive =  RIG_TRN_RIG,
.bank_qty =   0,
.chan_desc_sz =  9,

.chan_list =  {
			   {   1,  99, RIG_MTYPE_MEM, IC746PRO_MEM_CAP  },
			   { 100, 101, RIG_MTYPE_EDGE, IC746PRO_MEM_CAP },    /* two by two */
			   { 102, 102, RIG_MTYPE_CALL, IC746PRO_MEM_CAP },
			   RIG_CHAN_END,
		},

.rx_range_list1 =   {
		{kHz(30),MHz(60),IC746_ALL_RX_MODES,-1,-1,IC746_VFO_ALL,IC746_ANTS},
		{MHz(144),MHz(146),IC746_ALL_RX_MODES,-1,-1,IC746_VFO_ALL,IC746_ANTS},
		RIG_FRNG_END, },
.tx_range_list1 =  {
	FRQ_RNG_HF(1,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_6m(1,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_2m(1,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_HF(1,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	FRQ_RNG_6m(1,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	FRQ_RNG_2m(1,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	RIG_FRNG_END,
	},

/* most it2 rigs have 108-174 coverage*/
.rx_range_list2 =   {
		{kHz(30),MHz(60),IC746_ALL_RX_MODES,-1,-1,IC746_VFO_ALL,IC746_ANTS},
		{MHz(108),MHz(174),IC746_ALL_RX_MODES,-1,-1,IC746_VFO_ALL,IC746_ANTS},
		RIG_FRNG_END, },
.tx_range_list2 =  {
	FRQ_RNG_HF(2,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_6m(2,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_2m(2,IC746_OTHER_TX_MODES, W(5),W(100),IC746_VFO_ALL,IC746_ANTS),
	FRQ_RNG_HF(2,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	FRQ_RNG_6m(2,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	FRQ_RNG_2m(2,IC746_AM_TX_MODES, W(2),W(40),IC746_VFO_ALL,IC746_ANTS),   /* AM class */
	RIG_FRNG_END,
	},

.tuning_steps = 	{
	 {IC746_1HZ_TS_MODES,1},
	 {IC746_ALL_RX_MODES,100},
	 {IC746_ALL_RX_MODES,kHz(1)},
	 {IC746_ALL_RX_MODES,kHz(5)},
	 {IC746_ALL_RX_MODES,kHz(9)},
	 {IC746_ALL_RX_MODES,kHz(10)},
	 {IC746_ALL_RX_MODES,kHz(12.5)},
	 {IC746_ALL_RX_MODES,kHz(20)},
	 {IC746_ALL_RX_MODES,kHz(25)},
	 RIG_TS_END,
	},

	/* mode/filter list, remember: order matters! But duplication may speed up search.  Put the most commonly used modes first! It might be better to rewrite and just put all filter widths for 1 mode together in 1 record.  Remember these are defaults, with dsp rigs you can change them to anything you want (except rtty filter modes). */
.filters = 	{
		{RIG_MODE_SSB, kHz(2.4)},
		{RIG_MODE_SSB, kHz(1.8)},
		{RIG_MODE_SSB, kHz(3)},
		{RIG_MODE_FM, kHz(10)},
		{RIG_MODE_FM, kHz(15)},
		{RIG_MODE_FM, kHz(7)},

 /* There are 5 rtty filters when rtty filter mode is set (default condition) { 1k, 500, 350, 300, 250 }. These are fixed. If rtty filter mode is unset there are 3 general IF filters { 2.4k, 500, 250 are the defaults }.  These can be changed. There is a "twin-peak" filter mode as well.  It boosts the 2125 and 2295 recieve frequency reponse.    I'm not sure what the icom_defs S_FUNC_RNF (rtty notch filter) is supposed to refer to, it has no notch function, but, the commands turns the rtty filter mode on and off.  Changed to S_FUNC_RF */

		{RIG_MODE_CW|RIG_MODE_CWR|RIG_MODE_RTTY|RIG_MODE_RTTYR, Hz(500)}, /* RTTY &  "normal" IF Filters */
		{RIG_MODE_CW|RIG_MODE_CWR|RIG_MODE_RTTY|RIG_MODE_RTTYR, Hz(250)}, /* RTTY & "narrow" IF Filters */
		{RIG_MODE_CW|RIG_MODE_CWR, kHz(2.4)}, /* "wide" IF filter */
		{RIG_MODE_RTTY|RIG_MODE_RTTYR, kHz(1)}, /*RTTY mode Filter*/
		{RIG_MODE_RTTY|RIG_MODE_RTTYR, Hz(350)}, /*"Default " rtty mode filter*/
		{RIG_MODE_RTTY|RIG_MODE_RTTYR, Hz(300)}, /* RTTY mode Filter */
		{RIG_MODE_AM, kHz(6)},
		{RIG_MODE_AM, kHz(3)},
		{RIG_MODE_AM, kHz(9)},
		RIG_FLT_END,
	},
.str_cal = IC746_STR_CAL,

.cfgparams =  icom_cfg_params,
.set_conf =  icom_set_conf,
.get_conf =  icom_get_conf,

.priv =  (void*)&ic746pro_priv_caps,
.rig_init =   icom_init,
.rig_cleanup =   icom_cleanup,
.rig_open =  NULL,
.rig_close =  NULL,

.set_freq =  icom_set_freq,
.get_freq =  icom_get_freq,
.set_mode =  icom_set_mode,
.get_mode =  icom_get_mode,
.set_vfo =  icom_set_vfo,
.set_ant =  icom_set_ant,
.get_ant =  icom_get_ant,

.decode_event =  icom_decode_event,
.set_level =  icom_set_level,
.get_level =  icom_get_level,
.set_func =  icom_set_func,
.get_func =  icom_get_func,
.set_parm = ic746_set_parm,
.get_parm = ic746_get_parm,
.set_mem =  icom_set_mem,
.vfo_op =  icom_vfo_op,
.scan =  icom_scan,
.set_ptt =  icom_set_ptt,
.get_ptt =  icom_get_ptt,
.get_dcd =  icom_get_dcd,
.set_ts =  icom_set_ts,
.get_ts =  NULL,
.set_rptr_shift =  icom_set_rptr_shift,
.get_rptr_shift =  NULL,
.set_rptr_offs =  icom_set_rptr_offs,
.get_rptr_offs =  icom_get_rptr_offs,
.set_ctcss_tone =  icom_set_ctcss_tone,
.get_ctcss_tone =  icom_get_ctcss_tone,
.set_ctcss_sql =  icom_set_ctcss_sql,
.get_ctcss_sql =  icom_get_ctcss_sql,
.set_split_freq =  icom_set_split_freq,
.get_split_freq =  icom_get_split_freq,
.set_split_mode =  icom_set_split_mode,
.get_split_mode =  icom_get_split_mode,
.set_split_vfo =  icom_set_split_vfo,
.get_split_vfo =  NULL,
.get_channel = ic746pro_get_channel,
};


/*
 * icom_set_parm
 * Assumes rig!=NULL
 * These are very much rig specific and should probably be in rig files.  These are for IC-746Pro.
 *  The 746 has no parameters.
 */
int ic746_set_parm(RIG *rig, setting_t parm, value_t val)
{
	unsigned char prmbuf[MAXFRAMELEN], ackbuf[MAXFRAMELEN];
	int ack_len, prm_len;
	int prm_cn, prm_sc;
	int retval, icom_val;

	prm_cn = C_CTL_MEM;	/* Most parm are 0x05xx */
	prm_sc = S_MEM_PARM;

	switch (parm) {
	case RIG_PARM_ANN:
		if ((val.i == RIG_ANN_FREQ) || (val.i == RIG_ANN_RXMODE)) {
			prm_cn = C_CTL_ANN;
			prm_sc = val.i;
			prm_len = 0;
		}
		else {
			if ((val.i == RIG_ANN_ENG)||(val.i == RIG_ANN_JAP)) {
				prm_cn = C_CTL_MEM;
				prm_sc = S_MEM_LANG >> 8;
				prmbuf[0] = S_MEM_LANG & 0xff;
				prm_len = 2;
				prmbuf[1] = (val.i == RIG_ANN_ENG ? 0 : 1);
			}
			else {
				rig_debug(RIG_DEBUG_ERR,"Unsupported set_parm_ann %d\n", val.i);
				return -RIG_EINVAL;
			}
		}
		break;
	case RIG_PARM_BACKLIGHT:
		prmbuf[0] = S_MEM_BKLIT;
		prm_len = 3;
		icom_val =  val.f * 255 ;
		to_bcd_be(prmbuf + 1, (long long)icom_val, 4);
		break;
	case RIG_PARM_BEEP:
		prmbuf[0] = S_MEM_BEEP;
		prm_len = 2;
		prmbuf[1] = val.i;
		break;
	default:
	  rig_debug(RIG_DEBUG_ERR,"Unsupported set_parm %d\n", parm);
	  return -RIG_EINVAL;
	}

	retval = icom_transaction(rig, prm_cn, prm_sc, prmbuf, prm_len,
					ackbuf, &ack_len);
	if (retval != RIG_OK)
			return retval;

	if (ack_len != 1) {
		rig_debug(RIG_DEBUG_ERR,"icom_set_parm: wrong frame len=%d\n",
					ack_len);
		return -RIG_EPROTO;
	}

	return RIG_OK;
}

/*
 * icom_get_parm
 * Assumes rig!=NULL
 */
int ic746_get_parm(RIG *rig, setting_t parm, value_t *val)
{
	unsigned char resbuf[MAXFRAMELEN], data;
	int res_len, icom_val;
	int prm_cn, prm_sc;
	int cmdhead;
	int retval;

	prm_cn = C_CTL_MEM;
	prm_sc = S_MEM_PARM;

	switch (parm) {
	case RIG_PARM_BACKLIGHT:
		data = S_MEM_BKLIT;
		break;
	case RIG_PARM_BEEP:
		data = S_MEM_BEEP;
		break;
	default:
		rig_debug(RIG_DEBUG_ERR,"Unsupported get_parm %d", parm);
		return -RIG_EINVAL;
	}

	retval = icom_transaction (rig, prm_cn, prm_sc, &data, 1,
					resbuf, &res_len);
	if (retval != RIG_OK)
		return retval;

	/*
	 * strbuf should contain Cn,Sc,Data area
	 */
	cmdhead = (prm_sc == -1) ? 1:3;
	res_len -= cmdhead;
/* should echo cmd, subcmd and then data, if you get an ack something is wrong */
	if (resbuf[0] != prm_cn) {
		if (resbuf[0] == ACK) {
			rig_debug(RIG_DEBUG_ERR,"%s: protocol error (%#.2x), "
				"len=%d\n", __FUNCTION__,resbuf[0],res_len);
		return -RIG_EPROTO;
		}
		else {
			rig_debug(RIG_DEBUG_ERR,"%s: ack NG (%#.2x), "
				"len=%d\n", __FUNCTION__,resbuf[0],res_len);
		return -RIG_ERJCTED;
		}
	}

	icom_val = from_bcd_be(resbuf+cmdhead, res_len*2);	/* is this method necessary? Why not just use unsigned char directly on the buf ? */
	if (RIG_PARM_IS_FLOAT(parm))
		val->f = (float)icom_val/255;
	else
		val->i = icom_val;

	rig_debug(RIG_DEBUG_TRACE,"%s: %d %d %d %f\n",
			__FUNCTION__, res_len, icom_val, val->i, val->f);

	return RIG_OK;
}

/*
 * ic746pro_get_channel
 * Assumes rig!=NULL, rig->state.priv!=NULL, chan!=NULL
 * 
 * If memory is empty it will return RIG_OK,but every thing will be null. Where do we boundary check?
 */
int ic746pro_get_channel(RIG *rig, channel_t *chan)
{
		struct icom_priv_data *priv;
		struct rig_state *rs;
		unsigned char chanbuf[46], databuf[32], data;
		mem_buf_t *membuf;
		int chan_len, freq_len, retval, data_len, sc, band;

		rs = &rig->state;
		priv = (struct icom_priv_data*)rs->priv;

		to_bcd_be(chanbuf,chan->channel_num,4);
		chan_len = 2;

		freq_len = priv->civ_731_mode ? 4:5;

		retval = icom_transaction (rig, C_CTL_MEM, S_MEM_CNTNT,
						chanbuf, chan_len, chanbuf, &chan_len);
		if (retval != RIG_OK)
				return retval;

		chan->vfo = RIG_VFO_MEM;
		chan->ant = RIG_ANT_NONE;
		chan->freq = 0;
		chan->mode = RIG_MODE_NONE;
		chan->width = RIG_PASSBAND_NORMAL;
		chan->rptr_shift = RIG_RPT_SHIFT_NONE;
		chan->rptr_offs = 0;
		chan->tuning_step = 0;
		chan->tx_freq = 0;
		chan->tx_mode = RIG_MODE_NONE;
		chan->tx_width = RIG_PASSBAND_NORMAL;
		chan->tx_vfo = RIG_VFO_NONE;
		chan->rit = 0;
		chan->xit = 0;
		chan->funcs = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_PREAMP)].i = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_ATT)].i = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_AF)].f = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_RF)].f = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_SQL)].f = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_NR)].f = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_PBT_IN)].f = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_PBT_OUT)].f = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_CWPITCH)].i = 0;
		chan->levels[rig_setting2idx(RIG_LEVEL_AGC)].i = RIG_AGC_OFF;
		chan->ctcss_tone = 0;
		chan->ctcss_sql = 0;
		chan->dcs_code = 0;
		chan->dcs_sql = 0;
		chan->scan_group = 0;
		chan->flags = RIG_CHFLAG_SKIP;
		strcpy(chan->channel_desc, "         ");
		
		/*
		 * freqbuf should contain Cn,Sc,Data area
		 */
		if ((chan_len != freq_len*2+40) && (chan_len != 1)) {
				rig_debug(RIG_DEBUG_ERR,"ic746pro_get_channel: wrong frame len=%d\n",
								chan_len);
				return -RIG_ERJCTED;
		}
		
		/* do this only if not a blank channel */
		if (chan_len != 1) {

			membuf = (mem_buf_t *) (chanbuf+4);

			chan->flags = membuf->chan_flag && 0x01 ? RIG_CHFLAG_SKIP : RIG_CHFLAG_NONE;

			/* data mode on */
			if (membuf->rx.data) chan->flags |= RIG_CHFLAG_DATA;
			/*
			 * from_bcd requires nibble len
			 */
			chan->freq = from_bcd(membuf->rx.freq, freq_len*2);

			icom2rig_mode(rig, membuf->rx.mode, membuf->rx.pb,
							&chan->mode, &chan->width);

			chan->rptr_shift = (rptr_shift_t) (membuf->rx.dup >> 8);

			/* offset is default for the band & is not stored in channel memory.
		 	  The following retrieves the system default for the band */
			band = (int) chan->freq / 1000000;  /* hf, 2m or 6 m */
			sc = S_MEM_PARM;
			if (band < 50 ) data = S_MEM_HF_DUP_OFST;
			else if (band < 108) data = S_MEM_6M_DUP_OFST;
			else data = S_MEM_2M_DUP_OFST;
			retval = icom_transaction (rig, C_CTL_MEM, sc,
						&data, 1, databuf, &data_len);
			if (retval != RIG_OK)
				return retval;
			chan->rptr_offs = from_bcd(databuf + 3, 6) * 100;

			chan->ctcss_tone = from_bcd_be(membuf->rx.tone, 6);
			chan->ctcss_sql = from_bcd_be(membuf->rx.tone_sql, 6);
			chan->dcs_code = from_bcd_be(membuf->rx.dcs.code, 4);
			/* The dcs information include in the channel includes polarity information 
			for both tx and recieve.  Both directions are enabled when in dcs mode */

			chan->tx_freq = from_bcd(membuf->tx.freq, freq_len*2);
			icom2rig_mode(rig, membuf->tx.mode, membuf->tx.pb,
							&chan->tx_mode, &chan->tx_width);
			strncpy(chan->channel_desc, membuf->name, 9);
			chan->channel_desc[9] = '\0';	/* add null terminator */
		}

		return RIG_OK;
}
