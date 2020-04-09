/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sensor_cfg.h"
#include "sensor_drv_u.h"
/**---------------------------------------------------------------------------*
 ** 						   Macro Define
 **---------------------------------------------------------------------------*/
#define HI702_I2C_ADDR_W	(0x60>>1)
#define HI702_I2C_ADDR_R	(0x61>>1)

#define SENSOR_GAIN_SCALE		16 
/**---------------------------------------------------------------------------*
 ** 					Local Function Prototypes							  *
 **---------------------------------------------------------------------------*/
LOCAL uint32_t set_hi702_ae_enable(uint32_t enable);
LOCAL uint32_t set_hmirror_enable(uint32_t enable);
LOCAL uint32_t set_vmirror_enable(uint32_t enable);
LOCAL uint32_t set_preview_mode(uint32_t preview_mode);
LOCAL uint32_t _hi702_PowerOn(uint32_t power_on);
LOCAL uint32_t HI702_Identify(uint32_t param);
/*
LOCAL uint32_t HI702_BeforeSnapshot(uint32_t param);
LOCAL uint32_t HI702_After_Snapshot(uint32_t param);
*/
LOCAL uint32_t set_brightness(uint32_t level);
LOCAL uint32_t set_contrast(uint32_t level);
LOCAL uint32_t set_sharpness(uint32_t level);
LOCAL uint32_t set_saturation(uint32_t level);
LOCAL uint32_t set_image_effect(uint32_t effect_type);
LOCAL uint32_t read_ev_value(uint32_t value);
LOCAL uint32_t write_ev_value(uint32_t exposure_value);
LOCAL uint32_t read_gain_value(uint32_t value);
LOCAL uint32_t write_gain_value(uint32_t gain_value);
LOCAL uint32_t read_gain_scale(uint32_t value);
LOCAL uint32_t set_frame_rate(uint32_t param);
LOCAL uint32_t set_hi702_ev(uint32_t level);
LOCAL uint32_t set_hi702_awb(uint32_t mode);
LOCAL uint32_t set_hi702_anti_flicker(uint32_t mode);
LOCAL uint32_t set_hi702_video_mode(uint32_t mode);


LOCAL uint32_t HI702_After_Snapshot(uint32_t param);
LOCAL uint32_t HI702_BeforeSnapshot(uint32_t param);

// Additional functions
LOCAL uint32_t __HI702_WriteGroupRegs(SENSOR_REG_T* sensor_reg_ptr, char *name, int size);
LOCAL uint32_t _HI702_WriteGroupRegs(SENSOR_REG_T* sensor_reg_ptr, char *name);

/**---------------------------------------------------------------------------*
 ** 						Local Variables 								 *
 **---------------------------------------------------------------------------*/
 typedef enum
{
        FLICKER_50HZ = 0,
        FLICKER_60HZ,
        FLICKER_MAX
}FLICKER_E;



LOCAL SENSOR_REG_T HI702_image_auto_fps[] = {
	{0x0003, 0x0000},
	{0x0001, 0x00F1},
	{0x0003, 0x0000},
	{0x0011, 0x0090},
	{0x0042, 0x0001},
	{0x0043, 0x0050},
	{0x0090, 0x000C},
	{0x0091, 0x000C},
	{0x0092, 0x0078},
	{0x0093, 0x0070},
	{0x0003, 0x0020},
	{0x0010, 0x001C},
	{0x002A, 0x00F0},
	{0x002B, 0x00F4},
	{0x0030, 0x00F8},
	{0x0088, 0x0002},
	{0x0089, 0x00BF},
	{0x008A, 0x0020},
	{0x00B2, 0x0080},
	{0x0003, 0x0020},
	{0x0010, 0x009C},
	{0x0003, 0x0000},
	{0x0001, 0x00F0},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_wb_Auto[] = {
	{0x0003, 0x0022},
	{0x0010, 0x007B},
	{0x0011, 0x002E},
	{0x0080, 0x0039},
	{0x0081, 0x0020},
	{0x0082, 0x0035},
	{0x0083, 0x0054},
	{0x0084, 0x0020},
	{0x0085, 0x0052},
	{0x0086, 0x0020},
	{0x0010, 0x00FB},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_wb_Incandescence[] = {
	{0x0003, 0x0022},
	{0x0010, 0x007B},
	{0x0011, 0x0026},
	{0x0080, 0x0020},
	{0x0081, 0x0020},
	{0x0082, 0x0057},
	{0x0083, 0x0021},
	{0x0084, 0x001D},
	{0x0085, 0x0059},
	{0x0086, 0x0056},
	{0x0010, 0x00FB},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_wb_Fluorescent[] = {
	{0x0003, 0x0022},
	{0x0010, 0x007B},
	{0x0011, 0x0026},
	{0x0080, 0x0042},
	{0x0081, 0x0020},
	{0x0082, 0x0051},
	{0x0083, 0x004A},
	{0x0084, 0x003A},
	{0x0085, 0x0055},
	{0x0086, 0x0045},
	{0x0010, 0x00FB},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_wb_Sun[] = {
	{0x0003, 0x0022},
	{0x0010, 0x007B},
	{0x0011, 0x0026},
	{0x0080, 0x0030},
	{0x0081, 0x0020},
	{0x0082, 0x0036},
	{0x0083, 0x0031},
	{0x0084, 0x002F},
	{0x0085, 0x0037},
	{0x0086, 0x0035},
	{0x0010, 0x00FB},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_wb_Cloud[] = {
	{0x0003, 0x0022},
	{0x0010, 0x007B},
	{0x0011, 0x0026},
	{0x0080, 0x004F},
	{0x0081, 0x0020},
	{0x0082, 0x0025},
	{0x0083, 0x0053},
	{0x0084, 0x0048},
	{0x0085, 0x0035},
	{0x0086, 0x002B},
	{0x0010, 0x00FB},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_image_effect_normal[] = {
	{0x0003, 0x0010},
	{0x0011, 0x0003},
	{0x0012, 0x0030},
	{0x0003, 0x0013},
	{0x0010, 0x003B},
	{0x0020, 0x0002},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_image_effect_blackwhite[] = {
	{0x0003, 0x0010},
	{0x0011, 0x0003},
	{0x0012, 0x0033},
	{0x0044, 0x0080},
	{0x0045, 0x0080},
	{0x0003, 0x0013},
	{0x0010, 0x003B},
	{0x0020, 0x0002},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_image_effect_negative[] = {
	{0x0003, 0x0010},
	{0x0011, 0x0003},
	{0x0012, 0x0038},
	{0x0003, 0x0013},
	{0x0010, 0x003B},
	{0x0020, 0x0002},
	{0xFFFF, 0xFFFF},
};

LOCAL SENSOR_REG_T HI702_image_effect_sepia[] = {
	{0x0003, 0x0010},
	{0x0011, 0x0003},
	{0x0012, 0x0033},
	{0x0044, 0x0070},
	{0x0045, 0x0098},
	{0x0003, 0x0013},
	{0x0010, 0x003B},
	{0x0020, 0x0002},
	{0xFFFF, 0xFFFF},
};

// Only uses 1 Byte addresses and values
SENSOR_REG_T hi702_YUV_640X480[] = {
	{0x0003, 0x0000},
	{0x0001, 0x00F1},
	{0x0001, 0x00F3},
	{0x0001, 0x00F1},

	{0x0003, 0x0020},//page 3
	{0x0010, 0x001C},//ae off
	{0x0003, 0x0022},//page 4
	{0x0010, 0x007B},//awb off

	{0x0003, 0x0000},
	{0x0010, 0x0000},
	{0x0011, 0x0090},
	{0x0012, 0x0004},
	{0x0020, 0x0000},
	{0x0021, 0x0006},
	{0x0022, 0x0000},
	{0x0023, 0x0006},
	{0x0024, 0x0001},
	{0x0025, 0x00E0},
	{0x0026, 0x0002},
	{0x0027, 0x0080},
	{0x0040, 0x0001},
	{0x0041, 0x0050}, // Hblank 336

	{0x0042, 0x0001}, // Vsync 336
	{0x0043, 0x0050},

	////  BLC setting
	{0x0080, 0x003E},
	{0x0081, 0x0096},
	{0x0082, 0x0090},
	{0x0083, 0x0000},
	{0x0084, 0x002C},

	{0x0090, 0x000C},
	{0x0091, 0x000C},
	{0x0092, 0x0078},
	{0x0093, 0x0070},
	{0x0094, 0x0088},//add calvin 090410
	{0x0095, 0x0080},//add calvin 090410

	{0x0098, 0x0020},
	{0x00A0, 0x0040},
	{0x00A2, 0x0041},
	{0x00A4, 0x0040},
	{0x00A6, 0x0041},
	{0x00A8, 0x0044},
	{0x00AA, 0x0043},
	{0x00AC, 0x0044},
	{0x00AE, 0x0043},

	{0x0003, 0x0002},
	{0x0010, 0x0000},
	{0x0013, 0x0000},
	{0x0018, 0x001C},
	{0x0019, 0x0000},
	{0x001A, 0x0000},
	{0x001B, 0x0008},
	{0x001C, 0x0000},
	{0x001D, 0x0000},
	{0x0020, 0x0033},
	{0x0021, 0x00AA},
	{0x0022, 0x00A6},
	{0x0023, 0x00B0},
	{0x0031, 0x0099},
	{0x0032, 0x0000},
	{0x0033, 0x0000},
	{0x0034, 0x003C},
	{0x0050, 0x0021},
	{0x0054, 0x0030},
	{0x0056, 0x00FE},
	{0x0062, 0x0078},
	{0x0063, 0x009E},
	{0x0064, 0x0078},
	{0x0065, 0x009E},
	{0x0072, 0x007A},
	{0x0073, 0x009A},
	{0x0074, 0x007A},
	{0x0075, 0x009A},

	{0x0082, 0x0009},
	{0x0084, 0x0009},
	{0x0086, 0x0009},

	{0x00A0, 0x0003},
	{0x00A8, 0x001D},
	{0x00AA, 0x0049},

	{0x00B9, 0x008A},
	{0x00BB, 0x008A},
	{0x00BC, 0x0004},
	{0x00BD, 0x0010},
	{0x00BE, 0x0004},
	{0x00BF, 0x0010},

	{0x0003, 0x0010},//page 10:image effect
	{0x0010, 0x0003},//ISPCTL1
	{0x0012, 0x0030},//Y offet, dy offset enable
	{0x0040, 0x0080},
	{0x0041, 0x0005},
	{0x0050, 0x0078},

	{0x0060, 0x001F},
	{0x0061, 0x0085},
	{0x0062, 0x0080},
	{0x0063, 0x00F0},
	{0x0064, 0x0080},

	{0x0003, 0x0011},
	{0x0010, 0x0099},
	{0x0011, 0x0008},
	{0x0021, 0x0029},
	{0x0050, 0x0005},
	{0x0060, 0x000F},
	{0x0062, 0x0043},
	{0x0063, 0x0063},
	{0x0074, 0x0008},
	{0x0075, 0x0008},

	{0x0003, 0x0012},
	{0x0040, 0x0023},
	{0x0041, 0x003B},
	{0x0050, 0x0005},
	{0x0070, 0x001D},
	{0x0074, 0x0005},
	{0x0075, 0x0004},
	{0x0077, 0x0020},
	{0x0078, 0x0010},
	{0x0091, 0x0034},
	{0x00B0, 0x00C9},
	{0x00D0, 0x00B1},

	{0x0003, 0x0013},
	{0x0010, 0x003B},
	{0x0011, 0x0003},
	{0x0012, 0x0000},
	{0x0013, 0x0002},
	{0x0014, 0x0000},
	{0x0020, 0x0002},
	{0x0021, 0x0001},
	{0x0023, 0x0024},
	{0x0024, 0x0006},
	{0x0025, 0x0002},
	{0x0028, 0x0000},
	{0x0029, 0x00F0},
	{0x0030, 0x00FF},

	{0x0080, 0x000D},
	{0x0081, 0x0013},


	{0x0083, 0x005D},

	{0x0090, 0x0003},
	{0x0091, 0x0002},
	{0x0093, 0x003D},
	{0x0094, 0x0003},
	{0x0095, 0x008F},

	{0x0003, 0x0014},
	{0x0010, 0x0001},
	{0x0020, 0x0080},
	{0x0021, 0x0078},
	{0x0022, 0x0051},
	{0x0023, 0x0040},
	{0x0024, 0x003E},

	//  CMC
	{0x0003, 0x0015},
	{0x0010, 0x0003},
	{0x0014, 0x003C},
	{0x0016, 0x002C},
	{0x0017, 0x002F},


	{0x0030, 0x00CB},
	{0x0031, 0x0061},
	{0x0032, 0x0016},
	{0x0033, 0x0023},
	{0x0034, 0x00CE},
	{0x0035, 0x002B},
	{0x0036, 0x0001},
	{0x0037, 0x0034},
	{0x0038, 0x0075},

	{0x0040, 0x0087},
	{0x0041, 0x0018},
	{0x0042, 0x0091},
	{0x0043, 0x0094},
	{0x0044, 0x009F},
	{0x0045, 0x0033},
	{0x0046, 0x0000},
	{0x0047, 0x0094},
	{0x0048, 0x0014},

	{0x0003, 0x0016},
	{0x0010, 0x0001},
	{0x0030, 0x0000},
	{0x0031, 0x0009},
	{0x0032, 0x001B},
	{0x0033, 0x0035},
	{0x0034, 0x005D},
	{0x0035, 0x007A},
	{0x0036, 0x0093},
	{0x0037, 0x00A7},
	{0x0038, 0x00B8},
	{0x0039, 0x00C6},
	{0x003A, 0x00D2},
	{0x003B, 0x00E4},
	{0x003C, 0x00F1},
	{0x003D, 0x00F9},
	{0x003E, 0x00FF},

	//PAGE 20
	{0x0003, 0x0020},
	{0x0010, 0x001C},//100Hz
	{0x0011, 0x0004},

	{0x0020, 0x0001},
	{0x0028, 0x003F},
	{0x0029, 0x00A3},
	{0x002A, 0x00F0},
	{0x002B, 0x00F4},//?/??? Anti banding

	{0x0030, 0x00F8},//0xf8->0x78 1/120 Anti banding

	{0x0060, 0x00D5},
	{0x0068, 0x0034},
	{0x0069, 0x006E},
	{0x006A, 0x0028},
	{0x006B, 0x00C8},

	{0x0070, 0x0034},

	{0x0078, 0x0012},//yth1
	{0x0079, 0x0011},//yth2
	{0x007A, 0x0023},//yth3

	{0x0083, 0x0000},//EXP Normal 20.00 fps
	{0x0084, 0x00AF},
	{0x0085, 0x00C8},
	{0x0086, 0x0000},//EXPMin 6000.00 fps
	{0x0087, 0x00FA},
	{0x0088, 0x0002},//EXP Max 25.00 fps
	{0x0089, 0x00BF},
	{0x008A, 0x0020},
	{0x008B, 0x003A},//EXP100
	{0x008C, 0x0098},
	{0x008D, 0x0030},//EXP120
	{0x008E, 0x00D4},
	{0x008F, 0x0004},//EXP140 ?
	{0x0090, 0x0093},
	{0x0091, 0x0002},//EXP Fix 17.02 fps
	{0x0092, 0x00D2},
	{0x0093, 0x00A8},

	{0x0098, 0x008C},//outdoor th1
	{0x0099, 0x0023},//outdoor th2

	{0x009C, 0x0008},//EXP Limit 857.14 fps
	{0x009D, 0x00CA},
	{0x009E, 0x0000},//EXP Unit
	{0x009F, 0x00FA},

	{0x00B0, 0x0014},
	{0x00B1, 0x0014},
	{0x00B2, 0x0080},
	{0x00B3, 0x0014},
	{0x00B4, 0x001C},
	{0x00B5, 0x0048},
	{0x00B6, 0x0032},
	{0x00B7, 0x002B},
	{0x00B8, 0x0027},
	{0x00B9, 0x0025},
	{0x00BA, 0x0023},
	{0x00BB, 0x0022},
	{0x00BC, 0x0022},
	{0x00BD, 0x0021},

	{0x00C0, 0x0014},//skygain

	{0x00C8, 0x0070},
	{0x00C9, 0x0080},

	//Page 22

	{0x0003, 0x0022},
	{0x0010, 0x00E2},
	{0x0011, 0x0026},
	{0x0021, 0x0004},

	{0x0030, 0x0080},
	{0x0031, 0x0080},
	{0x0038, 0x0011},
	{0x0039, 0x0033},

	{0x0040, 0x00F0},
	{0x0041, 0x0033},
	{0x0042, 0x0033},
	{0x0043, 0x00F3},
	{0x0044, 0x0055},
	{0x0045, 0x0044},
	{0x0046, 0x0002},

	{0x0050, 0x00D0},
	{0x0051, 0x00A0},
	{0x0052, 0x00AA},

	{0x0080, 0x0040},
	{0x0081, 0x0020},
	{0x0082, 0x0038},
	{0x0083, 0x0054},
	{0x0084, 0x0023},
	{0x0085, 0x0056},
	{0x0086, 0x0020},

	{0x0087, 0x0042},
	{0x0088, 0x003A},
	{0x0089, 0x0034},
	{0x008A, 0x002E},

	{0x008B, 0x0001},
	{0x008D, 0x0022},
	{0x008E, 0x0071},

	{0x008F, 0x005C},
	{0x0090, 0x0059},
	{0x0091, 0x0056},
	{0x0092, 0x0052},
	{0x0093, 0x0046},
	{0x0094, 0x003F},
	{0x0095, 0x0037},
	{0x0096, 0x0033},
	{0x0097, 0x002C},
	{0x0098, 0x0023},
	{0x0099, 0x001C},
	{0x009A, 0x0017},
	{0x009B, 0x0009},

	{0x0003, 0x0022},
	{0x0010, 0x00FB},

	{0x0003, 0x0020},
	{0x0010, 0x009C},//50Hz

	{0x0001, 0x00F0},

	{0x00FF, 0x0014},
	{0x00FF, 0x00FF},
};

LOCAL SENSOR_REG_TAB_INFO_T s_HI702_resolution_Tab_YUV[]=
{
        // COMMON INIT
        {ADDR_AND_LEN_OF_ARRAY(hi702_YUV_640X480), 640, 480, 24, SENSOR_IMAGE_FORMAT_YUV422},

        // YUV422 PREVIEW 1	
        {PNULL, 0, 640, 480,24, SENSOR_IMAGE_FORMAT_YUV422},
        {PNULL, 0, 0, 0, 0, 0},
        {PNULL, 0, 0, 0, 0, 0},
        {PNULL, 0, 0, 0, 0, 0},

        // YUV422 PREVIEW 2 
        {PNULL, 0, 0, 0, 0, 0},
        {PNULL, 0, 0, 0, 0, 0},
        {PNULL, 0, 0, 0, 0, 0},
        {PNULL, 0, 0, 0, 0, 0}
};

LOCAL SENSOR_IOCTL_FUNC_TAB_T s_HI702_ioctl_func_tab = 
{
        // Internal 
        PNULL,
	 _hi702_PowerOn,
        PNULL,
        HI702_Identify,

        PNULL,			// write register
        PNULL,			// read  register	
        PNULL,
        PNULL,

        // External
        set_hi702_ae_enable,
        set_hmirror_enable,
        set_vmirror_enable,

        set_brightness,
        set_contrast,
        set_sharpness,
        set_saturation,

        set_preview_mode,	
        set_image_effect,

        HI702_BeforeSnapshot,
        HI702_After_Snapshot,

        PNULL,

        read_ev_value,
        write_ev_value,
        read_gain_value,
        write_gain_value,
        read_gain_scale,
        set_frame_rate,	
        PNULL,
        PNULL,
        set_hi702_awb,
        PNULL,
        PNULL,
        set_hi702_ev,
        PNULL,
        PNULL,
        PNULL,
        PNULL,
        PNULL,
        set_hi702_anti_flicker,
        set_hi702_video_mode,
        PNULL,
        PNULL,
        PNULL,
        PNULL,
#ifdef CONFIG_CAMERA_SENSOR_NEW_FEATURE
	PNULL,
	PNULL
#else
	PNULL,
	PNULL
#endif
};

/**---------------------------------------------------------------------------*
 ** 						Global Variables								  *
 **---------------------------------------------------------------------------*/
 SENSOR_INFO_T g_HI702_yuv_info =
{
	HI702_I2C_ADDR_W,				// salve i2c write address
	HI702_I2C_ADDR_R, 				// salve i2c read address

	SENSOR_I2C_VAL_8BIT|SENSOR_I2C_REG_8BIT|SENSOR_I2C_FREQ_400, // bit0: 0: i2c register value is 8 bit, 1: i2c register value is 16 bit
								// bit0: 0: i2c register value is 8 bit, 1: i2c register value is 16 bit
								// bit2: 0: i2c register addr  is 8 bit, 1: i2c register addr  is 16 bit
								// other bit: reseved
	SENSOR_HW_SIGNAL_PCLK_P|\
	SENSOR_HW_SIGNAL_VSYNC_N|\
	SENSOR_HW_SIGNAL_HSYNC_P,		// bit0: 0:negative; 1:positive -> polarily of pixel clock
								// bit2: 0:negative; 1:positive -> polarily of horizontal synchronization signal
								// bit4: 0:negative; 1:positive -> polarily of vertical synchronization signal
								// other bit: reseved											
										
	// preview mode
	SENSOR_ENVIROMENT_NORMAL|\
	SENSOR_ENVIROMENT_NIGHT|\
	SENSOR_ENVIROMENT_SUNNY,		

	// image effect
	SENSOR_IMAGE_EFFECT_NORMAL|\
	SENSOR_IMAGE_EFFECT_BLACKWHITE|\
	SENSOR_IMAGE_EFFECT_RED|\
	SENSOR_IMAGE_EFFECT_GREEN|\
	SENSOR_IMAGE_EFFECT_BLUE|\
	SENSOR_IMAGE_EFFECT_YELLOW|\
	SENSOR_IMAGE_EFFECT_NEGATIVE|\
	SENSOR_IMAGE_EFFECT_CANVAS,

	// while balance mode
	0,

	7,								// bit[0:7]: count of step in brightness, contrast, sharpness, saturation
								// bit[8:31] reseved

	SENSOR_LOW_PULSE_RESET,			// reset pulse level
	100,								// reset pulse width(ms)

	SENSOR_LOW_LEVEL_PWDN,			// 1: high level valid; 0: low level valid	

	2,								// count of identify code
	{{0x04, 0x8c},						// supply two code to identify sensor.
	{0x04, 0x8c}},						// for Example: index = 0-> Device id, index = 1 -> version id	
								
	SENSOR_AVDD_2800MV,				// voltage of avdd	

	640,							// max width of source image
	480,							// max height of source image
	"HI702",						// name of sensor												

	SENSOR_IMAGE_FORMAT_YUV422,		// define in SENSOR_IMAGE_FORMAT_E enum,
								// if set to SENSOR_IMAGE_FORMAT_MAX here, image format depent on SENSOR_REG_TAB_INFO_T
	SENSOR_IMAGE_PATTERN_YUV422_YUYV,	// pattern of input image form sensor;			

	s_HI702_resolution_Tab_YUV,	// point to resolution table information structure
	&s_HI702_ioctl_func_tab,		// point to ioctl function table
		
	PNULL,							// information and table about Rawrgb sensor
	PNULL,							// extend information about sensor	
	SENSOR_AVDD_1800MV,                     // iovdd
#if 1  //change dvdd value  ao.sun 20130828
	SENSOR_AVDD_1800MV, 					// dvdd
#else
	SENSOR_AVDD_1200MV,                      // dvdd
#endif	
	1,
	0,
	0,
	2,        
	0,			// threshold enable
	0,			// threshold mode
	0,			// threshold start postion
	0,			// threshold end postion
	0,
	{0, 2, 8, 1},
	PNULL,
	1
};
/**---------------------------------------------------------------------------*
 ** 							Function  Definitions
 **---------------------------------------------------------------------------*/
LOCAL void HI702_WriteReg( uint8_t  subaddr, uint8_t data )
{
#ifndef	_USE_DSP_I2C_
        Sensor_WriteReg_8bits(subaddr, data);
#else
        DSENSOR_IICWrite((uint16_t)subaddr, (uint16_t)data);
#endif

        SENSOR_TRACE("SENSOR: HI702_WriteReg reg/value(%x,%x) !!\n", subaddr, data);
}
LOCAL uint8_t HI702_ReadReg( uint8_t  subaddr)
{
        uint8_t value = 0;

#ifndef	_USE_DSP_I2C_
        value = Sensor_ReadReg( subaddr);
#else
        value = (uint16_t)DSENSOR_IICRead((uint16_t)subaddr);
#endif

        SENSOR_TRACE("SENSOR: HI702_ReadReg reg/value(%x,%x) !!\n", subaddr, value);
        return value;
}

LOCAL uint32_t __HI702_WriteGroupRegs(SENSOR_REG_T* sensor_reg_ptr, char *name, int nmem)
{
	SENSOR_REG_TAB_INFO_T infotab = {};

	if (nmem <= 0) {
		SENSOR_PRINT_ERR("Not writing an illegal member count %d", nmem);
		return -1;
	}

	if (name != NULL)
		SENSOR_PRINT_HIGH("Writing [%s] table", name);

	infotab.sensor_reg_tab_ptr = sensor_reg_ptr;
	infotab.reg_count = nmem;
	Sensor_SendRegTabToSensor(&infotab);

	return 0;

}

LOCAL uint32_t _HI702_WriteGroupRegs(SENSOR_REG_T* sensor_reg_ptr, char *name)
{
	int i = 0;
	for(; (0xFF != sensor_reg_ptr[i].reg_addr) || (0xFF != sensor_reg_ptr[i].reg_value) ; i++)
		;

	return __HI702_WriteGroupRegs(sensor_reg_ptr,name, i);
}

#define HI702_WriteGroupRegs(x) \
do { \
	__HI702_WriteGroupRegs(x, #x, (sizeof(x)/sizeof(x[0]))-1); \
} while (0)


LOCAL uint32_t _hi702_PowerOn(uint32_t power_on)
{
#ifndef CONFIG_CAMERA_IOCTL_IOCTL_HAS_POWER_ONOFF
	/*
	 * Some devices have a convenience IOCTL SENSOR_IO_POWER_ONOFF that
	 * will handle the process of turning this on and off.
	 * When the respective Flag is set, the HAL will attempt to call
	 * that IOCTL only after the poweron function has been called.
	 *
	 * If that IOCTL is present, it is more favorable to use that instead.
	 */
	SENSOR_AVDD_VAL_E dvdd_val = g_HI702_yuv_info.dvdd_val;
	SENSOR_AVDD_VAL_E avdd_val = g_HI702_yuv_info.avdd_val;
	SENSOR_AVDD_VAL_E iovdd_val = g_HI702_yuv_info.iovdd_val;
	BOOLEAN power_down = g_HI702_yuv_info.power_down_level;
	BOOLEAN reset_level = g_HI702_yuv_info.reset_pulse_level;
	SENSOR_PRINT("SENSOR_HI702: _hi702_Power_On:E!!  (1:on, 0:off): %d ------sunaodebug---- =\n", power_on);


	if (SENSOR_TRUE == power_on) {
                // Open power
                Sensor_SetVoltage(dvdd_val, avdd_val, iovdd_val); 
                SENSOR_Sleep(16);

                Sensor_PowerDown(!power_down);
		SENSOR_Sleep(2);

                Sensor_SetMCLK(SENSOR_DEFALUT_MCLK);
                SENSOR_Sleep(5);

		// Reset sensor
                Sensor_Reset(!reset_level);
                SENSOR_Sleep(60);
	} else {
		Sensor_Reset(reset_level);
		SENSOR_Sleep(2);

		Sensor_PowerDown(power_down);
		SENSOR_Sleep(2);

		Sensor_SetMCLK(SENSOR_DISABLE_MCLK);
		SENSOR_Sleep(2);

		Sensor_SetVoltage(SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED);	
		SENSOR_Sleep(5);
	}
#endif
	SENSOR_PRINT("SENSOR_HI702: _hi702_Power_On(1:on, 0:off): %d ------sunaodebug----\n", power_on);
	return SENSOR_SUCCESS;
}

LOCAL uint32_t HI702_Identify(uint32_t param)
{

        uint32_t i = 0;
        uint32_t nLoop = 0;
        uint8_t ret = 0;
        uint32_t err_cnt = 0;
        uint8_t reg[2] 	= {0x04, 0x04};
        uint8_t value[2] 	= {0x8c, 0x8c};

    SENSOR_Sleep(50);

	HI702_WriteReg(0x03, 0x00);
	HI702_WriteReg(0x01, 0xf1);
	HI702_WriteReg(0x01, 0xf3);
	HI702_WriteReg(0x01, 0xf1);

        SENSOR_TRACE("HI702_Identify-----sunao702----\n");
        for(i = 0; i<2; ) {
                nLoop = 1000;
                ret = HI702_ReadReg(reg[i]);
        	   SENSOR_TRACE("HI702 read reg0x00 = 0x%x -----sunao702----\n", ret);
                if( ret != value[i]) {
                        err_cnt++;
                        if(err_cnt>3) {
                                SENSOR_PRINT_HIGH("It is not HI702\n");
                                return SENSOR_FAIL;
                        } else {
                                while(nLoop--);
                                continue;
                        }
                }
                err_cnt = 0;
                i++;
        }

        SENSOR_TRACE("HI702_Identify: it is HI702----sunao702---\n");
        return (uint32_t)SENSOR_SUCCESS;
}

LOCAL uint32_t set_hi702_ae_enable(uint32_t enable)
{
        SENSOR_TRACE("set_hi702_ae_enable: enable = %d\n", enable);
        return 0;
}
LOCAL uint32_t set_hmirror_enable(uint32_t enable)
{
        uint8_t value;
	HI702_WriteReg(0x03, 0x00);
        value = HI702_ReadReg(0x11);
        value = (value & 0xFE) | enable;
        SENSOR_TRACE("set_hmirror_enable: enable = %d, 0x14: 0x%x.\n", enable, value);
        HI702_WriteReg(0x11, value);
        return 0;
}
LOCAL uint32_t set_vmirror_enable(uint32_t enable)
{
        uint8_t value;
	HI702_WriteReg(0x03, 0x00);
        value = HI702_ReadReg(0x11);
        value = (value & 0xFD) | ((enable & 0x1) << 1); //portrait
        SENSOR_TRACE("set_vmirror_enable: enable = %d, 0x14: 0x%x.\n", enable, value);
        HI702_WriteReg(0x11, value);
        return 0;
}
/******************************************************************************/
// Description: set brightness 
// Global resource dependence: 
// Author:
// Note:
//		level  must smaller than 8
/******************************************************************************/
SENSOR_REG_T hi702_brightness_tab[][2]=
{
        {		
        	{0xb5, 0xd0},{0xff,0xff},
        },

        {
        	{0xb5, 0xe0},{0xff,0xff},
        },

        {
        	{0xb5, 0xf0},{0xff,0xff},
        },

        {
        	{0xb5, 0x00},{0xff,0xff},
        },

        {
        	{0xb5, 0x20},{0xff,0xff},
        },

        {
        	{0xb5, 0x30},{0xff,0xff},
        },

        {
        	{0xb5, 0x40},{0xff,0xff},
        },
};

LOCAL uint32_t set_brightness(uint32_t level)
{
#if 0
        uint16_t i;
        SENSOR_REG_T* sensor_reg_ptr = (SENSOR_REG_T*)hi702_brightness_tab[level];

        if(level>6)
                return 0;
	
        for(i = 0; (0xFF != sensor_reg_ptr[i].reg_addr) && (0xFF != sensor_reg_ptr[i].reg_value); i++) {
                HI702_WriteReg(sensor_reg_ptr[i].reg_addr, sensor_reg_ptr[i].reg_value);
        }
#endif		
        SENSOR_TRACE("set_brightness: level = %d\n", level);
        return 0;
}

SENSOR_REG_T HI702_ev_tab[][3]=
{
        {{0xd3, 0x48}, {0xb5, 0xd0},{0xff, 0xff}},
        {{0xd3, 0x50}, {0xb5, 0xe0},{0xff, 0xff}},
        {{0xd3, 0x58}, {0xb5, 0xf0},{0xff, 0xff}},
        {{0xd3, 0x60}, {0xb5, 0x10},{0xff, 0xff}},
        {{0xd3, 0x68}, {0xb5, 0x20},{0xff, 0xff}},
        {{0xd3, 0x70}, {0xb5, 0x30},{0xff, 0xff}},
        {{0xd3, 0x78}, {0xb5, 0x40},{0xff, 0xff}},    
};

LOCAL uint32_t set_hi702_ev(uint32_t level)
{
#if 0
        uint16_t i; 
        SENSOR_REG_T* sensor_reg_ptr = (SENSOR_REG_T*)HI702_ev_tab[level];

        if(level>6)
                return 0;

        for(i = 0; (0xFF != sensor_reg_ptr[i].reg_addr) ||(0xFF != sensor_reg_ptr[i].reg_value) ; i++) {
                HI702_WriteReg(sensor_reg_ptr[i].reg_addr, sensor_reg_ptr[i].reg_value);
        }
#endif
        SENSOR_TRACE("SENSOR: set_ev: level = %d\n", level);
        return 0;
}

/******************************************************************************/
// Description: anti 50/60 hz banding flicker
// Global resource dependence: 
// Author:
// Note:
//		level  must smaller than 8
/******************************************************************************/
LOCAL uint32_t set_hi702_anti_flicker(uint32_t param )
{
        switch (param) {
        case FLICKER_50HZ:
                HI702_WriteReg(0x03, 0x20); 	
                HI702_WriteReg(0x10, 0x9c); 
                break;
        case FLICKER_60HZ:
                HI702_WriteReg(0x03, 0x20); 	
                HI702_WriteReg(0x10, 0x8c); 
                break;
        default:
                break;
        }
		
        return 0;
}

/******************************************************************************/
// Description: set video mode
// Global resource dependence: 
// Author:
// Note:
//		 
/******************************************************************************/
LOCAL uint32_t set_hi702_video_mode(uint32_t mode)
{
#if 0
        if(0 == mode)
                HI702_WriteReg(0xec,0x20);
        else if(1 == mode)
                HI702_WriteReg(0xec,0x00);
        SENSOR_TRACE("SENSOR: HI702_ReadReg(0xec) = %x\n", HI702_ReadReg(0xec));
#endif		
        SENSOR_TRACE("SENSOR: set_video_mode: mode = %d\n", mode);
        return 0;
}
/******************************************************************************/
// Description: set wb mode 
// Global resource dependence: 
// Author:
// Note:
//		
/******************************************************************************/
SENSOR_REG_T HI702_awb_tab[][5]=
{
        //AUTO
        {
                {0x5a, 0x4c}, {0x5b, 0x40}, {0x5c, 0x4a},
                {0x22, 0x57},    // the reg value is not written here, rewrite in set_HI702_awb();
                {0xff, 0xff}
        },	  
        //INCANDESCENCE:
        {
                {0x22, 0x55},	 // Disable AWB 
                {0x5a, 0x48},{0x5b, 0x40},{0x5c, 0x5c},
                {0xff, 0xff} 
        },
        //U30 ?
        {
                {0x41, 0x39},
                {0xca, 0x60},
                {0xcb, 0x40},
                {0xcc, 0x50},
                {0xff, 0xff}      
        },  
        //CWF ?
        {
                {0x41, 0x39},
                {0xca, 0x60},
                {0xcb, 0x40},
                {0xcc, 0x50},
                {0xff, 0xff}            
        },    
        //FLUORESCENT:
        {
                {0x22, 0x55},	// Disable AWB 
                {0x5a, 0x40},{0x5b, 0x42}, {0x5c, 0x50},
                {0xff, 0xff} 
        },
        //SUN:
        {
                {0x22, 0x55},	// Disable AWB
                {0x5a, 0x45},{0x5b, 0x3a},{0x5c, 0x40},
                {0xff, 0xff} 
        },
        //CLOUD:
        {
                {0x22, 0x55},   // Disable AWB
                {0x5a, 0x4a}, {0x5b, 0x32},{0x5c, 0x40},
                {0xff, 0xff} 
        },
};
	
LOCAL uint32_t set_hi702_awb(uint32_t mode)
{
#if 0
        uint8_t awb_en_value;
        uint16_t i;
        SENSOR_REG_T* sensor_reg_ptr = (SENSOR_REG_T*)HI702_awb_tab[mode];
        
        awb_en_value = HI702_ReadReg(0x22);	

        if(mode>6)
                return 0;

        for(i = 0; (0xFF != sensor_reg_ptr[i].reg_addr) || (0xFF != sensor_reg_ptr[i].reg_value); i++) {
                if(0x22 == sensor_reg_ptr[i].reg_addr) {
                        if(mode == 0)
                                HI702_WriteReg(0x22, awb_en_value |0x02 );
                        else
                                HI702_WriteReg(0x22, awb_en_value &0xfd );
                } else {
                        HI702_WriteReg(sensor_reg_ptr[i].reg_addr, sensor_reg_ptr[i].reg_value);
                }
        }
#endif
	switch(mode) {
		case 0:
			HI702_WriteGroupRegs(HI702_wb_Auto);
			break;
		case 1:
			HI702_WriteGroupRegs(HI702_wb_Incandescence);
			break;
		case 4:
			HI702_WriteGroupRegs(HI702_wb_Fluorescent);
			break;
		case 5:
			HI702_WriteGroupRegs(HI702_wb_Sun);
			break;
		case 6:
			HI702_WriteGroupRegs(HI702_wb_Cloud);
			break;
		default:
			break;
	}
        SENSOR_TRACE("SENSOR: set_awb_mode: mode = %d\n", mode);

        return 0;
}

SENSOR_REG_T hi702_contrast_tab[][2]=
{
        //level 0
        {
        	{0xb3,0x50},{0xff,0xff},
        },
        //level 1
        {
        	{0xb3,0x48},{0xff,0xff}, 
        },
        //level 2
        {
        	{0xb3,0x44},{0xff,0xff}, 
        },
        //level 3
        {
        	{0xb3,0x3c},{0xff,0xff},
        },
        //level 4
        {
        	{0xb3,0x3d},{0xff,0xff}, 
        },
        //level 5
        {
        	{0xb3,0x38},{0xff,0xff}, 
        },
        //level 6
        {
        	{0xb3,0x34},{0xff,0xff},
        },
};
LOCAL uint32_t set_contrast(uint32_t level)
{
#if 0
        uint16_t i;
        SENSOR_REG_T* sensor_reg_ptr;

        sensor_reg_ptr = (SENSOR_REG_T*)hi702_contrast_tab[level];

        if(level>6)
                return 0;

        for(i = 0; (0xFF != sensor_reg_ptr[i].reg_addr) && (0xFF != sensor_reg_ptr[i].reg_value); i++) {
                HI702_WriteReg(sensor_reg_ptr[i].reg_addr, sensor_reg_ptr[i].reg_value);
        }
#endif
        SENSOR_TRACE("set_contrast: level = %d\n", level);
        return 0;
}
LOCAL uint32_t set_sharpness(uint32_t level)
{
        return 0;
}
LOCAL uint32_t set_saturation(uint32_t level)
{
        return 0;
}

/******************************************************************************/
// Description: set brightness 
// Global resource dependence: 
// Author:
// Note:
//		level  must smaller than 8
/******************************************************************************/
LOCAL uint32_t set_preview_mode(uint32_t preview_mode)
{
        SENSOR_TRACE("set_preview_mode: preview_mode = %d\n", preview_mode);

        set_hi702_anti_flicker(0);
	set_frame_rate(0);
#if 0		
        switch (preview_mode) {
        case DCAMERA_ENVIRONMENT_NORMAL: 
                HI702_WriteReg(0xec,0x20);
                break;
        case DCAMERA_ENVIRONMENT_NIGHT:
                HI702_WriteReg(0xec,0x30);
                break;
        case DCAMERA_ENVIRONMENT_SUNNY:
                HI702_WriteReg(0xec,0x10);
                break;
        default:
                break;
        }
#endif		
        SENSOR_Sleep(10);
        return 0;
}

LOCAL uint32_t set_image_effect(uint32_t effect_type)
{
	switch(effect_type) {
		case 0:
			HI702_WriteGroupRegs(HI702_image_effect_normal);
			break;
		case 1:
			HI702_WriteGroupRegs(HI702_image_effect_blackwhite);
			break;
		case 6:
			HI702_WriteGroupRegs(HI702_image_effect_negative);
			break;
		case 7:
			HI702_WriteGroupRegs(HI702_image_effect_sepia);
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		default:
			break;
	}
        SENSOR_TRACE("-----------set_image_effect: effect_type = %d------------\n", effect_type);
        return 0;
}

LOCAL uint32_t HI702_After_Snapshot(uint32_t param)
{

#if 0
	Sensor_SetMCLK(24);
	
	HI702_WriteReg(0x41,HI702_ReadReg(0x41) | 0xf7);
	SENSOR_Sleep(200);
#endif	
	return 0;
    
}

LOCAL uint32_t HI702_BeforeSnapshot(uint32_t param)
{
#if 0

    uint16_t shutter = 0x00;
    uint16_t temp_reg = 0x00;
    uint16_t temp_r =0x00;
    uint16_t temp_g =0x00;
    uint16_t temp_b =0x00;    
    BOOLEAN b_AEC_on;
    

    SENSOR_TRACE("HI702_BeforeSnapshot ");   
    	if(HI702_ReadReg(0X41)  & 0x08 == 0x08)  //AEC on
    		b_AEC_on = SENSOR_TRUE;
    	else
    		b_AEC_on = SENSOR_FALSE;

	temp_reg = HI702_ReadReg(0xdb);
	temp_r = HI702_ReadReg(0xcd);
	temp_g = HI702_ReadReg(0xce);
	temp_b = HI702_ReadReg(0xcf);

	shutter = (HI702_ReadReg(0x03)<<8)  | (HI702_ReadReg(0x04)&0x00ff) ;
	shutter = shutter /2;

	if(b_AEC_on)
		HI702_WriteReg(0x41,HI702_ReadReg(0x41) & 0xc5); //0x01);
	SENSOR_Sleep(300); 

///12m
	Sensor_SetMCLK(12);
	
	HI702_WriteReg(0x03,shutter/256);
	HI702_WriteReg(0x04,shutter & 0x00ff);	
   	//SENSOR_TRACE("HI702_BeforeSnapshot, temp_r=%x,temp_reg=%x, final = %x ",temp_r,temp_reg, temp_r*temp_reg/ 0x80);    

	temp_r = (temp_r*temp_reg) / 0x80;
	temp_g = (temp_g*temp_reg) / 0x80;
	temp_b = (temp_b*temp_reg) / 0x80;
	if(b_AEC_on)
	{
		HI702_WriteReg(0xcd, temp_r);
		HI702_WriteReg(0xce, temp_g);
		HI702_WriteReg(0xcf , temp_b);
	}
   	//SENSOR_TRACE("HI702_BeforeSnapshot, temp_r=%x,temp_g=%x, temp_b = %x ",temp_r,temp_g,temp_b);    

	SENSOR_Sleep(300); 
#endif	
    	return 0;
    
}

LOCAL uint32_t read_ev_value(uint32_t value)
{
        return 0;
}
LOCAL uint32_t write_ev_value(uint32_t exposure_value)
{
        return 0;	
}
LOCAL uint32_t read_gain_value(uint32_t value)
{
        return 0;
}
LOCAL uint32_t write_gain_value(uint32_t gain_value)
{	
        return 0;
}
LOCAL uint32_t read_gain_scale(uint32_t value)
{
        return SENSOR_GAIN_SCALE;
}
LOCAL uint32_t set_frame_rate(uint32_t param)  
{
        // Maybe implement fixed fps settings for 15fps, 20fps later
	HI702_WriteGroupRegs(HI702_image_auto_fps);
        return 0;
}
#if 0
struct sensor_drv_cfg sensor_hi702 = {
        .sensor_pos = CONFIG_DCAM_SENSOR_POS_HI702,
        .sensor_name = "hi702",
        .driver_info = &g_HI702_yuv_info,
};

static int __init sensor_hi702_init(void)
{
        return dcam_register_sensor_drv(&sensor_hi702);
}

subsys_initcall(sensor_hi702_init);
#endif
