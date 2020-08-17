/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifdef CONFIG_CAMERA_KANAS
/*
 * Workaround: Increase DDR frequency when using the 720p preview modes
 * SPRD's devfreq ondemand governor is not fast or sensitive enough to
 * raise the frequency when a load spike occurs.
 * This spike will either cause an overflow, due to the slower processing
 * on a recent burst of frames or an undefined behavior in the sensor.
 */
#define WA_BOOST_DDR_FREQ_720P

/*
 * Workaround: Limit Frame rate
 * Allows setting arbritrary limits depending on the resolution.
 * Write to s_fps_limit the maximum fps desired.
 */
#define WA_LIMIT_HD_CAM_FPS
#endif

#include <utils/Log.h>
#include <errno.h>
#include "sensor.h"
#include "jpeg_exif_header.h"
#include "sensor_drv_u.h"
#include "cmr_oem.h"

#ifdef WA_BOOST_DDR_FREQ_720P
#include <stdio.h>
#endif

#include "sensor_s5k4ecgx_regs_mipi.h"

#ifdef	 __cplusplus
	extern	 "C"
	{
#endif

#define S5K4EC_I2C_ADDR_W        0xac>>1//0x5A//0x5A-0101 1010->0010 1101
#define S5K4EC_I2C_ADDR_R        0xac>>1//0x5A//0x5B-0101 1011->0010 1101

#define FOCUS_ZONE_W 80
#define FOCUS_ZONE_H 60

#define EXPOSURE_ZONE_W 1280
#define EXPOSURE_ZONE_H 960

/*
 * Light status values
 * Or, we can use the camera_light_status_type
 */
#define LIGHT_STATUS_LOW_LEVEL 0x0032
#define LIGHT_STATUS_HIGH_LEVEL 0x0082
#define LIGHT_STATUS_IS_LOW(x) \
	((x) < LIGHT_STATUS_LOW_LEVEL)
#define LIGHT_STATUS_IS_NORMAL(x) \
	(!LIGHT_STATUS_IS_LOW(x))

LOCAL uint32_t is_cap = 0;
LOCAL uint16_t s_current_shutter = 0;
LOCAL uint16_t s_current_gain = 0;


// Global copy of states
LOCAL uint8_t s_contrast_lvl = 3;
LOCAL uint8_t s_sharpness_lvl = 3;
LOCAL uint8_t s_saturation_lvl = 3;
LOCAL uint8_t s_image_effect = 0;
LOCAL uint8_t s_ISO_mode = 0;
LOCAL uint8_t s_anti_flicker_mode = 0; //50Hz
LOCAL uint8_t s_ev_comp_lvl = 3;

LOCAL int16_t s_fps_cur_mode = -1; // current max FPS (abs max is 30)
LOCAL int32_t s_target_max_fps = -1; // target max FPS
LOCAL uint16_t s_cur_scene = 0;
LOCAL uint8_t  s_force_set_scene = 1;
LOCAL uint32_t s_preview_mode = 0;
LOCAL uint32_t s_white_balance = 0;

// Local copy of Flash state (copied from the global context)
LOCAL uint8_t  s_flash_state = 0; // flash state in the global context
LOCAL uint32_t s_torch_mode_en = 0;

LOCAL uint8_t s_fast_ae_en = 0;
LOCAL uint8_t s_stream_is_on = 0;

// Local copy of state used by the HD camcorder settings
LOCAL uint32_t s_brightness_lvl = 3; // Default value
LOCAL uint32_t s_metering_mode = CAMERA_AE_CENTER_WEIGHTED;
LOCAL  uint8_t s_hd_applied = 0;

// Local AF states
LOCAL uint16_t s_focus_mode = 0;
LOCAL  uint8_t s_using_low_light_af = 0;
LOCAL  uint8_t s_af_wnd_has_changed = 0;

// FPS modes
enum {
	FPS_MODE_AUTO = 0,
	FPS_MODE_7,
	FPS_MODE_10,
	FPS_MODE_12,
	FPS_MODE_15,
	FPS_MODE_24,
	FPS_MODE_25,
	FPS_MODE_30,
	FPS_MODE_MANUAL = 64,
	FPS_MODE_INVALID  = ((uint16_t) -1) >> 1,
};

LOCAL enum {
	ENVIRONMENT_NORMAL = 0, // Normal conditions
	ENVIRONMENT_LOW_LIGHT,  // Low light conditions
	ENVIRONMENT_DIM,        // Dimly lit (dimmer than Low light)
	ENVIRONMENT_DIM_FLASH,  // Low Light but flash will overexpose it
	ENVIRONMENT_LONG_EXPOSURE, // Almost unilluminated, no flash, might need a tripod
} s_current_env = ENVIRONMENT_NORMAL;

#ifdef WA_BOOST_DDR_FREQ_720P
LOCAL   int8_t s_ddr_boosted = 0;
#endif

#ifdef WA_LIMIT_HD_CAM_FPS
LOCAL  int32_t s_fps_limit = 0;
#endif

LOCAL uint32_t _s5k4ec_InitExifInfo(void);
LOCAL uint32_t _s5k4ec_GetResolutionTrimTab(uint32_t param);
LOCAL uint32_t _s5k4ec_PowerOn(uint32_t power_on);
LOCAL uint32_t _s5k4ec_Identify(uint32_t param);
LOCAL uint32_t _s5k4ec_set_brightness(uint32_t level);
LOCAL uint32_t _s5k4ec_set_contrast(uint32_t level);
LOCAL uint32_t _s5k4ec_set_saturation(uint32_t level);
LOCAL uint32_t _s5k4ec_set_image_effect(uint32_t effect_type);
LOCAL uint32_t _s5k4ec_set_ev(uint32_t level);
LOCAL uint32_t _s5k4ec_set_anti_flicker(uint32_t mode);
LOCAL uint32_t _s5k4ec_set_video_mode(uint32_t mode);
LOCAL uint32_t _s5k4ec_set_awb(uint32_t mode);
LOCAL uint32_t _s5k4ec_set_scene_mode(uint32_t mode);
LOCAL uint32_t _s5k4ec_BeforeSnapshot(uint32_t param);
// LOCAL uint32_t _s5k4ec_check_image_format_support(uint32_t param);
// LOCAL uint32_t _s5k4ec_pick_out_jpeg_stream(uint32_t param);
LOCAL uint32_t _s5k4ec_after_snapshot(uint32_t param);
LOCAL uint32_t _s5k4ec_GetExifInfo(uint32_t param);
LOCAL uint32_t _s5k4ec_ExtFunc(uint32_t ctl_param);
LOCAL uint32_t _s5k4ec_StreamOn(uint32_t param);
LOCAL uint32_t _s5k4ec_StreamOff(uint32_t param);
LOCAL uint32_t _s5k4ec_set_iso(uint32_t level);

// Additional functions
LOCAL uint32_t s5k4ec_I2C_write(SENSOR_REG_T* sensor_reg_ptr);
LOCAL uint32_t s5k4ec_lightcheck();
LOCAL uint32_t s5k4ec_set_Metering(uint32_t metering_mode);
LOCAL uint32_t s5k4ec_set_sharpness(uint32_t level);
LOCAL uint32_t s5k4ec_flash(uint32_t param);
LOCAL uint32_t s5k4ec_get_ISO_rate();
LOCAL uint32_t s5k4ec_get_shutter_speed();
LOCAL uint32_t s5k4ecgx_set_focus_mode(uint32_t mode);
LOCAL uint32_t __s5k4ecgx_set_focus_mode(uint32_t mode); // Force-set
LOCAL uint32_t _s5k4ecgx_reset_focus_touch_position();
LOCAL uint16_t s5k4ecgx_get_frame_time();
LOCAL uint32_t s5k4ec_wait_until_ae_stable();
LOCAL uint32_t s5k4ec_set_ae_awb_enable(uint32_t enable);
LOCAL uint32_t s5k4ec_set_ae_enable(uint32_t enable);
LOCAL uint32_t s5k4ec_set_awb_enable(uint32_t enable);

LOCAL uint32_t s5k4ecgx_fast_ae(uint32_t on);
LOCAL uint32_t s5k4ec_preflash_af(uint32_t on);
LOCAL uint32_t s5k4ec_main_flash(uint32_t on);
LOCAL uint32_t s5k4ec_low_light_AF_check();
LOCAL uint32_t s5k4ec_set_FPS(uint32_t fps);
LOCAL uint32_t s5k4ec_set_FPS_mode(uint32_t fps_mode);
LOCAL uint32_t s5k4ec_set_manual_FPS(uint32_t min, uint32_t max);
LOCAL void s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(int bit, int set);
LOCAL const char *s5k4ec_StreamStrerror(int error);

#ifdef WA_BOOST_DDR_FREQ_720P
LOCAL int8_t s5k4ec_ddr_is_slow(int8_t boost);
#endif

LOCAL EXIF_SPEC_PIC_TAKING_COND_T s_s5k4ec_exif;

LOCAL SENSOR_REG_TAB_INFO_T s_s5k4ec_resolution_Tab_YUV[] = {
	//COMMON INIT
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_common_init), 0, 0, 24, SENSOR_IMAGE_FORMAT_YUV422},
	//YUV422 PREVIEW 1
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_320X240), 320, 240, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_640X480), 640, 480, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_720X540), 720, 540, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_1024X768), 1024, 768, 24, SENSOR_IMAGE_FORMAT_YUV422},

	//YUV422 PREVIEW 2
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_1280X960), 1280, 960, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_1408x1056), 1408, 1056, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_2048X1536), 2048, 1536, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_2560X1920), 2560, 1920, 24, SENSOR_IMAGE_FORMAT_YUV422},

	{PNULL, 0, 0, 0, 0, 0}
};

LOCAL uint32_t s5k4ec_set_awb_enable(uint32_t enable)
{
	uint16_t unlock;

	if (enable == 1) {
		SENSOR_PRINT_HIGH("Unlock AWB");
		unlock = 1;
	} else if (enable == 0) {
		SENSOR_PRINT_HIGH("Lock AWB");
		unlock = 0;
	} else {
		SENSOR_PRINT_HIGH("Undefined parameter %u", enable);
		return SENSOR_OP_PARAM_ERR;
	}

	Sensor_WriteReg(0xFCFC, 0xD000);
	Sensor_WriteReg(0x0028, 0x7000);
	Sensor_WriteReg(0x002A, 0x2C66);
	Sensor_WriteReg(0x0F12, unlock);

	return 0;
}

LOCAL uint32_t s5k4ec_set_ae_enable(uint32_t enable)
{
	uint16_t unlock;

	if (enable == 1) {
		SENSOR_PRINT_HIGH("Unlock AE");
		unlock = 1;
	} else if (enable == 0) {
		SENSOR_PRINT_HIGH("Lock AE");
		unlock = 0;
	} else {
		SENSOR_PRINT_HIGH("Undefined parameter %u", enable);
		return SENSOR_OP_PARAM_ERR;
	}

	Sensor_WriteReg(0xFCFC, 0xD000);
	Sensor_WriteReg(0x0028, 0x7000);
	Sensor_WriteReg(0x002A, 0x2C5E);
	Sensor_WriteReg(0x0F12, unlock);

	return 0;
}

LOCAL uint32_t s5k4ec_set_ae_awb_enable(uint32_t enable)
{
	s5k4ec_set_ae_enable(enable);
	s5k4ec_set_awb_enable(enable);
	return 0;
}

LOCAL SENSOR_TRIM_T s_s5k4ec_Resolution_Trim_Tab[]=
{
	// COMMON INIT
	{0, 0, 640, 480, 0, 0, 0, {0, 0, 0, 0}},

	// YUV422 PREVIEW 1
	{0, 0, 320, 240, 680, 648, 40, {0, 0, 320, 240}},
	{0, 0, 640, 480, 680, 648, 40, {0, 0, 640, 480}},
	{0, 0, 720, 540, 680, 648, 40, {0, 0, 720, 540}},
	{0, 0, 1024, 768, 664, 648, 0, {0, 0, 1024, 768}},

	//YUV422 PREVIEW 2
	{0, 0, 1280, 960, 664, 648, 0, {0, 0, 1280, 960}},
	{0, 0, 1408, 1056, 664, 648, 0, {0, 0, 1408, 1056}},
	{0, 0, 2048, 1536, 660, 648, 0, {0, 0, 2048, 1536}},
	{0, 0, 2560, 1920, 660, 648, 0, {0, 0, 2560, 1920}},

	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}}
};

LOCAL SENSOR_IOCTL_FUNC_TAB_T s_s5k4ec_ioctl_func_tab =
{
	// Internal
	PNULL, /*0*/
	_s5k4ec_PowerOn,
	PNULL,/*2*/
	_s5k4ec_Identify,
	PNULL,/*4*/			// write register
	PNULL,/*5*/                 // read  register
	PNULL,//    cus_func_1//s5k4ec_init_by_burst_write,/*6*/
	_s5k4ec_GetResolutionTrimTab,//PNULL,/*7*/

	// External
	s5k4ec_set_ae_awb_enable,/*8*/
	PNULL,/*9*/
	PNULL,/*10*/
	_s5k4ec_set_brightness,/*11*/
	_s5k4ec_set_contrast,/*12*/
	s5k4ec_set_sharpness,/*13*/
	_s5k4ec_set_saturation,/*14*/
	_s5k4ec_set_scene_mode,/*15*/
	_s5k4ec_set_image_effect,/*16*/
	_s5k4ec_BeforeSnapshot,/*17*/
	_s5k4ec_after_snapshot,/*18*/
	s5k4ec_flash,//PNULL,/*19*/
	PNULL,/*20*///read_ae_value
	PNULL,/*21*///write_ae_value
	PNULL,/*22*///read_gain_value
	PNULL,/*23*///write_gain_value
	PNULL,/*24*///read_gain_scale
	PNULL,/*25*///set_frame_rate
	s5k4ecgx_set_focus_mode,/*26*///af_enable
	PNULL,/*27*///af_get_status
	_s5k4ec_set_awb,/*28*/
	PNULL,/*29*///get_skip_frame
	_s5k4ec_set_iso,/*30*///iso
	_s5k4ec_set_ev,/*31*///exposure
	PNULL,//_s5k4ec_check_image_format_support,//PNULL,/*32*///check_image_format_support
	PNULL,/*33*///change_image_format
	PNULL,/*34*///set_zoom
	_s5k4ec_GetExifInfo,//PNULL,/*35*///get_exif
	_s5k4ec_ExtFunc,//PNULL,/*36*///set_focus
	_s5k4ec_set_anti_flicker,/*37*///set_anti_banding_flicker
	_s5k4ec_set_video_mode,/*38*///set_video_mode
	PNULL,//_s5k4ec_pick_out_jpeg_stream,//PNULL,/*39*///pick_jpeg_stream
	s5k4ec_set_Metering,/*40*///set_meter_mode
	PNULL, /*41*///get_status
	_s5k4ec_StreamOn,/*42*///stream_on
	_s5k4ec_StreamOff,//_s5k4ec_StreamOff/*43*/// stream_off
	NULL,
};

SENSOR_INFO_T g_s5k4ec_mipi_yuv_info =
{
	S5K4EC_I2C_ADDR_W,				// salve i2c write address
	S5K4EC_I2C_ADDR_R, 				// salve i2c read address
	SENSOR_I2C_VAL_16BIT|SENSOR_I2C_REG_16BIT|SENSOR_I2C_FREQ_400,//SENSOR_I2C_VAL_8BIT|SENSOR_I2C_REG_8BIT,			// bit0: 0: i2c register value is 8 bit, 1: i2c register value is 16 bit
									// bit1: 0: i2c register addr  is 8 bit, 1: i2c register addr  is 16 bit
									// other bit: reseved
	SENSOR_HW_SIGNAL_PCLK_N|\
	SENSOR_HW_SIGNAL_VSYNC_P|\
	SENSOR_HW_SIGNAL_HSYNC_P,		// bit0: 0:negative; 1:positive -> polarily of pixel clock
									// bit2: 0:negative; 1:positive -> polarily of horizontal synchronization signal
									// bit4: 0:negative; 1:positive -> polarily of vertical synchronization signal
									// other bit: reseved

	// preview mode
	SENSOR_ENVIROMENT_NORMAL|\
	SENSOR_ENVIROMENT_NIGHT,

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

	0x7,
// bit[0:7]: count of step in brightness, contrast, sharpness, saturation
									// bit[8:31] reseved

	SENSOR_LOW_PULSE_RESET,//SENSOR_LOW_PULSE_RESET,		// reset pulse level
	50,								// reset pulse width(ms)

	SENSOR_LOW_LEVEL_PWDN,		// 1: high level valid; 0: low level valid

	1,								// count of identify code
	 {{0x01a4, 0x4ec0},                // supply two code to identify sensor.
	{0x01a6, 0x0011}},               // for Example: index = 0-> Device id, index = 1 -> version id

	SENSOR_AVDD_2800MV,			// voltage of avdd
	2560,							// max width of source image
	1920,							// max height of source image
	"s5k4ec",						// name of sensor

	SENSOR_IMAGE_FORMAT_MAX,        // define in SENSOR_IMAGE_FORMAT_E enum,SENSOR_IMAGE_FORMAT_MAX
                                    // if set to SENSOR_IMAGE_FORMAT_MAX here, image format depent on SENSOR_REG_TAB_INFO_T

	SENSOR_IMAGE_PATTERN_YUV422_UYVY,	// pattern of input image form sensor;
	s_s5k4ec_resolution_Tab_YUV,	// point to resolution table information structure
	&s_s5k4ec_ioctl_func_tab,		// point to ioctl function table

	PNULL,							// information and table about Rawrgb sensor
	PNULL,				// extend information about sensor
	SENSOR_AVDD_1800MV,                     // iovdd
	SENSOR_AVDD_1200MV,                      // dvdd
	1,                     // skip frame num before preview
	1,                     // skip frame num before capture
	0,                     // deci frame num during preview;
	0,                     // deci frame num during video preview;

	0,                     // threshold enable
	0,                     // threshold mode
	0,                     // threshold start postion
	0,                     // threshold end postion
	0,                     // i2c_dev_handler
	{SENSOR_INTERFACE_TYPE_CSI2, 2, 8, 1},
	PNULL,
	1,                     // skip frame num while change setting
};

LOCAL int32_t _gcd(int32_t a, int32_t b)
{
	for (int32_t r; b > 0; ) {
		r = a % b;
		a = b;
		b = r;
	}

	return a;
}

LOCAL uint32_t _s5k4ec_GetExifInfo(__attribute__((unused)) uint32_t param)
{
	EXIF_SPEC_PIC_TAKING_COND_T* exif_ptr=&s_s5k4ec_exif;
	uint32_t shutter_speed;
	uint16_t iso_value;
	int32_t gcd_shutter;

	iso_value = s5k4ec_get_ISO_rate();

	exif_ptr->ISOSpeedRatings.count = 1;
	exif_ptr->ISOSpeedRatings.type = EXIF_SHORT;
	exif_ptr->ISOSpeedRatings.size = sizeof(uint16_t);
	memcpy((void*)&exif_ptr->ISOSpeedRatings.ptr[0],
	       (void*)&iso_value, sizeof(uint16_t));

	shutter_speed = s5k4ec_get_shutter_speed();

	if (shutter_speed == 0) {
		exif_ptr->valid.ExposureTime = 0;
	} else {
		exif_ptr->valid.ExposureTime = 1;

		gcd_shutter = _gcd(shutter_speed, 400000);
		exif_ptr->ExposureTime.numerator = shutter_speed / gcd_shutter;
		exif_ptr->ExposureTime.denominator = 400000 / gcd_shutter;
	}

	return (unsigned long)exif_ptr;
}

LOCAL uint32_t _s5k4ec_InitExifInfo(void)
{
	EXIF_SPEC_PIC_TAKING_COND_T* exif_ptr=&s_s5k4ec_exif;

	memset(&s_s5k4ec_exif, 0, sizeof(EXIF_SPEC_PIC_TAKING_COND_T));

	SENSOR_PRINT_HIGH("Initializing Exif data template");

	exif_ptr->valid.FNumber=1;
	exif_ptr->FNumber.numerator=14;
	exif_ptr->FNumber.denominator=5;

	exif_ptr->valid.ExposureProgram=1;
	exif_ptr->ExposureProgram=0x04;

	//exif_ptr->SpectralSensitivity[MAX_ASCII_STR_SIZE];
	//exif_ptr->ISOSpeedRatings;
	//exif_ptr->OECF;

	//exif_ptr->ShutterSpeedValue;

	exif_ptr->valid.ApertureValue=1;
	exif_ptr->ApertureValue.numerator=14;
	exif_ptr->ApertureValue.denominator=5;

	//exif_ptr->BrightnessValue;
	//exif_ptr->ExposureBiasValue;

	exif_ptr->valid.MaxApertureValue=1;
	exif_ptr->MaxApertureValue.numerator=14;
	exif_ptr->MaxApertureValue.denominator=5;

	//exif_ptr->SubjectDistance;
	//exif_ptr->MeteringMode;
	//exif_ptr->LightSource;
	//exif_ptr->Flash;

	exif_ptr->valid.FocalLength=1;
	exif_ptr->FocalLength.numerator=289;
	exif_ptr->FocalLength.denominator=100;

	//exif_ptr->SubjectArea;
	//exif_ptr->FlashEnergy;
	//exif_ptr->SpatialFrequencyResponse;
	//exif_ptr->FocalPlaneXResolution;
	//exif_ptr->FocalPlaneYResolution;
	//exif_ptr->FocalPlaneResolutionUnit;
	//exif_ptr->SubjectLocation[2];
	//exif_ptr->ExposureIndex;
	//exif_ptr->SensingMethod;

	exif_ptr->valid.FileSource=1;
	exif_ptr->FileSource=0x03;

	//exif_ptr->SceneType;
	//exif_ptr->CFAPattern;
	//exif_ptr->CustomRendered;

	exif_ptr->valid.ExposureMode=1;
	exif_ptr->ExposureMode=0x00;

	exif_ptr->valid.WhiteBalance=1;
	exif_ptr->WhiteBalance=0x00;

	//exif_ptr->DigitalZoomRatio;
	//exif_ptr->FocalLengthIn35mmFilm;
	//exif_ptr->SceneCaptureType;
	//exif_ptr->GainControl;
	//exif_ptr->Contrast;
	//exif_ptr->Saturation;
	//exif_ptr->Sharpness;
	//exif_ptr->DeviceSettingDescription;
	//exif_ptr->SubjectDistanceRange;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_GetResolutionTrimTab(__attribute__((unused)) uint32_t param)
{
	return (unsigned long)s_s5k4ec_Resolution_Trim_Tab;
}

LOCAL uint32_t __s5k4ec_PowerOn(uint32_t power_on)
{
	SENSOR_AVDD_VAL_E dvdd_val = g_s5k4ec_mipi_yuv_info.dvdd_val;
	SENSOR_AVDD_VAL_E avdd_val = g_s5k4ec_mipi_yuv_info.avdd_val;
	SENSOR_AVDD_VAL_E iovdd_val = g_s5k4ec_mipi_yuv_info.iovdd_val;
	BOOLEAN power_down = g_s5k4ec_mipi_yuv_info.power_down_level;
	BOOLEAN reset_level = g_s5k4ec_mipi_yuv_info.reset_pulse_level;

	SENSOR_PRINT("_s5k4ec_PowerOn: E!!  (1:on, 0:off): %u \n", power_on);

 	if (SENSOR_TRUE == power_on) {
		Sensor_PowerDown(power_down);
		Sensor_SetResetLevel(reset_level);
		SENSOR_Sleep(1);

		// Open power
		Sensor_SetAvddVoltage(avdd_val);
		SENSOR_Sleep(5);
		Sensor_SetIovddVoltage(iovdd_val);
		Sensor_SetMonitorVoltage(avdd_val); // AF
		SENSOR_Sleep(2);

		Sensor_SetMCLK(SENSOR_DEFALUT_MCLK);
		SENSOR_Sleep(7);

		Sensor_SetDvddVoltage(dvdd_val);
		SENSOR_Sleep(1);

		Sensor_PowerDown(!power_down);
		SENSOR_Sleep(1);

		Sensor_Reset(!reset_level);
		SENSOR_Sleep(5);
	} else {
		Sensor_SetResetLevel(reset_level);
		SENSOR_Sleep(1);

		Sensor_SetMCLK(SENSOR_DISABLE_MCLK);
		SENSOR_Sleep(5);

		Sensor_PowerDown(power_down);
		SENSOR_Sleep(1);

		Sensor_SetVoltage(SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED);
		Sensor_SetMonitorVoltage(SENSOR_AVDD_CLOSED);
		SENSOR_Sleep(10);
	}
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_PowerOn(uint32_t power_on)
{
#ifndef CONFIG_CAMERA_IOCTL_IOCTL_HAS_POWER_ONOFF
	/*
	 * Some devices offer a convenience IOCTL, SENSOR_IO_POWER_ONOFF, that
	 * will handle the process of turning this on and off.
	 * The CONFIG_CAMERA_IOCTL_IOCTL_HAS_POWER_ONOFF when set will
	 * attempt to call that only after the poweron function has
	 * been called.
	 * If that IOCTL is present, it is advisable to use that instead
	 * of this.
	 */
	__s5k4ec_PowerOn(power_on);
#endif

#ifdef WA_BOOST_DDR_FREQ_720P
	if (s_ddr_boosted) {
		s5k4ec_ddr_is_slow(0);
	}
#endif

	if (s_torch_mode_en) {
		s_torch_mode_en = 0;
		Sensor_SetFlash(FLASH_CLOSE);
	}

	SENSOR_PRINT_HIGH("(1:on, 0:off): %u", power_on);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_Identify(__attribute__((unused)) uint32_t param)
{
#define s5k4ec_PID_VALUE    0x4ec0
#define s5k4ec_PID_ADDR     0x01a4
#define s5k4ec_VER_VALUE    0x0011
#define s5k4ec_VER_ADDR     0x01a6
	uint16_t pid_value = 0x00;
	uint16_t ver_value = 0x00;
	uint32_t ret_value = SENSOR_FAIL;
	uint8_t max_attmp = 2;

	SENSOR_PRINT_HIGH("-----20130610----");

	for (uint8_t i = 1; i <= max_attmp; i++) {
		SENSOR_PRINT_HIGH("identify tries: %u/%u", i, max_attmp);

		Sensor_WriteReg(0xfcfc,0xd000);
		Sensor_WriteReg(0x002c, 0x7000);
		Sensor_WriteReg(0x002e, 0x01a4);
		pid_value = Sensor_ReadReg(0x0f12);


		if (s5k4ec_PID_VALUE == pid_value) {
			Sensor_WriteReg(0x002c, 0x7000);
			Sensor_WriteReg(0x002e, s5k4ec_VER_ADDR);
			ver_value = Sensor_ReadReg(0x0f12);
			SENSOR_PRINT_HIGH("PID = %x, VER = %x", pid_value, ver_value);
			if (s5k4ec_VER_VALUE == ver_value) {
				ret_value = SENSOR_SUCCESS;
				SENSOR_PRINT_HIGH("this is s5k4ec sensor !");
			} else {
				SENSOR_PRINT_ERR("this is xx%x%x sensor !",
				pid_value, ver_value);
			}
			break;
		} else {
			SENSOR_PRINT_ERR("identify fail, pid_value=%d", pid_value);

			if (i != max_attmp) {
				SENSOR_PRINT_HIGH("Retrying after 100 msec");

				// Power cycle
				__s5k4ec_PowerOn(0);
				SENSOR_Sleep(100);
				__s5k4ec_PowerOn(1);
			} else {
				SENSOR_PRINT_HIGH("Bailing out");
			}
		}
	}

	if (SENSOR_SUCCESS == ret_value) {
		//_s5k4ec_get_antibanding();
		_s5k4ec_InitExifInfo();
		//_s5k4ec_set_vendorid();

		/*
		 * Reinitialize all the local state variables
		 * Expecially if we are recovering from a sudden crash
		 */
		s_contrast_lvl = 3;
		s_sharpness_lvl = 3;
		s_saturation_lvl = 3;
		s_image_effect = 0;
		s_ISO_mode = 0;
		s_anti_flicker_mode = 0;
		s_ev_comp_lvl = 3;

		s_fps_cur_mode = -1;
		s_target_max_fps = -1;
		s_current_env = ENVIRONMENT_NORMAL;
		s_cur_scene = 0;
		s_preview_mode = 0;
		s_white_balance = 0;

		// Local copy of Flash state (copied from the global context)
		s_flash_state = 0; // flash state in the global context
		s_torch_mode_en = 0;

		// Local copy of state used by the HD camcorder settings
		s_brightness_lvl = 3; // Default value
		s_metering_mode = CAMERA_AE_CENTER_WEIGHTED;
		s_hd_applied = 0;

#ifdef WA_LIMIT_HD_CAM_FPS
		s_fps_limit = 0;
#endif

		// Local AF states
		s_focus_mode = 0;
		s_using_low_light_af = 0;
		s_af_wnd_has_changed = 0;

		// Misc
		s_fast_ae_en = 0;
		s_stream_is_on = 0;
	}

	return ret_value;
}

LOCAL uint32_t _s5k4ec_set_brightness(uint32_t level)
{
	if (level > 6) {
		SENSOR_PRINT_ERR("Undefined Brightness level %u", level);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Brightness level %u", level);

	s5k4ec_I2C_write(s5k4ec_ae_brightness_tab[level]);
	s_brightness_lvl = level;
	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_BRIGHTNESSVALUE, (uint32_t) level);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_contrast(uint32_t level)
{
	if (level > 6) {
		SENSOR_PRINT_ERR("Undefined Contrast level %u", level);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Contrast level %u", level);

	s5k4ec_I2C_write(s5k4ec_contrast_tab[level]);
	s_contrast_lvl = level;
	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_CONTRAST, (uint32_t) level);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_iso(uint32_t level)
{
	if (CAMERA_ISO_MAX <= level) {
		SENSOR_PRINT_ERR("Undefined ISO mode %u", level);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply ISO mode %u", level);

	switch(level) {
	case CAMERA_ISO_50:
		s5k4ec_I2C_write(s5k4ec_ISO_50);
		break;
	case CAMERA_ISO_100:
		s5k4ec_I2C_write(s5k4ec_ISO_100);
		break;
	case CAMERA_ISO_200:
		s5k4ec_I2C_write(s5k4ec_ISO_200);
		break;
	case CAMERA_ISO_300: /* This value is linearly interpolated */
		s5k4ec_I2C_write(s5k4ec_ISO_300);
		break;
	case CAMERA_ISO_400:
		s5k4ec_I2C_write(s5k4ec_ISO_400);
		break;
	/*
	 * These values are experimental
	 * To be frank, the factory settings only allow auto ISO
	 * go up to around ISO400, with or without Digital Gain.
	 * It's possible to use Higher ISO settings but doing so
	 * will cause much increase in noise with some unexplored
	 * side effects.
	 */
	case CAMERA_ISO_600:
		s5k4ec_I2C_write(s5k4ec_ISO_600);
		break;
	case CAMERA_ISO_800:
	case CAMERA_ISO_1600:
		s5k4ec_I2C_write(s5k4ec_ISO_800);
		break;

	/*
	 * If ever the check passed but with an unknown value
	 * default to auto.
	 */
	default:
	case CAMERA_ISO_AUTO:
		s5k4ec_I2C_write(s5k4ec_ISO_auto);
		break;
	}

	s_ISO_mode = level;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_saturation(uint32_t level)
{
	if (level > 6) {
		SENSOR_PRINT_ERR("Undefined Saturation level %u", level);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Saturation level %u",level);
	s5k4ec_I2C_write(s5k4ec_saturation_tab[level]);
	s_saturation_lvl = level;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_image_effect(uint32_t effect_type)
{
	if (effect_type > 9) {
		SENSOR_PRINT_ERR("Undefined Image Effect #%u", effect_type);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Image Effect type %u", effect_type);
	s5k4ec_I2C_write(s5k4ec_image_effect_tab[effect_type]);
	s_image_effect = (int8_t) effect_type;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_ev(uint32_t level)
{
	const uint16_t hd_mode_offset = 0x8;
	if (level > 6) {
		SENSOR_PRINT_ERR("Undefined Exposure Compensation level %u", level);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Exposure Compensation level %u", level);

	if (s_hd_applied) {
		SENSOR_PRINT_HIGH("HD settings detected, apply offset -%u", hd_mode_offset);
		Sensor_WriteReg(0x0028, 0x7000);
		Sensor_WriteReg(0x002A, 0x023A); // REG_TC_UserExposureVal88
		SENSOR_PRINT_HIGH("Apply %u", s5k4ec_ev_tab[level][2].reg_value - hd_mode_offset);
		Sensor_WriteReg(0x0F12, s5k4ec_ev_tab[level][2].reg_value - hd_mode_offset);
	} else {
		s5k4ec_I2C_write(s5k4ec_ev_tab[level]);
	}

	s_ev_comp_lvl = (int8_t) level;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_anti_flicker(uint32_t mode)
{
	const char *str = "";
	if (mode >= CAMERA_MAX_ANTIBANDING) {
		SENSOR_PRINT_ERR("Undefined Anti-banding mode %u", mode);
		return SENSOR_OP_PARAM_ERR;
	}

	if (mode == CAMERA_ANTIBANDING_50HZ) {
		str = "Applying 50Hz";
	} else if (mode == CAMERA_ANTIBANDING_60HZ) {
		str = "Applying 60Hz";
	} else if (mode == CAMERA_ANTIBANDING_OFF) {
		str = "Disabling ";
	} else if (mode == CAMERA_ANTIBANDING_AUTO) {
		str = "Applying Auto";
	}

	SENSOR_PRINT_HIGH("%s Anti-banding", str);

	// Deactivate auto anti flicker algorithm when
	if (mode == CAMERA_ANTIBANDING_AUTO)
		s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(AA_FLICKER, 1);
	else
		s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(AA_FLICKER, 0);

	s5k4ec_I2C_write(s5k4ec_anti_banding_flicker_tab[mode]);
	s_anti_flicker_mode = (int8_t) mode;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_video_mode(uint32_t mode)
{
	struct camera_context *cxt = camera_get_cxt();
	SENSOR_REG_TAB_INFO_T *res;

	SENSOR_PRINT_HIGH("mode = 0x%X", mode);

	SENSOR_PRINT_HIGH("cxt->sn_cxt.preview_mode=%u s_preview_mode=%u", cxt->sn_cxt.preview_mode, s_preview_mode);
	res = &s_s5k4ec_resolution_Tab_YUV[cxt->sn_cxt.preview_mode];

	/* Define some artificial limits depending on the resolution.
	 * On kanas3g, 1280x960 recording is possible which is nice
	 * because it's in the middle 720p(960x720) 1080p(1440x1080) recording.
	 * (Keep in mind this is 4:3, comparing to 16:9 is just unfair.
	 * But due to the configuration limits, we can only get up to 27 fps.
	 *
	 * Same story with 14**x10** resolution (the 4:3 1080p) but with
	 * drastic tradeoffs causing it to go up to 11 fps, tops.
	 *
	 * Limiting to 24 fps on 1280x960 not only make the fps look like
	 * it was shoot in film but also frees up a bit of processing power.
	 * DCAM line1 tx_error is still present but limiting it to 24 fps
	 * will make it even less frequent.
	 *
	 * Funny note, DCAM line1 tx_errors is highly unlikely to occur on
	 * 14**x10** because the bottleneck on that case is actually the
	 * camera not the memory bus.
	 */
#ifdef WA_LIMIT_HD_CAM_FPS
	if (1400 <= res->width) // experimental 1080p mode
		s_fps_limit = 11;
	else if ((1280*960) <= (res->width * res->height))
		s_fps_limit = 24;
	else
		s_fps_limit = 0;
#endif


	/*
	 * There are camera apps that set the FPS after StreamOn() is done.
	 * We need to re-apply the FPS settings when that happens.
	 */
	if ((s_target_max_fps != (int32_t) cxt->cmr_set.frame_rate) && s_stream_is_on) {
		s_fps_cur_mode = FPS_MODE_INVALID;
#ifdef WA_LIMIT_HD_CAM_FPS
		if ((s_fps_limit) &&
		    (((int32_t)cxt->cmr_set.frame_rate > s_fps_limit) || (cxt->cmr_set.frame_rate == 0))) {
			SENSOR_PRINT_HIGH("workaround: Reapplying max FPS limit to %d", s_fps_limit);
			if (!cxt->cmr_set.frame_rate) // Automatic Framerate is requested
				s5k4ec_set_manual_FPS(0, s_fps_limit);
			else
				s5k4ec_set_manual_FPS(s_fps_limit, s_fps_limit);
		}
#endif

		if (s_fps_cur_mode == FPS_MODE_INVALID)
			s5k4ec_set_FPS(cxt->cmr_set.frame_rate);
	}
	s_target_max_fps = cxt->cmr_set.frame_rate;

	/*
	 * Apply the so called HD camcorder settings.
	 * These settings are designed to enhance the overall perceived
	 * video quality. But, we only apply this to the HD resolutions
	 * which is anything above 1280*720.
	 * Also 960*720, according to Wikipedia, is also an HD resolution.
	 */
	if (((1280 <= res->width) && (720 <= res->height)) || // anything above 720p
	    ((960 == res->width) && (720 == res->height)) // 960x720 case
	) {
		if (!s_hd_applied) {
			SENSOR_PRINT_HIGH("Applying HD camcorder settings");
			s5k4ec_I2C_write(s5k4ec_enable_camcorder);
			s_hd_applied = 1;

			// default HD settings is too bright.
			// _s5k4ec_set_ev will apply an offset when s_hd_applied == 1
			_s5k4ec_set_ev(s_ev_comp_lvl);
		}
	} else if (s_hd_applied) {
		SENSOR_PRINT_HIGH("Reverting HD camcorder settings");
		s5k4ec_I2C_write(s5k4ec_disable_camcorder);

		// revert AE settings
		_s5k4ec_set_ev(s_ev_comp_lvl);

		// Revert Sharpness setting, default level is 3
		s5k4ec_set_sharpness(s_sharpness_lvl);
		s_hd_applied = 0;
	}

	s_preview_mode = cxt->sn_cxt.preview_mode;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_awb(uint32_t mode)
{
	if (mode > 6) {
		SENSOR_PRINT_ERR("Undefined Auto White Balance mode %u",mode);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Auto White Balance mode %u", mode);
	if (mode == 0) // AWB mode
		s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(AA_WB_ACTIVE, 1);
	else
		s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(AA_WB_ACTIVE, 0);

	s5k4ec_I2C_write(s5k4ec_awb_tab[mode]);
	s_white_balance = mode;

	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_LIGHTSOURCE, (uint32_t) mode);
	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_WHITEBALANCE, (uint32_t) mode);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_set_scene_mode(uint32_t mode)
{
	SENSOR_PRINT_HIGH("Apply Scene mode %u", mode);

	if ((s_cur_scene == mode) && (s_force_set_scene == 0)) {
		SENSOR_PRINT_HIGH("Already applied");
		return SENSOR_SUCCESS;
	}

	if ((mode != CAMERA_SCENE_MODE_AUTO) && (mode != s_cur_scene)) {
		SENSOR_PRINT_HIGH("Trying to revert changes from the previous scene mode");
		switch (s_cur_scene) {
		case CAMERA_SCENE_MODE_PORTRAIT:
		case CAMERA_SCENE_MODE_TEXT:
			s5k4ec_I2C_write(s5k4ec_scene_revert_sharpness0);
			break;
		case CAMERA_SCENE_MODE_LANDSCAPE:
			s5k4ec_I2C_write(s5k4ec_scene_revert_sharpness0);
			s5k4ec_set_Metering(s_metering_mode);
			break;
		case CAMERA_SCENE_MODE_DARK:
			s5k4ec_I2C_write(s5k4ec_scene_revert_gain);
			/* Fall-through */
		case CAMERA_SCENE_MODE_NIGHT:
			s5k4ec_I2C_write(s5k4ec_scene_revert_exp);
			s5k4ec_I2C_write(s5k4ec_scene_revert_night);
			__s5k4ecgx_set_focus_mode(s_focus_mode);
			break;
		case CAMERA_SCENE_MODE_SPORTS:
		case CAMERA_SCENE_MODE_FIREWORK:
			s5k4ec_I2C_write(s5k4ec_scene_revert_sports);
			s5k4ec_I2C_write(s5k4ec_scene_revert_exp);
			// _s5k4ec_set_iso(s_ISO_mode); // Moved to StreamOn()
			break;
		case CAMERA_SCENE_MODE_PARTY:
			s5k4ec_I2C_write(s5k4ec_scene_revert_sports);
			_s5k4ec_set_saturation(s_saturation_lvl);
			break;
		case CAMERA_SCENE_MODE_BEACH:
			s5k4ec_I2C_write(s5k4ec_scene_revert_sports);
			_s5k4ec_set_saturation(s_saturation_lvl);
			_s5k4ec_set_brightness(s_brightness_lvl);
			break;
		case CAMERA_SCENE_MODE_FALL_COLOR:
			_s5k4ec_set_saturation(s_saturation_lvl);
			break;
		case CAMERA_SCENE_MODE_BACKLIGHT:
			s5k4ec_set_Metering(s_metering_mode);
			break;

		// These three scenes just adjusts the White balance and disables AWB
		case CAMERA_SCENE_MODE_SUNSET:
		case CAMERA_SCENE_MODE_DUSK_DAWN:
		case CAMERA_SCENE_MODE_CANDLELIGHT:
			_s5k4ec_set_awb(s_white_balance);
			/* Fall-through */
		default:
			break;
		}
		SENSOR_PRINT_HIGH("Revert Done.");
	}

	switch (mode) {
	case CAMERA_SCENE_MODE_AUTO:
		s5k4ec_I2C_write(s5k4ec_scene_off);
		break;

	/*
	 * This mode is essentially night scene mode with drastically longer
	 * Exposure time and less aggressive Auto Gain/Auto ISO.
	 * You'll need a patched sprd_dcam kernel module to avoid getting
	 * timeout errors, and also a tripod.
	 */
	case CAMERA_SCENE_MODE_DARK:
		s5k4ec_I2C_write(s5k4ec_scene_dark);
		break;
	case CAMERA_SCENE_MODE_NIGHT:
		s5k4ec_I2C_write(s5k4ec_scene_night);
		break;
	case CAMERA_SCENE_MODE_PORTRAIT:
		s5k4ec_I2C_write(s5k4ec_scene_portrait);
		break;
	case CAMERA_SCENE_MODE_LANDSCAPE:
		s5k4ec_I2C_write(s5k4ec_scene_landscape);
		break;
	case CAMERA_SCENE_MODE_SPORTS:
		s5k4ec_I2C_write(s5k4ec_scene_sports);
		break;
	case CAMERA_SCENE_MODE_PARTY:
		s5k4ec_I2C_write(s5k4ec_scene_party);
		break;
	case CAMERA_SCENE_MODE_BEACH:
		s5k4ec_I2C_write(s5k4ec_scene_beach);
		break;
	case CAMERA_SCENE_MODE_SUNSET:
		s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(AA_WB_ACTIVE, 0); // Disables AWB
		s5k4ec_I2C_write(s5k4ec_scene_sunset);
		break;
	case CAMERA_SCENE_MODE_DUSK_DAWN:
		s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(AA_WB_ACTIVE, 0); // Disables AWB
		s5k4ec_I2C_write(s5k4ec_scene_dawn);
		break;
	case CAMERA_SCENE_MODE_FALL_COLOR:
		s5k4ec_I2C_write(s5k4ec_scene_fall);
		break;
	case CAMERA_SCENE_MODE_TEXT:
		s5k4ec_I2C_write(s5k4ec_scene_text);
		break;
	case CAMERA_SCENE_MODE_CANDLELIGHT:
		s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(AA_WB_ACTIVE, 0); // Disables AWB
		s5k4ec_I2C_write(s5k4ec_scene_candlelight);
		break;
	case CAMERA_SCENE_MODE_FIREWORK:
		s5k4ec_I2C_write(s5k4ec_scene_firework);
		break;
	case CAMERA_SCENE_MODE_BACKLIGHT:
		s5k4ec_I2C_write(s5k4ec_scene_backlight);
		break;

	case CAMERA_SCENE_MODE_HDR:
		/* Do nothing */
		break;
	case CAMERA_SCENE_MODE_ACTION:
	case CAMERA_SCENE_MODE_NORMAL:
	default:
		SENSOR_PRINT_ERR("Undefined Scene mode %u", mode);
		return SENSOR_SUCCESS; // ignore the error
	}

	if (mode == CAMERA_SCENE_MODE_HDR)
		Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_SCENECAPTURETYPE, mode);
	else
		Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_SCENECAPTURETYPE, s_cur_scene);

	s_cur_scene = mode;
	s_force_set_scene = 0;
	return SENSOR_SUCCESS;
}

/*
 * Checks if either of these two sets of conditions are fully met:
 *    A.1. Flash will be used.
 *    A.2. Scene mode is neither Dark or Night
 *
 *    B.1. Flash will be used.
 *    B.2. Scene mode is either Dark or Night
 *    B.3. With flash on, the environment is acceptably illuminated.
 *
 * Returns 1 if at least one set is met, otherwise 0.
 */
LOCAL uint8_t _s5k4ec_nightscene_flash(void)
{
	uint32_t lux;
	uint16_t frame_time;
	/*
	 * We could actually support flash with Night Scene mode
	 * but not the full "High Light" flash since it
	 * won't last the entire shutter cycle. We could, however, use
	 * the dimmer "Torch" flash (basically the flashlight mode)
	 * to act as the supplemental light source. The longer
	 * exposure should compensate for the drastically dimmer light.
	 */
	if (FLASH_CLOSE == s_flash_state)
		return 0;

	if ((s_cur_scene != CAMERA_SCENE_MODE_NIGHT) &&
	    (s_cur_scene != CAMERA_SCENE_MODE_DARK))
		return 1;

	SENSOR_PRINT_HIGH("It seems that flash will be used, using torch instead");
	s_torch_mode_en = 1;
	Sensor_SetFlash(FLASH_OPEN/*FLASH_TORCH*/);

	s5k4ec_set_ae_awb_enable(1);
	s5k4ecgx_fast_ae(1);
	s5k4ec_wait_until_ae_stable();
	s5k4ecgx_fast_ae(0);

	/*
	 * It is possible that the sensor may use the longer exposure time
	 * even with the flash and eventually lead to an overexposed image.
	 * After making sure that AE is stable, check the lux levels
	 * if it's still dark enough for the long exposure.
	 */
	frame_time = s5k4ecgx_get_frame_time();
	SENSOR_Sleep(frame_time * 2);
	lux = s5k4ec_lightcheck();
	if (lux > LIGHT_STATUS_LOW_LEVEL) {
		SENSOR_PRINT_HIGH("The image may become over-exposed, limiting EI");
		s5k4ec_I2C_write(s5k4ec_scene_revert_exp);
		s5k4ec_I2C_write(s5k4ec_night_mode_revert_LEI);
		s_current_env = ENVIRONMENT_DIM_FLASH;
		return 1;
	}
	return 0;
}

LOCAL uint32_t _s5k4ec_BeforeSnapshot(uint32_t param)
{
	uint32_t capture_mode = param & 0xffff;
	struct camera_context *cxt = camera_get_cxt();

	SENSOR_PRINT_HIGH("Begin. capture_mode=%d", capture_mode);

	if(1 != capture_mode)
		is_cap = 1;

	// Use the global context to find out whether we are using
	// camera flash or not
	// NOTE: Night and Firework scenes, and HDR don't use flash
	if ((FLASH_CLOSE != s_flash_state) && _s5k4ec_nightscene_flash()) {
		SENSOR_PRINT_HIGH("Flash will be used, not applying any low light tweaks");
	} else if (LIGHT_STATUS_IS_LOW(s5k4ec_lightcheck())) {
		/*
		 * This code block should only run when flash will never be used
		 * in this shot. These settings will allow the shutter
		 * speed to be slower than 1/8th second which will never
		 * guarantee that the flash would cover a complete cycle
		 */
		SENSOR_PRINT_HIGH("Low light environment detected");
		if (s_cur_scene == CAMERA_SCENE_MODE_NIGHT) {
			SENSOR_PRINT_HIGH("Night mode activate");
			s5k4ec_I2C_write(s5k4ec_night_mode_On);
			s_current_env = ENVIRONMENT_DIM;
		} else if (s_cur_scene == CAMERA_SCENE_MODE_DARK) {
			SENSOR_PRINT_HIGH("Dark mode activate");
			s5k4ec_I2C_write(s5k4ec_capture_longer_FPS);
			s_current_env = ENVIRONMENT_LONG_EXPOSURE;
		} else if (s_cur_scene == CAMERA_SCENE_MODE_SPORTS) {
			/*
			 * The Sports scene mode cannot have low light capture tweaks with
			 * or without flash since that's the only mode that's designed
			 * to be a sanic in capturing fast moving objects.
			 * The shutter speed should go fast in that use case.
			 */
			SENSOR_PRINT_HIGH("But still not applying any low light tweaks.");
		} else {
			/*
			 * We won't support flash like what we did with the night mode
			 * Use night mode if they want that.
			 */
			SENSOR_PRINT_HIGH("Low cap mode activate");
			s5k4ec_I2C_write(s5k4ec_capture_med_FPS); // medium exposure (~325 msecs)
			s5k4ec_I2C_write(s5k4ec_low_cap_On);
			s_current_env = ENVIRONMENT_LOW_LIGHT;
		}
	} else { // normal capture
		s5k4ec_I2C_write(s5k4ec_capture_short_FPS); // normal exposure (~125 msecs)
	}

	Sensor_SetMode(capture_mode);
	Sensor_SetMode_WaitDone();

	SENSOR_PRINT_HIGH("s_current_shutter,s_current_gain = %x,%x", s_current_shutter,s_current_gain);
	return SENSOR_SUCCESS;
}
#if 0
LOCAL uint32_t _s5k4ec_check_image_format_support(uint32_t param)
{
	uint32_t ret_val = SENSOR_FAIL;
	SENSOR_PRINT("SENSOR: _s5k4ec_check_image_format_support \n");
	switch (param) {
		case SENSOR_IMAGE_FORMAT_YUV422:
			ret_val = SENSOR_SUCCESS;
			break;
		case SENSOR_IMAGE_FORMAT_JPEG:
			ret_val = SENSOR_SUCCESS;
			break;
		default:
			break;
	}
	return ret_val;
}
#endif
#if 0
LOCAL uint32_t _s5k4ec_pick_out_jpeg_stream(uint32_t param)
{
	uint8_t *p_frame =
	    ((DCAMERA_SNAPSHOT_RETURN_PARAM_T *) param)->return_data_addr;
	uint32_t buf_len =
	    ((DCAMERA_SNAPSHOT_RETURN_PARAM_T *) param)->return_data_len;
	uint32_t i = 0x00;

	SENSOR_PRINT("SENSOR: s5k4ec jpeg capture head: 0x%x, 0x%x \n",
		     *((uint8 *) p_frame), *((uint8 *) p_frame + 1));

	/* Find the tail position */
	for (i = 0x00; i < buf_len; i++)
	{
		#define TAIL_VAL 0xffd9
		uint8_t* p_cur_val = (uint8*)p_frame;

		uint16_t tail_val = ((p_cur_val[i]<<8) | p_cur_val[i+1]);

		if (TAIL_VAL == tail_val)
		{
			i += 2;
			break;
		}
	}

	/* check if the tail is found */
	if (i < buf_len)
	{
		SENSOR_PRINT("SENSOR: s5k4ec Found the jpeg tail at %d: 0x%x 0x%x \n",
		     i + 1, *((uint8 *) p_frame + i),*((uint8 *) p_frame + i + 1));
	}
	else
	{
		SENSOR_PRINT("SENSOR: s5k4ec can not find the jpeg tail: %d \n",i);
		i = 0x00;
	}

	return i;
        SENSOR_PRINT("SENSOR: _s5k4ec_pick_out_jpeg_stream \n");
	return 0;
}
#endif

#if 0
LOCAL uint32_t _s5k4ec_chang_image_format(uint32_t param)
{
	uint32_t ret_val = SENSOR_FAIL;
	SENSOR_REG_TAB_INFO_T st_yuv422_reg_table_info ={ ADDR_AND_LEN_OF_ARRAY(s5k4ec_640X480), 0, 0, 0, 0 };

	switch (param) {
	case SENSOR_IMAGE_FORMAT_YUV422:
		SENSOR_PRINT("SENSOR: s5k4ec  chang_image_format  YUV422 \n");
		ret_val = Sensor_SendRegTabToSensor(&st_yuv422_reg_table_info);
		break;

		case SENSOR_IMAGE_FORMAT_JPEG:
			SENSOR_PRINT("SENSOR: s5k4ec  chang_image_format  jpg \n");
			ret_val = SENSOR_FAIL;//Sensor_SendRegTabToSensor(&st_jpeg_reg_table_info);
			break;

		default:
			break;
	}

	return ret_val;
}
#endif

LOCAL uint32_t _s5k4ec_after_snapshot(uint32_t param)
{
	SENSOR_PRINT_HIGH("=========sonia SENSOR: _s5k4ec_after_snapshot %u",param);
	uint16_t width = 0, height = 0;

	// Revert reg settings regarding flash and disable non Highlight flash
	if (FLASH_CLOSE != s_flash_state)
		s5k4ec_main_flash(0);

	if (s_torch_mode_en) {
		s_torch_mode_en = 0;
		Sensor_SetFlash(FLASH_CLOSE);
	}

	// Reset the low light capture settings as soon as possible.
	switch (s_current_env) {
	case ENVIRONMENT_DIM:
		s5k4ec_I2C_write(s5k4ec_night_mode_Off);
		break;
	case ENVIRONMENT_DIM_FLASH:
		s5k4ec_I2C_write(s5k4ec_night_mode_apply_LEI);
		if (s_cur_scene == CAMERA_SCENE_MODE_NIGHT)
			s5k4ec_I2C_write(s5k4ec_capture_long_FPS);
		break;
	case ENVIRONMENT_LOW_LIGHT:
		s5k4ec_I2C_write(s5k4ec_low_cap_Off);
		break;
	case ENVIRONMENT_LONG_EXPOSURE:
		s5k4ec_I2C_write(s5k4ec_scene_revert_exp);
		break;
	default:
	case ENVIRONMENT_NORMAL:
		break;
	}
	s_current_env = 0;

	// Restore previous sensor mode (which is the preview mode)
	Sensor_SetMode((uint32_t)param);
	Sensor_SetMode_WaitDone();
	is_cap = 0;

	// Retrieve information about the size of the captured image
	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x1D02);
	width = Sensor_ReadReg(0x0F12);
	height = Sensor_ReadReg(0x0F12);
	SENSOR_PRINT_HIGH("width=0x%X(%u), height=0x%X(%u)", width, width, height, height);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_AutoFocusTrig(SENSOR_EXT_FUN_PARAM_T_PTR param_ptr)
{
	struct camera_context *cxt = camera_get_cxt();
	uint32_t rtn = SENSOR_FAIL;
	uint16_t reg_value;
	uint16_t frame_time;
	int8_t flash_mode = cxt->cmr_set.flash;
	int fresh = 1, wait = 5, restart_wait = 0; // for workaround
	int i;

	SENSOR_PRINT_HIGH("Start");

	// Use the global context to find out whether we are using
	// camera flash or not (assuming the autoflash has been
	// reduced to a YES or NO flash question)
	if (flash_mode != FLASH_CLOSE) {
		s5k4ecgx_fast_ae(1);
		s5k4ec_preflash_af(1);
		SENSOR_Sleep(100);

		s5k4ec_wait_until_ae_stable();
		// Lock AWB + AE
		s5k4ec_set_ae_awb_enable(0);
	}

	frame_time = s5k4ecgx_get_frame_time();

	s5k4ec_I2C_write(s5k4ec_single_AF_start);

	/*
	 * 2 frame delay before checking the result of the
	 * first phase, too early or we would get an error
	 * value.
	 */
	SENSOR_Sleep(frame_time * 2);


	for (i = 35; i--;) {
		SENSOR_Sleep(frame_time);
		Sensor_WriteReg(0x002C, 0x7000);
		Sensor_WriteReg(0x002E, 0x2EEE);
		reg_value = Sensor_ReadReg(0x0F12);

		if (0x2 == reg_value) {
			SENSOR_PRINT_HIGH("[1st]AF -Success");
			break;
		} else if ((0x1 == reg_value) && (i > 0)) {
			SENSOR_PRINT_HIGH("[1st]AF -Progress");
			fresh = 0;
			continue;
		}

		/*
		 * According to the old datasheet, the read register values
		 * have a meaning, not all of it are errors.
		 * 0 - Idle AF                  6 - AF Scene Detecting (AE related)
		 * 1 - AF searching             7 - AF Scene Detecting
		 * 2 - AF search success        8 - AF Scene Detecting (AF Window related)
		 * 3 - Low confidence position
		 * 4 - AF was cancelled         *any other value - probably an error
		 */
		if ((reg_value == 6) || (reg_value == 7) || (reg_value == 8) || (reg_value == 0)) {
			/*
			 * Issue: Unknown delay between AF start and progress checks.
			 * Specially after setting the AF window, there would be
			 * some unknown delay. Unfortunately, it also seemed
			 * that it's independent of the frame rate.
			 * AF search will start automatically afterwards.
			 *
			 * Issue: When changing from CAF to AF, it might take more time
			 * to initiate a single AF search causing the reg_value at
			 * 0x70002EEE to be 0. AF search will never start in most cases.
			 */
			if (fresh == 1) {
				if (reg_value) {
					SENSOR_PRINT_ERR("[1st]AF -Detecting (%u)", reg_value);
					continue;
				} else if (wait) {
					if (!(restart_wait--)) {
						SENSOR_PRINT_ERR("[1st]AF -Restart AF");
						s5k4ec_I2C_write(s5k4ec_single_AF_start);
						restart_wait = 2;
					} else {
						SENSOR_PRINT_ERR("[1st]AF -Waiting");
					}
					wait--;
					continue;
				} else {
					SENSOR_PRINT_ERR("[1st]AF -Wait timed out! ");
				}
			} else {
				SENSOR_PRINT_ERR("[1st]AF -Error! (%u)", reg_value);
			}

		} else if (reg_value == 4)  {
			SENSOR_PRINT_ERR("[1st]AF -Cancelled!");
			goto cleanup;
		} else if (reg_value == 3)  {
			// Low Confidence AF search result automatically cancels
			// no need to send the s5k4ec_AF_off_1 reg setting
			SENSOR_PRINT_ERR("[1st]AF -Low confidence result");
			goto cleanup;
		} else if (i > 0) { //etc
			SENSOR_PRINT_ERR("[1st]AF -Error! (%u)", reg_value);
		} else {
			SENSOR_PRINT_ERR("[1st]AF -Timeout!");
		}

		// Reset AF mode setting
		// _s5k4ecgx_set_focus_mode(s_focus_mode);

		// In the stock rom, this reg setting should be
		// enough to reset the AF... should be.
		s5k4ec_I2C_write(s5k4ec_AF_off_1);
		goto cleanup;
	};


	for (i = 60; i--; ) {
		SENSOR_Sleep(frame_time);
		Sensor_WriteReg(0x002C, 0x7000);
		Sensor_WriteReg(0x002E, 0x2207);
		reg_value = Sensor_ReadReg(0x0F12);

		// They say that the least significant byte
		// can be non-zero even on success so ignore it.
		if (0x0 == (reg_value & 0xFF00)) {
			SENSOR_PRINT_HIGH("[2nd]AF -Success");
			break;
		}

		SENSOR_PRINT_HIGH("[2nd]AF -Progress");
	}
	rtn = SENSOR_SUCCESS;

cleanup:
	if (flash_mode != FLASH_CLOSE) {
		s5k4ecgx_fast_ae(0);
		s5k4ec_preflash_af(0);
		s5k4ec_set_ae_awb_enable(1);
	}
	SENSOR_PRINT_HIGH("Done.");

	return rtn;
}

LOCAL uint32_t _s5k4ecgx_set_focus_touch_position(SENSOR_EXT_FUN_PARAM_T_PTR param_ptr)
{
	uint16_t reg_value;
	uint16_t width, height;
	uint16_t outer_window_width, outer_window_height,
		inner_window_width, inner_window_height;
	uint16_t inner_width, inner_height;
	uint16_t inner_x, inner_y, outer_x, outer_y;
	uint16_t touch_x, touch_y;

	width = s_s5k4ec_resolution_Tab_YUV[s_preview_mode].width;
	height = s_s5k4ec_resolution_Tab_YUV[s_preview_mode].height;
	touch_x = param_ptr->zone[0].x;
	touch_y = param_ptr->zone[0].y;

	SENSOR_PRINT_HIGH(
		"Start x=%d, y=%d, w=%d, h=%d, width=%d height=%d",
		param_ptr->zone[0].x,param_ptr->zone[0].y, param_ptr->zone[0].w, param_ptr->zone[0].h,
		width, height
	);

	if (touch_x < 0 || touch_y < 0) {
		SENSOR_PRINT_HIGH("Invalid coordinates, reseting AF window setting");
		_s5k4ecgx_reset_focus_touch_position();
		return 0;
	}

	if (!touch_x && !touch_y && !param_ptr->zone[0].w && !param_ptr->zone[0].h) {
		SENSOR_PRINT_HIGH("Rectangle at (0,0) w=0 and h=0 considered invalid.");
		_s5k4ecgx_reset_focus_touch_position();
		return 0;
	}


	Sensor_WriteReg(0xFCFC, 0xD000);
	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x0298);
	reg_value = Sensor_ReadReg(0x0F12);
	SENSOR_PRINT_HIGH("outer_width : 0x%X(%d)", reg_value, reg_value);
	outer_window_width = (reg_value * width / 1024);

	Sensor_WriteReg(0x002E, 0x029A);
	reg_value = Sensor_ReadReg(0x0F12);
	SENSOR_PRINT_HIGH("outer_height : 0x%X(%d)", reg_value, reg_value);
	outer_window_height = (reg_value * height / 1024);

	// Allow setting a smaller window size
	if (param_ptr->zone[0].w && param_ptr->zone[0].h) {
		SENSOR_PRINT_HIGH("Second search window size will be changed");
		inner_width = (param_ptr->zone[0].w * 1024) / width;
		inner_height = (param_ptr->zone[0].h * 1024) / height;
	} else {
		Sensor_WriteReg(0x002E, 0x02A0);
		inner_width = Sensor_ReadReg(0x0F12);

		Sensor_WriteReg(0x002E, 0x02A2);
		inner_height = Sensor_ReadReg(0x0F12);
	}
	inner_window_width = ((inner_width * width) / 1024);
	inner_window_height = ((inner_height * height) / 1024);
	SENSOR_PRINT_HIGH("inner_width : 0x%X(%d)", inner_width, inner_width);
	SENSOR_PRINT_HIGH("inner_height : 0x%X(%d)", inner_height, inner_height);

	/*
	 * After this code block lies a copy-pasted code to determine the
	 * coordinates of top-left corners of the two AF search windows.
	 * Re-read the previous sentence if you didn't get something was up.
	 *
	 * The coordinates are already given but why calculate it?
	 *
	 * Because, the code assumes that the coordinates given is
	 * not of a top-left corner point but of a geometric center
	 * of some rectangle. We might want to either fix the code
	 * or make the assumption true. The latter is much easier to implement.
	 */
	// When we get nil or 1 dimensional window, treat the coordinates
	// as a center point rather than a top-left corner point.
	if (param_ptr->zone[0].w && param_ptr->zone[0].h) {
		touch_x += param_ptr->zone[0].w/2;
		touch_y += param_ptr->zone[0].h/2;

		// Don't allow unbounded values
		if (touch_x > width)
			touch_x = width - 1;
		if (touch_y > height)
			touch_y = height - 1;
	}

	if (touch_x <= inner_window_width/2) {
		// inner window, outer window should be positive.
		outer_x = 0;
		inner_x = 0;
	} else if (touch_x <= outer_window_width/2) {
		// outer window should be positive.
		inner_x = touch_x - inner_window_width/2;
		outer_x = 0;
	} else if (touch_x >= ((width - 1) - inner_window_width/2)) {
		// inner window, outer window should be less than the preview size
		inner_x = (width - 1) - inner_window_width;
		outer_x = (width - 1) - outer_window_width;
	} else if (touch_x >= ((width -1) - outer_window_width/2)) {
		// outer window should be less than the preview size
		inner_x = touch_x - inner_window_width/2;
		outer_x = (width -1) - outer_window_width;
	} else {
		// touch_x is not in a corner, so set using touch point.
		inner_x = touch_x - inner_window_width/2;
		outer_x = touch_x - outer_window_width/2;
	}

	if (touch_y <= inner_window_height/2) {
		// inner window, outer window should be positive.
		outer_y = 0;
		inner_y = 0;
	} else if (touch_y <= outer_window_height/2) {
		// outer window should be positive.
		inner_y = touch_y - inner_window_height/2;
		outer_y = 0;
	} else if (touch_y >= ((height - 1) - inner_window_height/2)) {
		// inner window, outer window should be less than the preview size
		inner_y = (height - 1) - inner_window_height;
		outer_y = (height - 1) - outer_window_height;
	} else if (touch_y >= ((height - 1) - outer_window_height/2)) {
		// outer window should be less than the preview size
		inner_y = touch_y - inner_window_height/2;
		outer_y = (height - 1) - outer_window_height;
	} else {
		// touch_y is not in a corner, so set using touch point.
		inner_y = touch_y - inner_window_height/2;
		outer_y = touch_y - outer_window_height/2;
	}

	if (!outer_x) outer_x = 1;
	if (!outer_y) outer_y = 1;
	if (!inner_x) inner_x= 1;
	if (!inner_y) inner_y= 1;

	SENSOR_PRINT_HIGH("touch position(%d, %d), preview size(%d, %d)",
			touch_x, touch_y, width, height);
	SENSOR_PRINT_HIGH("point first(%d, %d), second(%d, %d)",
			outer_x, outer_y, inner_x, inner_y);

	Sensor_WriteReg(0xFCFC, 0xD000);
	Sensor_WriteReg(0x0028, 0x7000);

	Sensor_WriteReg(0x002A, 0x0294);       //AF window setting
	Sensor_WriteReg(0x0F12, outer_x * 1024 / width);       //REG_TC_AF_FstWinStartX
	Sensor_WriteReg(0x0F12, outer_y * 1024 / height);       //REG_TC_AF_FstWinStartY

	Sensor_WriteReg(0x002A, 0x029C);       //AF window setting
	Sensor_WriteReg(0x0F12, inner_x * 1024 / width);       //REG_TC_AF_ScndWinStartX
	Sensor_WriteReg(0x0F12, inner_y * 1024 / height);       //REG_TC_AF_ScndWinStartY

	Sensor_WriteReg(0x002A, 0x02A0);       //AF window setting
	Sensor_WriteReg(0x0F12, inner_width);  //REG_TC_AF_ScndWinSizeX
	Sensor_WriteReg(0x0F12, inner_height); //REG_TC_AF_ScndWinSizeY

	Sensor_WriteReg(0x002A, 0x02A4);       //AF window setting
	Sensor_WriteReg(0x0F12, 0x0001);       //REG_TC_AF_WinSizesUpdated

	SENSOR_PRINT_HIGH("REG_TC_AF_FstWinStartX 0x%04X(%d), REG_TC_AF_FstWinStartY 0x%04X(%d),",
			outer_x * 1024 / width, outer_x * 1024 / width,
			outer_y * 1024 / width, outer_y * 1024 / width);
	SENSOR_PRINT_HIGH("REG_TC_AF_ScndWinStartX 0x%04X(%d), REG_TC_AF_ScndWinStartY 0x%04X(%d),",
			inner_x * 1024 / width, inner_x * 1024 / width,
			inner_y * 1024 / width, inner_y * 1024 / width);

	s_af_wnd_has_changed = 1;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ecgx_reset_focus_touch_position(void)
{
	SENSOR_PRINT_HIGH("Reset AF Window setting");

	if (!s_af_wnd_has_changed){
		SENSOR_PRINT_HIGH("Window hasn't been changed");
		return SENSOR_SUCCESS;
	}

	Sensor_WriteReg(0xFCFC, 0xD000);
	Sensor_WriteReg(0x0028, 0x7000);
	Sensor_WriteReg(0x002A, 0x0294);//AF window setting
	Sensor_WriteReg(0x0F12, 0x0100);//294 //REG_TC_AF_FstWinStartX
	Sensor_WriteReg(0x0F12, 0x00E3);//296 //REG_TC_AF_FstWinStartY
	Sensor_WriteReg(0x0F12, 0x0200);//298 //REG_TC_AF_FstWinSizeX
	Sensor_WriteReg(0x0F12, 0x0238);//29A //REG_TC_AF_FstWinSizeY
	Sensor_WriteReg(0x0F12, 0x01C6);//29C //REG_TC_AF_ScndWinStartX
	Sensor_WriteReg(0x0F12, 0x0166);//29E //REG_TC_AF_ScndWinStartY
	Sensor_WriteReg(0x0F12, 0x0074);//2A0 //REG_TC_AF_ScndWinSizeX
	Sensor_WriteReg(0x0F12, 0x0132);//2A2 //REG_TC_AF_ScndWinSizeY
	Sensor_WriteReg(0x0F12, 0x0001);//2A4 //REG_TC_AF_WinSizesUpdated
	s_af_wnd_has_changed = 0;
	SENSOR_PRINT_HIGH("Done.");

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_StartAutoFocus(uint32_t param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr = (SENSOR_EXT_FUN_PARAM_T_PTR) param;
	SENSOR_PRINT_HIGH("Start. param = %d", ext_ptr->param);

	switch (ext_ptr->param) {
	case SENSOR_EXT_FOCUS_TRIG:
	case SENSOR_EXT_FOCUS_MACRO:
		s5k4ec_low_light_AF_check();
		// This function usualy is activated when doing AF mode is macro
		// But some camera apps doesn't even expose changing AF modes...
		_s5k4ecgx_set_focus_touch_position(ext_ptr);

		rtn |= _s5k4ec_AutoFocusTrig(ext_ptr);
		break;
	case SENSOR_EXT_FOCUS_CAF:
	case SENSOR_EXT_FOCUS_MULTI_ZONE:
	case SENSOR_EXT_FOCUS_ZONE:
	case SENSOR_EXT_FOCUS_CHECK_AF_GAIN:
	default:
		break;
	}
	SENSOR_PRINT_HIGH("Done.");
	return rtn;
}

/*
 * Calculates the exposure and gain for an HDR step using
 * the previous AE and ISO settings. This doesn't do anything
 * in the first steps and lets the camera sensor auto adjust its
 * Exposure and ISO settings.
 *
 * Second step doubles both values. In the Third step,
 * the values are the 2.5x of the original values.
 */
LOCAL uint32_t _s5k4ec_SetEV(uint32_t param)
{
#define MAX_A_D_GAIN (8 << 8) //max combined analog*digital gain, 8x (~400ISO)
#define MAX_EV_TIME (2000 * 100) // max exposure time, 2000ms (2 seconds)

	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr = (SENSOR_EXT_FUN_PARAM_T_PTR) param;
	uint16_t f_gain = 0;
	uint32_t f_ev = 0;

	static int8_t restore_needed = 0;
	static int32_t ev;
	static int32_t gain;
	static int32_t manual_ev;
	static int32_t manual_gain;
	static int16_t auto_algorithm_en; // REG_TC_DBG_AutoAlgEnBits

	SENSOR_PRINT_HIGH("param: 0x%x", ext_ptr->param);

	switch(ext_ptr->param) {
	case SENSOR_HDR_EV_LEVE_0:
		SENSOR_PRINT_HIGH("Capture normal image first...");
		// We do nothing at this state, just return

		// Increase possible framerate for the capture mode
		s5k4ec_I2C_write(s5k4ec_capture_hdr_FPS);

		if ( restore_needed != 0)
			SENSOR_PRINT_HIGH("Backed up register values may have not been restored");
		restore_needed = 0;

		return SENSOR_SUCCESS;
	case SENSOR_HDR_EV_LEVE_1:
		if (!restore_needed) { // as an HDR step
			SENSOR_PRINT_HIGH("Backup AE, EV and Gain settings");
			// Calculate Exposure time
			Sensor_WriteReg(0xFCFC, 0xD000);
			Sensor_WriteReg(0x002C, 0x7000);
			Sensor_WriteReg(0x002E, 0x2C28);
			ev = Sensor_ReadReg(0x0F12);
			ev += Sensor_ReadReg(0x0F12) << 16;
			ev = ev >> 2;
			SENSOR_PRINT_HIGH("ev=0x%X(%f)\n", ev, (float)ev / 100);

			// Calculate sensor Gains
			// NOTE: Analog and Digital gains are in 8.8 fixed point numbers
			Sensor_WriteReg(0x002C, 0x7000);
			Sensor_WriteReg(0x002E, 0x2BC4);
			gain = Sensor_ReadReg(0x0F12); //A gain
			gain = gain * Sensor_ReadReg(0x0F12); //D gain
			// NOTE: By this line, gain is a 16.16 fixed-point number
			// But the sensor only reads 8.8 values
			// We need to reduce its precision.
			// formula: let x=>16.16 number, y=>temp variable, z=>result
			//          y = x / (2 ^ 16) // convert to real number
			//          z = y * (2 ^ 8)  // convert back to 8.8 fp number
			//          z = x / (2 ^ 8)  // Simplified form
			gain =  gain >> 8;
			// CAUTION: If this gain value goes under 1, you may want
			//          to check why that happens.
			SENSOR_PRINT_HIGH("gain=0x%X(%f)\n", gain, (float)gain / 256);

			// Backup auto algorithm switches
			Sensor_WriteReg(0x002C, 0x7000);
			Sensor_WriteReg(0x002E, 0x04E6);
			auto_algorithm_en = Sensor_ReadReg(0x0F12);
			SENSOR_PRINT_HIGH("AutoAlgEn=0x%X\n", auto_algorithm_en);

			// Backup manual EV and Gain
			Sensor_WriteReg(0x002C, 0x7000);
			Sensor_WriteReg(0x002E, 0x04AC);
			manual_ev = Sensor_ReadReg(0x0F12);
			manual_ev |= Sensor_ReadReg(0x0F12) << 16;
			Sensor_WriteReg(0x002E, 0x04B2);
			manual_gain = Sensor_ReadReg(0x0F12);
			SENSOR_PRINT_HIGH("m_gain=0x%X m_ev=0x%X\n", manual_gain, manual_ev);

			SENSOR_PRINT_HIGH("Backup Done, this will be restored later.");

			// Disable LEI adjustments and AE algorithms
			Sensor_WriteReg(0x0028, 0x7000);
			Sensor_WriteReg(0x002A, 0x04E6);
			Sensor_WriteReg(0x0F12, auto_algorithm_en & 0xFFF9);

			f_gain = gain * 2.0;
			f_ev = ev * 2.0;
		} else {
			SENSOR_PRINT_HIGH("HDR has finished, will restore values now.");
			// Restore auto algorithm switches
			Sensor_WriteReg(0x0028, 0x7000);
			Sensor_WriteReg(0x002A, 0x04E6);
			Sensor_WriteReg(0x0F12, auto_algorithm_en);

			// Restore defaults
			f_gain = manual_gain;
			f_ev = manual_ev;
			// Skip sanity checks and go directly set the backed up values
			goto _do_set;
		}

		restore_needed = !restore_needed;
		break;
	case SENSOR_HDR_EV_LEVE_2:
		f_gain = gain * 2.5;
		f_ev = ev * 2.5;

		break;
	default:
		SENSOR_PRINT_ERR("Undefined parameter level");
		return 0;
	}

	if (f_gain > MAX_A_D_GAIN) {
		SENSOR_PRINT_ERR("Maximum ISO level reached, extending exposure time");
		f_ev = (uint32_t)((float)(f_gain / MAX_A_D_GAIN) * f_ev);
		f_gain = MAX_A_D_GAIN;
	}

	f_gain = (f_gain > MAX_A_D_GAIN) ? MAX_A_D_GAIN : (!f_gain ? 1 : f_gain);
	f_ev = (f_ev > MAX_EV_TIME) ? MAX_EV_TIME : (!f_ev ? 1 : f_ev);

	SENSOR_PRINT_HIGH("f_gain=0x%X(%f)\n", f_gain, (float)f_gain / 256);
	SENSOR_PRINT_HIGH("f_ev=0x%X(%f msec) \n", f_ev, (float)f_ev / 100);

_do_set:
	Sensor_WriteReg(0x002A, 0x04AC);
	Sensor_WriteReg(0x0F12, (int16_t) (f_ev & 0xFFFF));   // REG_SF_USER_Exposure
	Sensor_WriteReg(0x0F12, (int16_t) (f_ev >> 16));      // REG_SF_USER_ExposureHigh
	Sensor_WriteReg(0x0F12, 0x0001);         // REG_SF_USER_ExposureChanged
	Sensor_WriteReg(0x0F12, f_gain);         // REG_SF_USER_TotalGain
	Sensor_WriteReg(0x0F12, 0x0001);         // REG_SF_USER_TotalGainChanged

	return rtn;
}

LOCAL uint32_t _s5k4ec_ExtFunc(uint32_t ctl_param)
{
	uint32_t rtn = SENSOR_SUCCESS;

	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr =(SENSOR_EXT_FUN_PARAM_T_PTR) ctl_param;
	SENSOR_PRINT_HIGH("cmd:0x%x", ext_ptr->cmd);

	switch (ext_ptr->cmd)
	{
	 case SENSOR_EXT_FUNC_INIT:
		// rtn = _s5k4ec_init_firmware(ctl_param);
		break;
	 case SENSOR_EXT_FOCUS_START:
		rtn = _s5k4ec_StartAutoFocus(ctl_param);
		break;
	case SENSOR_EXT_EV:
		rtn = _s5k4ec_SetEV(ctl_param);
		break;
	case SENSOR_EXT_EXPOSURE_START:
	default:
		    break;
	}

	return rtn;
}

LOCAL uint32_t _s5k4ec_StreamOn(__attribute__((unused)) uint32_t param)
{
	SENSOR_PRINT_HIGH("SENSOR:Start s5k4ec_steamon 1613");
	int16_t error;

	if (1 != is_cap) {
		SENSOR_PRINT_HIGH("zxdbg preview stream on");
		Sensor_WriteReg(0x002C, 0x7000);
		Sensor_WriteReg(0x002E, 0x026C); // REG_TC_GP_ErrorPrevConfig
		error = Sensor_ReadReg(0x0F12);

		if (error) {
			SENSOR_PRINT_HIGH("Preview Stream error 0x%04X: %s", error, s5k4ec_StreamStrerror(error));

			// Attempt to recover some errors
			if (error ==  0x06) {
				SENSOR_PRINT_HIGH("Attempting to set Framerate to auto.");
				s_target_max_fps = 0;
#ifdef WA_LIMIT_HD_CAM_FPS
				s5k4ec_set_FPS(s_target_max_fps);
#endif
			}
		}
		/*
		 * NOTE: There is no reason to manually activate the
		 * preview stream, even when changing resolutions
		 * since the REG_TC_GP_PrevOpenAfterChange register
		 * will open it when it's set to 1.
		 */
		// s5k4ec_I2C_write(s5k4ec_preview_Stream_On);
		// SENSOR_Sleep(10);


		if ((s_cur_scene != CAMERA_SCENE_MODE_BEACH) ||
		    (s_cur_scene != CAMERA_SCENE_MODE_FIREWORK) ||
		    (s_cur_scene != CAMERA_SCENE_MODE_PARTY) ||
		    (s_cur_scene != CAMERA_SCENE_MODE_SPORTS)) {
			SENSOR_PRINT_HIGH("workaround: Reapplying ISO settings");
			_s5k4ec_set_iso(s_ISO_mode);
		}

#if defined(WA_BOOST_DDR_FREQ_720P) || defined(WA_LIMIT_HD_CAM_FPS)
		s_fps_cur_mode = FPS_MODE_INVALID;

#ifdef WA_BOOST_DDR_FREQ_720P
		if (s_hd_applied) {
			if (s_ddr_boosted == 0) {
				SENSOR_PRINT_HIGH("workaround: Increasing DDR freq for 720p recording");
				if (s5k4ec_ddr_is_slow(1))
					SENSOR_PRINT_HIGH("Failed to apply workaround");
			}
		}
#endif

#ifdef WA_LIMIT_HD_CAM_FPS
		if (s_fps_limit &&
		    (!s_target_max_fps || (s_target_max_fps > s_fps_limit))) {
			SENSOR_PRINT_HIGH("workaround: limiting maximum FPS to %d", s_fps_limit);
			if (!s_target_max_fps) // Auto FPS is requested
				s5k4ec_set_manual_FPS(0, s_fps_limit);
			else
				s5k4ec_set_manual_FPS(s_fps_limit, s_fps_limit);
		}
#endif
#endif

		// Apply the settings that tweaks the maximum Exposure time
		// only for the scenes that don't change it.
		if (s_cur_scene != CAMERA_SCENE_MODE_NIGHT &&
		    s_cur_scene != CAMERA_SCENE_MODE_DARK &&
		    s_cur_scene != CAMERA_SCENE_MODE_FIREWORK &&
		    s_fps_cur_mode == FPS_MODE_INVALID) {
			s5k4ec_set_FPS(s_target_max_fps);
		}

		/*
		 * Force unlock both AE and AWB on Stream On
		 * Sometimes the AE/AWB lock gets stuck without being released
		 * Due to some unforeseeable events (e.g. errors, OOM killer).
		 */
		s5k4ec_set_ae_awb_enable(1);
	} else {
		SENSOR_PRINT_HIGH("zxdbg capture stream on");

		s5k4ec_I2C_write(s5k4ec_capture_start);

		Sensor_WriteReg(0x002C, 0x7000);
		Sensor_WriteReg(0x002E, 0x0272);
		error = Sensor_ReadReg(0x0F12);

		// Ignore PrevConfigIdxTooHigh since that doesn't make sense in capture mode
		if (error && error != 1)
			SENSOR_PRINT_HIGH("Capture Stream error 0x%04X: %s", error, s5k4ec_StreamStrerror(error));

	}

	s_stream_is_on = 1;
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_StreamOff(__attribute__((unused)) uint32_t param)
{
	struct camera_context *cxt = camera_get_cxt();

	SENSOR_PRINT_HIGH("Stop");

#ifdef WA_BOOST_DDR_FREQ_720P
	if (s_ddr_boosted)
		s5k4ec_ddr_is_slow(0);
#endif

	s_flash_state = cxt->cmr_set.flash;
	if (s_cur_scene == CAMERA_SCENE_MODE_NIGHT ||
	    s_cur_scene == CAMERA_SCENE_MODE_DARK ||
	    s_cur_scene == CAMERA_SCENE_MODE_FIREWORK ||
	    s_cur_scene == CAMERA_SCENE_MODE_HDR) {

		if (cxt->cmr_set.flash != FLASH_CLOSE) {
			cxt->cmr_set.flash = FLASH_CLOSE;
			SENSOR_PRINT_HIGH("Flash is forced-disabled on the current scene mode.");
		}
	} else if ((FLASH_CLOSE != s_flash_state) && s_stream_is_on) {
		s5k4ec_main_flash(1);
	}

	/*
	 * Work around: Disable Capture mode as Soon As Possible
	 * With long exposures, this becomes more apparent that
	 * the camera wastes a frame of capture after the initial one.
	 */
	if (is_cap && s_stream_is_on)
		s5k4ec_I2C_write(s5k4ec_preview_return);

	/*
	 * Work around: Allow setting the scene mode again
	 * Some camera apps apply the scene mode twice (!?)
	 * Before and After applying user settings.
	 */
	s_force_set_scene = 1;

	s_stream_is_on = 0;
	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_I2C_write(SENSOR_REG_T* sensor_reg_ptr)
{
	uint16_t i;
	SENSOR_REG_TAB_INFO_T infotab = {};

	for(i = 0; (0xFFFF != sensor_reg_ptr[i].reg_addr) || (0xFFFF != sensor_reg_ptr[i].reg_value) ; i++)
		   ;

	infotab.sensor_reg_tab_ptr = sensor_reg_ptr;
	infotab.reg_count = i;
	SENSOR_PRINT_HIGH("count = %d", i);
	Sensor_SendRegTabToSensor(&infotab);


	return 0;
}

LOCAL uint32_t s5k4ec_set_Metering(uint32_t metering_mode)
{
	const char *str = "";

	if(metering_mode >= CAMERA_AE_MODE_MAX) {
		SENSOR_PRINT_ERR("Undefined Metering mode %u", metering_mode);
		return SENSOR_OP_PARAM_ERR;
	}

	switch(metering_mode) {
		case CAMERA_AE_FRAME_AVG:
			s5k4ec_I2C_write(s5k4ec_metering_matrix);
			str = "Matrix";
			break;
		case CAMERA_AE_CENTER_WEIGHTED:
			s5k4ec_I2C_write(s5k4ec_metering_center_weighted);
			str = "Center Weighted";
			break;
		case CAMERA_AE_SPOT_METERING:
			s5k4ec_I2C_write(s5k4ec_metering_spot);
			str = "Spot";
			break;
	}

	SENSOR_PRINT_HIGH("Apply %s Metering mode ", str);
	s_metering_mode = metering_mode;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_set_sharpness(uint32_t level)
{
	if(level >= 7) {
		SENSOR_PRINT_ERR("Undefined Sharpness level %u", level);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Sharpness level %u", level);

	switch(level) {
	case 0:
		s5k4ec_I2C_write(s5k4ec_sharpness_minus_3);
		break;
	case 1:
		s5k4ec_I2C_write(s5k4ec_sharpness_minus_2);
		break;
	case 2:
		s5k4ec_I2C_write(s5k4ec_sharpness_minus_1);
		break;
	case 4:
		s5k4ec_I2C_write(s5k4ec_sharpness_plus_1);
		break;
	case 5:
		s5k4ec_I2C_write(s5k4ec_sharpness_plus_2);
		break;
	case 6:
		s5k4ec_I2C_write(s5k4ec_sharpness_plus_3);
		break;
	default:
	case 3:
		s5k4ec_I2C_write(s5k4ec_sharpness_default);
		break;
	}

	s_sharpness_lvl = (int32_t) level;
	return SENSOR_SUCCESS;

}

LOCAL uint32_t s5k4ec_lightcheck()
{
	uint16_t low_word = 0;
	uint16_t high_word = 0;

	Sensor_WriteReg(0xFCFC,0xD000);
	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x2C18);
	low_word = Sensor_ReadReg(0x0F12);
	high_word = Sensor_ReadReg(0x0F12);

	SENSOR_PRINT_HIGH("Luminance results high=%x low=%x", high_word, low_word);

	return low_word | (high_word <<16);

}

LOCAL uint32_t s5k4ec_flash(uint32_t param)
{
	uint32_t *autoflash = (uint32_t *)param;
	struct camera_context *cxt = camera_get_cxt();
	uint32_t lux;

	SENSOR_PRINT_HIGH("Start");

	lux = s5k4ec_lightcheck();
	if (LIGHT_STATUS_IS_LOW(lux)) {
		SENSOR_PRINT_HIGH("Low light, may use flash");
		(*autoflash) = 1;
	} else {
		SENSOR_PRINT_HIGH("Normal light levels, not using flash");
	}

	SENSOR_PRINT_HIGH("Done.");

	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_get_ISO_rate(void)
{
	uint16_t iso_a_gain = 0;
	uint16_t iso_d_gain = 0;
	uint32_t iso_gain, iso_rate;

	SENSOR_PRINT_HIGH("Get ISO gain");

	// These registers output a fixed point number, 8.8 format
	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x2BC4);
	iso_a_gain = Sensor_ReadReg(0x00F12); // Mon_AAIO_PrevAcqCtxt_ME_AGain
	iso_d_gain = Sensor_ReadReg(0x00F12); // Mon_AAIO_PrevAcqCtxt_ME_DGain


	iso_gain = (iso_a_gain * iso_d_gain) / 400 /*384*/ /*200*/;

	if (!iso_gain) {
		SENSOR_PRINT_ERR("Failed. [ISO rate: 50 gain: %u]", iso_gain);
		return 50;
	}

	/* Convert ISO value */
	if(iso_gain > 0x400) {
		iso_rate = 400;
	} else if(iso_gain > 0x200) {
		iso_rate = 200;
	} else if(iso_gain > 0x100) {
		iso_rate = 100;
	} else {
		iso_rate = 50;
	}

	SENSOR_PRINT_ERR("Analog gain: %f Digital Gain: %f]", (float) iso_a_gain/256, (float) iso_d_gain/256);
	SENSOR_PRINT_ERR("Multiplied Gain: %f]", (float)(iso_a_gain * iso_d_gain) / (65536));
	SENSOR_PRINT_ERR("Effective ISO value: %f, Sensor ISO value: %f", ((float)(iso_a_gain * iso_d_gain) / (65536)) * 50, ((float)iso_a_gain/256) * 50);

	SENSOR_PRINT_HIGH("Done. [Simplified ISO rate:%u A*Dgain: %u]", iso_rate, iso_gain);
	return ((iso_a_gain * iso_d_gain * 50) / (65536));
}

LOCAL uint32_t s5k4ec_get_shutter_speed(void)
{
	uint16_t lsb, msb;
	uint32_t exposure_time;

	SENSOR_PRINT_HIGH("Get Shutter speed or exposure time");

	Sensor_WriteReg(0xFCFC, 0xD000);
	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x2BC0);
	lsb = Sensor_ReadReg(0x00F12);
	msb = Sensor_ReadReg(0x00F12);

	/*
	 * Formula:
	 * x = ((msb << 16) | lsb) / 400
	 * shutter_speed = 1000 / x
	 */
	exposure_time = ((msb << 16) | lsb);

	if (!exposure_time) {
		SENSOR_PRINT_HIGH("Failed, sensor values are 0");
		return 0;
	}

	SENSOR_PRINT_HIGH("Done. Shutter Speed : %u [1/400 msec] (%f msec)", exposure_time, (float)exposure_time/400);
	return exposure_time;
}

LOCAL uint16_t s5k4ecgx_get_frame_time()
{
	uint16_t frame_time = 0;
	uint16_t msb, lsb;
	int err;

	SENSOR_PRINT_HIGH("Start");

	Sensor_WriteReg(0xFCFC, 0xD000);
	Sensor_WriteReg(0x002C, 0x7000);

	Sensor_WriteReg(0x002E, 0x2128);
	lsb = Sensor_ReadReg (0x0F12);
	msb = Sensor_ReadReg (0x0F12);

	frame_time = (lsb | (msb << 16)) / 400;

	SENSOR_PRINT_HIGH("Done. [Frame Time: lsb 0x%02X msb 0x%02X]", lsb , msb);
	SENSOR_PRINT_HIGH("Done. [Frame Time: %u]",frame_time);

	return frame_time;
}

LOCAL uint32_t __s5k4ecgx_set_focus_mode(uint32_t mode)
{
	uint16_t delay;

	if (mode > 5) {
		SENSOR_PRINT_ERR("Undefined Focus mode %u", mode);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Focus Mode %u", mode);

	delay = s5k4ecgx_get_frame_time();

	if ((mode != s_focus_mode) && ((s_focus_mode != 5) || (s_focus_mode != 4)))
		s5k4ec_I2C_write(s5k4ec_AF_revert_continuous_mode);


	switch(mode) {
	case 2: // Macro
		// Cancel CAF first
		if ((s_focus_mode != 5) || (s_focus_mode != 4)) {
			s5k4ec_I2C_write(s5k4ec_AF_normal_mode_2);
			SENSOR_Sleep(delay);
		}

		s5k4ec_I2C_write(s5k4ec_AF_macro_mode_1);
		SENSOR_Sleep(delay);

		s5k4ec_I2C_write(s5k4ec_AF_macro_mode_2);
		SENSOR_Sleep(delay);

		if ((s_cur_scene != CAMERA_SCENE_MODE_NIGHT) &&
		    (s_cur_scene != CAMERA_SCENE_MODE_DARK))
			s5k4ec_I2C_write(s5k4ec_AF_macro_mode_3);
		break;
	case 3: // Infinity
		s5k4ec_I2C_write(s5k4ec_AF_off_1);
		break;
	case 5: // Continuous AF for video
		// Reset AF Window
		_s5k4ecgx_reset_focus_touch_position();
		/* Fall-through */
	case 4: // Continuous AF for picture
		s5k4ec_I2C_write(s5k4ec_AF_continuous_mode_1);
		SENSOR_Sleep(delay);

		if ((s_cur_scene != CAMERA_SCENE_MODE_NIGHT) &&
		    (s_cur_scene != CAMERA_SCENE_MODE_DARK))
			s5k4ec_I2C_write(s5k4ec_AF_continuous_mode_2);

		s5k4ec_I2C_write(s5k4ec_continuous_AF_start);
		SENSOR_Sleep(2 * delay);

		break;
	// For everything else, use the normal AF
	default:
		s5k4ec_I2C_write(s5k4ec_AF_normal_mode_1);
		SENSOR_Sleep(delay);

		s5k4ec_I2C_write(s5k4ec_AF_normal_mode_2);
		SENSOR_Sleep(delay);

		if ((s_cur_scene != CAMERA_SCENE_MODE_NIGHT) &&
		    (s_cur_scene != CAMERA_SCENE_MODE_DARK))
			s5k4ec_I2C_write(s5k4ec_AF_normal_mode_3);
		break;
	}

	s_focus_mode = mode;
	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ecgx_set_focus_mode(uint32_t mode)
{
	if (s_focus_mode == mode) {
		SENSOR_PRINT_HIGH("focus mode (%u) has already been applied", mode);
		return SENSOR_SUCCESS;
	}
	return __s5k4ecgx_set_focus_mode(mode);
}
#define AE_STABLE_SEARCH_COUNT 50
/*
 * This function takes from what the black box tests results shows.
 * There are two modes for all 4 possible states:
 * mode 1: Maintain a read value of 1 until the end of phase 1
 *         and still get a 1 on phase 2.
 * mode 2: Fail to maintain a read value of 1 until the end of phase 1
 *         but would get a at least a single 1 on phase 2
 * The two other state may mean that the AE has failed to become stable.
 */

LOCAL uint32_t s5k4ec_wait_until_ae_stable()
{
	uint16_t reg_value;
	Sensor_WriteReg(0xFCFC, 0xD000);

	SENSOR_PRINT_HIGH("Waiting for AE to become stable");

	for (int ph = 0 ; ph <= 1; ph++) {
		SENSOR_PRINT_HIGH("Phase %d", ph + 1);
		for (int i = 0 ; i < AE_STABLE_SEARCH_COUNT; i++) {
			Sensor_WriteReg(0x002C, 0x7000);
			Sensor_WriteReg(0x002E, 0x2C74);

			reg_value = Sensor_ReadReg(0x0F12);
			if(ph == reg_value) {
				SENSOR_PRINT_HIGH("Phase %d success", ph + 1);
				break;
			}

			SENSOR_PRINT_HIGH("Recheck AE status: %u, expected %d", reg_value, ph);

			if (s_fast_ae_en)
				SENSOR_Sleep(1);
			else
				SENSOR_Sleep(10);
		}
	}
	SENSOR_PRINT_HIGH("Done.");
	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_preflash_af(uint32_t on)
{
	SENSOR_PRINT_HIGH("Turn %s Pre Flash setting", on ? "On" : "Off");
	if (on)
		s5k4ec_I2C_write(s5k4ec_pre_flash_On);
	else
		s5k4ec_I2C_write(s5k4ec_pre_flash_Off);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ecgx_fast_ae(uint32_t on)
{
	SENSOR_PRINT_HIGH("Turn %s Fast AE mode", on ? "On" : "Off");
	if (on)
		s5k4ec_I2C_write(s5k4ec_FAST_AE_On);
	else
		s5k4ec_I2C_write(s5k4ec_FAST_AE_Off);
	s_fast_ae_en = on;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_main_flash(uint32_t on)
{
	SENSOR_PRINT_HIGH("Turn %s Main Flash setting", on ? "On" : "Off");
	if (on)
		s5k4ec_I2C_write(s5k4ec_main_flash_On);
	else
		s5k4ec_I2C_write(s5k4ec_main_flash_Off);

	return SENSOR_SUCCESS;
}

/*
 * Low light AF - Reduce AF lens posiiton table
 * The AF algorithm requires a number of the actual preview frames
 * to compute its sharpness statistics.
 * On low light conditions, preview framerate tends to be reduced,
 * thus causing a longer AF search time.
 *
 * Low light AF will almost halve the table which should give
 * some considerable speed up on the aforementioned scenario.
 */
LOCAL uint32_t s5k4ec_low_light_AF_check(void)
{
	struct camera_context *cxt = camera_get_cxt();
	uint8_t flash_mode = cxt->cmr_set.flash;
	uint8_t use_ll_af = 0;

	SENSOR_PRINT_HIGH("Decide whether to use low light AF or not");

	if ((s_cur_scene == CAMERA_SCENE_MODE_NIGHT) ||
	    (s_cur_scene == CAMERA_SCENE_MODE_DARK) ){
		SENSOR_PRINT_HIGH("Night Scene mode only uses low light AF.");
		return SENSOR_SUCCESS;
	}

	if ((flash_mode == FLASH_CLOSE) && (s5k4ecgx_get_frame_time() > 67))
		use_ll_af = 1;
	else
		use_ll_af = 0;

	if (use_ll_af == s_using_low_light_af) {
		SENSOR_PRINT_HIGH(
			"Sensor already%s using Low light autofocus",
			use_ll_af ? "" : " not"
		);
		return SENSOR_SUCCESS;
	} else {
		SENSOR_PRINT_HIGH("Will %s low light autofocus", use_ll_af ? "apply" : "revert");
	}

	if (use_ll_af) {
		s5k4ec_I2C_write(s5k4ec_AF_low_light_mode_On);

		// Reverse the direction when on Macro mode
		if (s_focus_mode == 2) // Macro
			s5k4ec_I2C_write(s5k4ec_AF_low_light_macro_mode);
	} else {
		if (s_focus_mode == 2) { // Macro
			s5k4ec_I2C_write(s5k4ec_AF_macro_mode_3);
		} else if ((s_focus_mode == 5) || (s_focus_mode == 4)) { // CAF
			s5k4ec_I2C_write(s5k4ec_AF_continuous_mode_2);
		} else { // Normal AF
			s5k4ec_I2C_write(s5k4ec_AF_normal_mode_3);
		}
	}

	SENSOR_PRINT_HIGH("Done.");
	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_set_FPS_mode(uint32_t fps_mode)
{
	SENSOR_PRINT_HIGH("Apply FPS mode %d", fps_mode);

	switch(fps_mode) {
	case FPS_MODE_AUTO:
		s5k4ec_I2C_write(s5k4ec_Auto30_FPS);
		break;
	case FPS_MODE_7:
		s5k4ec_I2C_write(s5k4ec_7_FPS);
		break;
	case FPS_MODE_12:
		s5k4ec_I2C_write(s5k4ec_12_FPS);
		break;
	case FPS_MODE_15:
		s5k4ec_I2C_write(s5k4ec_15_FPS);
		break;
	case FPS_MODE_24:
		s5k4ec_I2C_write(s5k4ec_24_FPS);
		break;
	case FPS_MODE_25:
		s5k4ec_I2C_write(s5k4ec_25_FPS);
		break;
	case FPS_MODE_30:
		s5k4ec_I2C_write(s5k4ec_30_FPS);
		break;
	default:
		SENSOR_PRINT_ERR("Undefined FPS mode %u", fps_mode);
		return SENSOR_FAIL;
	}
	s_fps_cur_mode = fps_mode;

	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_set_FPS(uint32_t fps)
{
	uint32_t mode;

	if (0 == fps) { /* Auto FPS */
		mode = FPS_MODE_AUTO;
	} else if (7 >= fps) {
		mode = FPS_MODE_7;
	} else if (12 >= fps) {
		mode = FPS_MODE_12;
	} else if (15 >= fps) {
		mode = FPS_MODE_15;
	} else if (24 >= fps) {
		mode = FPS_MODE_24;
	} else if (25 >= fps) {
		mode = FPS_MODE_25;
	} else if (30 >= fps) {
		mode = FPS_MODE_30;
	} else {
		SENSOR_PRINT_HIGH("Unsupported fps %u", fps);
		mode = FPS_MODE_INVALID; /* An invalid mode */
	}

	return s5k4ec_set_FPS_mode(mode);
}
#ifdef WA_LIMIT_HD_CAM_FPS
LOCAL uint32_t s5k4ec_set_manual_FPS(uint32_t min, uint32_t max)
{
	uint16_t FrRateQualityType;
	uint16_t FrRateType;
	static SENSOR_REG_T manual_setting[] = {
		{0x0028, 0x7000},

		{0x002A, 0x02BE},
		{0x0F12, 0x0000}, //REG_0TC_PCFG_usFrTimeType//2
		{0x0F12, 0x0000}, //REG_0TC_PCFG_FrRateQualityType//3
		{0x0F12, 0x0000}, //REG_0TC_PCFG_usMaxFrTimeMsecMult10//4
		{0x0F12, 0x0000}, //REG_0TC_PCFG_usMinFrTimeMsecMult10//5

		{0x002A, 0x0266},
		{0x0F12, 0x0000},
		{0x002A, 0x026A},
		{0x0F12, 0x0001},
		{0x002A, 0x024E},
		{0x0F12, 0x0001},
		{0x002A, 0x0268},
		{0x0F12, 0x0001},
		{0xFFFF, 0xFFFF},
	};

	SENSOR_PRINT_ERR("Max:%u Min:%u", max, min);

	if ((30 < max) || (30 < min) || (min > max)) {
		SENSOR_PRINT_ERR("Invalid options passed.");
		return -1;
	}

	if ((min == max) && (min == 0)) { // auto
		FrRateQualityType = 0;
		FrRateType = 0;
	} else {
		FrRateQualityType = 1;
		FrRateType = 2;
	}

	manual_setting[2].reg_value = FrRateType;
	manual_setting[3].reg_value = FrRateQualityType;
	manual_setting[4].reg_value = max ? 10000/max : 0;
	manual_setting[5].reg_value = min ? 10000/min : 0;

	s5k4ec_I2C_write(manual_setting);

	s_fps_cur_mode = FPS_MODE_MANUAL;
	return SENSOR_SUCCESS;
}
#endif
LOCAL void s5k4ec_set_REG_TC_DBG_AutoAlgEnBits(int bit, int set)
{
	uint16_t REG_TC_DBG_AutoAlgEnBits = 0;
	static SENSOR_REG_T reg_set[] = {
		{0xFCFC, 0xD000},
		{0x0028, 0x7000},
		{0x002A, 0x04E6},
		{0x0F12, 0x077F},// 3
		{0xFFFF, 0xFFFF},
	};

	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x04E6);
	REG_TC_DBG_AutoAlgEnBits = Sensor_ReadReg(0x0F12);

	SENSOR_PRINT_HIGH ("REG_TC_DBG_AutoAlgEnBits before: 0x%X", REG_TC_DBG_AutoAlgEnBits);
	if (set) {
		if (REG_TC_DBG_AutoAlgEnBits & (1 << bit)) {
			SENSOR_PRINT_HIGH ("No need to set");
			return;
		}

		REG_TC_DBG_AutoAlgEnBits |= (1 << bit);
	} else {
		if (!(REG_TC_DBG_AutoAlgEnBits & (1 << bit))) {
			SENSOR_PRINT_HIGH ("No need to unset");
			return;
		}

		REG_TC_DBG_AutoAlgEnBits &= ~(1 << bit);
	}
	reg_set[3].reg_value = REG_TC_DBG_AutoAlgEnBits;

	SENSOR_PRINT_HIGH ("REG_TC_DBG_AutoAlgEnBits after: 0x%X", REG_TC_DBG_AutoAlgEnBits);

	s5k4ec_I2C_write(reg_set);
	return;
}

/*
 * Decodes error codes when setting preview/capture settings.
 * Values from the s5k4ec datasheet.
 */
LOCAL const char *s5k4ec_StreamStrerror(int error) {
	switch (error) {
		case 0: return "No Error";
		case 1: return "PrevConfigIdxTooHigh";
		case 2: return "CapConfigIdxTooHigh";
		case 4: return "ClockIdxTooHigh";
		case 5: return "BadDsFixedHsync";
		case 6: return "BEST_FRRATE_NOT_ALLOWED";
		case 7: return "BAD_DS_RATIO";
		case 8: return "BAD_INPUT_WIDTH";
		case 9: return "Pvi Div";
		default: return "Unknown";
	}
}

#ifdef WA_BOOST_DDR_FREQ_720P
LOCAL int8_t s5k4ec_ddr_is_slow(int8_t boost) {
	/*
	 * We could do two things as a workaround:
	 * 1. Disable DFS and clamp to the highest frequency using set_freq
	 * 2. Tweak the thresholds using set_upthreshold
	 *    of SPRD's ondemand governor to make it
	 *    more sensitive to load changes.
	 * #1 is the simplest but have the tendency to clamp it there
	 * forever until someone fixes it, when an error occurs.
	 * #2 on errors, will still have the DFS on and may
	 * still scale down. It's just more likely to scale up.
	 *
	 * I'll go with #2.
	 */
#define METHOD 2
	const char* const set_freq = "/sys/devices/platform/scxx30-dmcfreq.0/devfreq/scxx30-dmcfreq.0/ondemand/set_freq";
	const char* const set_upthreshold = "/sys/class/devfreq/scxx30-dmcfreq.0/ondemand/set_upthreshold";
	const char* const threshold = "50";
#if METHOD == 2
#define METHOD_FILE set_upthreshold
#else
#define METHOD_FILE set_freq
#endif

	static char prev_thresh[5] = {0};
	int ret = 0;
	FILE* fp;

	SENSOR_PRINT_HIGH("boost=%d", boost);

	if (s_ddr_boosted == boost)
		return 0;

	SENSOR_PRINT_HIGH("Open file %s", METHOD_FILE);
	if (!(fp = fopen(METHOD_FILE, "r+"))) {
		SENSOR_PRINT_ERR("Failed to open %s: %s", METHOD_FILE, strerror(errno));
		return -1;
	}

#if METHOD == 2
	//method #2
	if (boost) {
		prev_thresh[0] = 0;
		if (fgets(prev_thresh, 5 /*sizeof(prev_thresh)*/, fp)) {
			ret = fprintf(fp, "%s", threshold);
			ret = ret > 0 ? 0 : errno;
		} else {
			SENSOR_PRINT_ERR("Error reading file: %s", strerror(errno));
			ret = -1;
			goto close_file;
		}
	} else {
		ret = fprintf(fp, "%s", prev_thresh);
		ret = ret > 0 ? 0 : errno;
	}
#else
	// method #1
	ret = fprintf(fp, "%d", boost ? 500000: 0);
	ret = ret > 0 ? 0 : errno;
#endif

	if (ret < 0) {
		SENSOR_PRINT_HIGH("Error writing to file: %s", strerror(errno));
	} else {
		s_ddr_boosted = boost;
	}

close_file:
	fclose(fp);
	return ret;
}
#endif
