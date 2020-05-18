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

#include <utils/Log.h>
#include "sensor.h"
#include "jpeg_exif_header.h"
#include "sensor_drv_u.h"
#include "cmr_oem.h"

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
#define LIGHT_STATUS_IS_LOW(x) \
	((x) < LIGHT_STATUS_LOW_LEVEL)
#define LIGHT_STATUS_IS_NORMAL(x) \
	(!LIGHT_STATUS_IS_LOW(x))

LOCAL uint32_t is_cap = 0;
LOCAL uint16_t s_current_shutter = 0;
LOCAL uint16_t s_current_gain = 0;

LOCAL uint16_t s_fps_cur_max = -1; // current max FPS (abs max is 30)
LOCAL uint16_t s_current_env = 0; // 0 - Norm, 1 - Low Light, 2 - Night
LOCAL uint16_t s_current_ev = 0;
LOCAL uint16_t s_cur_scene = 0;
LOCAL uint32_t s_flash_mode_en = 0;
LOCAL uint32_t s_preview_mode = 0;
LOCAL uint32_t s_white_balance = 0;

// Local copy of state used by the HD camcorder settings
LOCAL uint32_t s_brightness = 3; // Default value
LOCAL uint32_t s_metering_mode = 0;
LOCAL  uint8_t s_hd_applied = 0;

// Local AF states
LOCAL uint16_t s_focus_mode = 0;
LOCAL  uint8_t s_using_low_light_af = 0;
LOCAL  uint8_t s_af_wnd_has_changed = 0;

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
LOCAL uint32_t _s5k4ec_check_image_format_support(uint32_t param);
LOCAL uint32_t _s5k4ec_pick_out_jpeg_stream(uint32_t param);
LOCAL uint32_t _s5k4ec_after_snapshot(uint32_t param);
LOCAL uint32_t _s5k4ec_GetExifInfo(uint32_t param);
LOCAL uint32_t _s5k4ec_ExtFunc(uint32_t ctl_param);
LOCAL uint32_t _s5k4ec_StreamOn(uint32_t param);
LOCAL uint32_t _s5k4ec_set_iso(uint32_t level);
LOCAL uint32_t _s5k4ec_recovery_init();

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

LOCAL uint32_t s5k4ecgx_fast_ae(uint32_t on);
LOCAL uint32_t s5k4ec_preflash_af(uint32_t on);
LOCAL uint32_t s5k4ec_main_flash(uint32_t on);
LOCAL uint32_t s5k4ec_low_light_AF_check();
LOCAL uint32_t s5k4ec_set_FPS(uint32_t fps);
LOCAL uint32_t s5k4ec_set_FPS_mode(uint32_t fps_mode);

LOCAL EXIF_SPEC_PIC_TAKING_COND_T s_s5k4ec_exif;

LOCAL SENSOR_REG_TAB_INFO_T s_s5k4ec_resolution_Tab_YUV[] = {
	//COMMON INIT
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_common_init), 0, 0, 24, SENSOR_IMAGE_FORMAT_YUV422},
	//YUV422 PREVIEW 1
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_320X240), 320, 240, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_640X480), 640, 480, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_720X540), 720, 540, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_1280X720), 1280, 720, 24, SENSOR_IMAGE_FORMAT_YUV422},

	//YUV422 PREVIEW 2
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_1280X960), 1280, 960, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_1600X1200), 1600, 1200, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_2048X1536), 2048, 1536, 24, SENSOR_IMAGE_FORMAT_YUV422},
	{ADDR_AND_LEN_OF_ARRAY(s5k4ec_2560X1920), 2560, 1920, 24, SENSOR_IMAGE_FORMAT_YUV422},

	{PNULL, 0, 0, 0, 0, 0}
};

LOCAL uint32_t s5k4ec_set_awb_enable(uint32_t enable)
{
	uint16_t unlock = 1;

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
	Sensor_WriteReg(0x002A, 0x2c66);
	Sensor_WriteReg(0x0F12, unlock);

	return 0;
}

LOCAL uint32_t s5k4ec_set_ae_enable(uint32_t enable)
{
	uint16_t unlock = 1;

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
	{0, 0, 1280, 720, 664, 648, 0, {0, 0, 1280, 720}},

	//YUV422 PREVIEW 2
	{0, 0, 1280, 960, 664, 648, 0, {0, 0, 1280, 960}},
	{0, 0, 1600, 1200, 664, 648, 0, {0, 0, 1600, 1200}},
	{0, 0, 2048, 1536, 660, 648, 0, {0, 0, 2048, 1536}},
	{0, 0, 2560, 1920, 660, 648, 0, {0, 0, 2560, 1920}},

	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}},
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
	s5k4ec_set_ae_enable,/*8*/
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
	PNULL,//_s5k4ec_StreamOff/*43*/// stream_off
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

LOCAL uint32_t _s5k4ec_GetExifInfo(__attribute__((unused)) uint32_t param)
{
	EXIF_SPEC_PIC_TAKING_COND_T* exif_ptr=&s_s5k4ec_exif;
	uint32_t shutter_speed;
	uint16_t iso_value;

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
		exif_ptr->ExposureTime.numerator = 1;
		exif_ptr->ExposureTime.denominator = shutter_speed;
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

LOCAL uint32_t _s5k4ec_PowerOn(uint32_t power_on)
{
#ifndef CONFIG_CAMERA_IOCTL_IOCTL_HAS_POWER_ONOFF
	/*
	 * Some devices have a convenience IOCTL SENSOR_IO_POWER_ONOFF that
	 * will handle the process of turning this on and off.
	 * The CONFIG_CAMERA_IOCTL_IOCTL_HAS_POWER_ONOFF when set will
	 * attempt to call that only after the poweron function has
	 * been called.
	 * If that IOCTL is present, it is advisable to use that instead
	 * of this.
	 */
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
#endif
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
				SENSOR_Sleep(100);
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
		 * Initialize our local states, again.
		 * Just to be sure. Setting it again will not hurt
		 * But not setting it at all will.
		 */
		s_fps_cur_max = -1; // current max FPS (abs max is 30)
		s_current_env = 0; // 0 - Norm, 1 - Low Light, 2 - Night
		s_current_ev = 0;
		s_cur_scene = 0;
		s_flash_mode_en = 0;
		s_preview_mode = 0;
		s_white_balance = 0;
		s_brightness = 3; // Default value
		s_hd_applied = 0;
		s_metering_mode = 0;
		s_focus_mode = 0;
		s_using_low_light_af = 0;
		s_af_wnd_has_changed = 0;
	}

	return ret_value;
}

LOCAL uint32_t _s5k4ec_set_brightness(uint32_t level)
{
	if (level > 7) {
		SENSOR_PRINT_ERR("Undefined Brightness level %u", level);
		return 0;
	}
	SENSOR_PRINT_HIGH("Apply Brightness level %u", level);

	s5k4ec_I2C_write(s5k4ec_brightness_tab[level]);
	s_brightness = level;
	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_BRIGHTNESSVALUE, (uint32_t) level);

	return 0;
}

LOCAL uint32_t _s5k4ec_set_contrast(uint32_t level)
{
	if (level > 7) {
		SENSOR_PRINT_ERR("Undefined Contrast level %u", level);
		return 0;
	}

	SENSOR_PRINT_HIGH("Apply Contrast level %u", level);

	s5k4ec_I2C_write(s5k4ec_contrast_tab[level]);
	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_CONTRAST, (uint32_t) level);

	return 0;
}

LOCAL uint32_t _s5k4ec_set_iso(uint32_t level)
{
	if (CAMERA_ISO_MAX < level) {
		SENSOR_PRINT_ERR("Undefined ISO mode %u", level);
		return 0;
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
		/*
		 * Maximum iso gain is about 5232 which is about
		 * 400 ISO as it seems with the msm implementation
		 * and the stock rom. AUTO ISO might never
		 * reach this level, but would still appear
		 * on the Exif data as 400 ISO though.
		 */
		case CAMERA_ISO_400:
		case CAMERA_ISO_800:
		case CAMERA_ISO_1600:
			s5k4ec_I2C_write(s5k4ec_ISO_400);
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

        return 0;
}

LOCAL uint32_t _s5k4ec_set_saturation(uint32_t level)
{
	if (level > 6) {
		SENSOR_PRINT_ERR("Undefined Saturation level %u", level);
		return 0;
	}

	SENSOR_PRINT_HIGH("Apply Saturation level %u",level);
	s5k4ec_I2C_write(s5k4ec_saturation_tab[level]);

	return 0;
}

LOCAL uint32_t _s5k4ec_set_image_effect(uint32_t effect_type)
{
	static uint8_t should_revert_WB = 0;
	if (effect_type > 8) {
		SENSOR_PRINT_ERR("Undefined Image Effect type %u", effect_type);
		return 0;
	}

	SENSOR_PRINT_HIGH("Apply Image Effect type %u", effect_type);
	s5k4ec_I2C_write(s5k4ec_image_effect_tab[effect_type]);

	/*
	 * Work around to image effects affecting the whitebalance.
	 * Switching to None should also revert the alteration to WB.
	 */
	if (effect_type >= 2 && effect_type <= 4) {
		should_revert_WB = 1;
	} else if (should_revert_WB) {
		SENSOR_PRINT_HIGH("Reverting White Balance from effects.");
		_s5k4ec_set_awb(s_white_balance);
		should_revert_WB = 0;
	}

	return 0;
}

LOCAL uint32_t _s5k4ec_set_ev(uint32_t level)
{
	if (level > 6) {
		SENSOR_PRINT_ERR("Undefined Exposure Compensation level %u", level);
		return 0;
	}

	SENSOR_PRINT_HIGH("Apply Exposure Compensation level %u", level);
	s_current_ev = level;
	s5k4ec_I2C_write(s5k4ec_ev_tab[level]);

	return 0;
}

LOCAL uint32_t _s5k4ec_set_anti_flicker(uint32_t mode)
{
	if (mode > 6) {
		SENSOR_PRINT_ERR("Undefined Anti-banding mode %u", mode);
		return 0;
	}

	SENSOR_PRINT_HIGH("Applied %s Anti-banding", mode ? "50Hz" : "60Hz");
	s5k4ec_I2C_write(s5k4ec_anti_banding_flicker_tab[mode]);

	return 0;
}

LOCAL uint32_t _s5k4ec_set_video_mode(uint32_t mode)
{
	struct camera_context *cxt = camera_get_cxt();

	SENSOR_PRINT_HIGH("mode = 0x%X", mode);

	SENSOR_PRINT_HIGH("cxt->sn_cxt.preview_mode=%u s_preview_mode=%u", cxt->sn_cxt.preview_mode, s_preview_mode);
	if (s_preview_mode != cxt->sn_cxt.preview_mode) {
		if (1280 <= s_s5k4ec_resolution_Tab_YUV[cxt->sn_cxt.preview_mode].width) {
			if (!s_hd_applied) {
				SENSOR_PRINT_HIGH("Applying HD camcorder settings");
				s5k4ec_I2C_write(s5k4ec_enable_camcorder);
			}
			s_hd_applied = 1;
		} else if (s_hd_applied) {
			SENSOR_PRINT_HIGH("Reverting HD camcorder settings");
			s5k4ec_I2C_write(s5k4ec_disable_camcorder);

			// revert AE
			_s5k4ec_set_brightness(s_brightness);

			// Revert Metering setting
			if (s_metering_mode != 0) // matrix
				s5k4ec_set_Metering(s_metering_mode);
			s_hd_applied = 0;
		}

		s5k4ec_I2C_write(s5k4ec_preview_return);
	}

	s_preview_mode = cxt->sn_cxt.preview_mode;
	s5k4ec_set_FPS(cxt->cmr_set.frame_rate);

	return 0;
}

LOCAL uint32_t _s5k4ec_set_awb(uint32_t mode)
{
	if (mode > 6) {
		SENSOR_PRINT_ERR("Undefined Auto White Balance mode %u",mode);
		return 0;
	}

	SENSOR_PRINT_HIGH("Apply Auto White Balance mode %u", mode);
	s5k4ec_I2C_write(s5k4ec_awb_tab[mode]);
	s_white_balance = mode;

	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_LIGHTSOURCE, (uint32_t) mode);
	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_WHITEBALANCE, (uint32_t) mode);

	return 0;
}

LOCAL uint32_t _s5k4ec_set_scene_mode(uint32_t mode)
{
	SENSOR_PRINT_HIGH("Apply Scene mode %u", mode);

	switch (mode) {
		case CAMERA_SCENE_MODE_AUTO:
			s5k4ec_I2C_write(s5k4ec_scene_off);
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
			s5k4ec_I2C_write(s5k4ec_scene_sunset);
			break;
		case CAMERA_SCENE_MODE_DUSK_DAWN:
			s5k4ec_I2C_write(s5k4ec_scene_dawn);
			break;
		case CAMERA_SCENE_MODE_FALL_COLOR:
			s5k4ec_I2C_write(s5k4ec_scene_fall);
			break;
		case CAMERA_SCENE_MODE_TEXT:
			s5k4ec_I2C_write(s5k4ec_scene_text);
			break;
		case CAMERA_SCENE_MODE_CANDLELIGHT:
			s5k4ec_I2C_write(s5k4ec_scene_candlelight);
			break;
		case CAMERA_SCENE_MODE_FIREWORK:
			s5k4ec_I2C_write(s5k4ec_scene_firework);
			break;
		case CAMERA_SCENE_MODE_BACKLIGHT:
			s5k4ec_I2C_write(s5k4ec_scene_backlight);
			break;

		case CAMERA_SCENE_MODE_HDR:
			/*
			 * Try to use the previous mode as the template
			 * Mode of HDR. Just check that it's not
			 * CAMERA_SCENE_MODE_HDR or we'll have
			 * infinite recursion.
			 */
			SENSOR_PRINT_ERR("HDR: setting previous mode %u", s_cur_scene);
			_s5k4ec_set_scene_mode(s_cur_scene);

			return SENSOR_SUCCESS; // ignore the error
		case CAMERA_SCENE_MODE_ACTION:
		case CAMERA_SCENE_MODE_NORMAL:
		default:
			SENSOR_PRINT_ERR("Undefined Scene mode %u", mode);
		return SENSOR_SUCCESS; // ignore the error
	}

	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_SCENECAPTURETYPE,(uint32_t) mode);
	s_cur_scene = mode;
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_BeforeSnapshot(uint32_t param)
{
	struct camera_context *cxt = camera_get_cxt();
	uint32_t capture_mode = param & 0xffff;
	s_flash_mode_en = cxt->cmr_set.flash;

	SENSOR_PRINT_HIGH("Begin. capture_mode=%d", capture_mode);

	if(1 != capture_mode)
		is_cap = 1;

	// Use the global context to find out whether we are using
	// camera flash or not (assuming the autoflash has been
	// reduced to a YES or NO flash question)
	if (FLASH_CLOSE != s_flash_mode_en)
		s5k4ec_main_flash(1);

	if (LIGHT_STATUS_IS_LOW(s5k4ec_lightcheck())) {
		SENSOR_PRINT_HIGH("Low light environment detected");
		if (s_cur_scene == CAMERA_SCENE_MODE_NIGHT ||
			s_cur_scene == CAMERA_SCENE_MODE_FIREWORK) {
			SENSOR_PRINT_HIGH("Night mode activate");
			s5k4ec_I2C_write(s5k4ec_night_mode_On);
			s_current_env = 2;
		} else {
			SENSOR_PRINT_HIGH("Low cap mode activate");
			s5k4ec_I2C_write(s5k4ec_low_cap_On);
			s_current_env = 1;
		}
	}

	Sensor_SetMode(capture_mode);
	SENSOR_Sleep(10);

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
	uint16_t width, height=0;

	if (FLASH_CLOSE != s_flash_mode_en)
		s5k4ec_main_flash(0);

	// Reset the low (light) capture settings as soon as possible.
	if (2 == s_current_env) {
		s5k4ec_I2C_write(s5k4ec_night_mode_Off);
	} else if (1 == s_current_env) {
		s5k4ec_I2C_write(s5k4ec_low_cap_Off);
	}
	s_current_env = 0;

	//_s5k4ec_recovery_init();
	Sensor_SetMode((uint32_t)param);
	//SENSOR_Sleep(10);
	is_cap = 0;


	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x1D02);
	width = Sensor_ReadReg(0x0f12);
	Sensor_WriteReg(0x002E, 0x2BC4);
	height = Sensor_ReadReg(0x0F12);
	SENSOR_PRINT_HIGH("width=%x, height=%x",width,height);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_MatchZone(SENSOR_EXT_FUN_T_PTR param_ptr)
{
	SENSOR_RECT_T zone_rect;
	uint32_t rtn=SENSOR_SUCCESS;
	memset((void*)&zone_rect, 0, sizeof(SENSOR_RECT_T));
	switch (param_ptr->cmd)
	{
	case SENSOR_EXT_FOCUS_START:
			switch (param_ptr->param)
			{
			case SENSOR_EXT_FOCUS_ZONE:
			case SENSOR_EXT_FOCUS_MULTI_ZONE:
					zone_rect.w = FOCUS_ZONE_W;
					zone_rect.h = FOCUS_ZONE_H;
					break;
			default:
					break;
			}
			break;
	case SENSOR_EXT_EXPOSURE_START:
			switch (param_ptr->param)
			{
			case SENSOR_EXT_EXPOSURE_ZONE:
					zone_rect.w = EXPOSURE_ZONE_W;
					zone_rect.h = EXPOSURE_ZONE_H;
					break;
			default:
					break;
			}
			break;
	 default:
		  break;
	}

	if ((0x00 != s_s5k4ec_resolution_Tab_YUV[SENSOR_MODE_PREVIEW_ONE].width)&& (0x00 !=s_s5k4ec_resolution_Tab_YUV[SENSOR_MODE_PREVIEW_ONE].height)
	    && (0x00 != zone_rect.w)&& (0x00 != zone_rect.h))
	{
		param_ptr->zone.x =(zone_rect.w * param_ptr->zone.x) /
		    s_s5k4ec_resolution_Tab_YUV[SENSOR_MODE_PREVIEW_ONE].width;
		param_ptr->zone.y =(zone_rect.h * param_ptr->zone.y) /
		    s_s5k4ec_resolution_Tab_YUV[SENSOR_MODE_PREVIEW_ONE].height;
	}
	else
	{
		SENSOR_PRINT_ERR("SENSOR: _s5k4ec_MatchZone, w:%d, h:%d error \n",zone_rect.w, zone_rect.h);
		rtn = SENSOR_FAIL;
	}

	SENSOR_PRINT_HIGH("SENSOR: _s5k4ec_MatchZone, x:%d, y:%d \n",param_ptr->zone.x, param_ptr->zone.y);
	return rtn;
}

LOCAL uint32_t _s5k4ec_AutoFocusTrig(__attribute__((unused)) SENSOR_EXT_FUN_PARAM_T_PTR param_ptr)
{
	struct camera_context *cxt = camera_get_cxt();
	uint32_t rtn = SENSOR_FAIL;
	uint16_t reg_value;
	uint16_t frame_time = s5k4ecgx_get_frame_time();
	int8_t flash_mode = cxt->cmr_set.flash;
	int fresh = 1, retries = 7; // for workaround
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
		_s5k4ec_set_awb(0);
	}

	s5k4ec_I2C_write(s5k4ec_single_AF_start);

	/*
	 * 2 frame delay before checking the result of the
	 * first phase, too early or we would get an error
	 * value.
	 */
	SENSOR_Sleep(frame_time*2);


	for (i = 30; i--; ) {
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

		// Erroneous state, when reg_value is
		// either of the following: 0,3,4,6,8
		// In the case of both timeout and error
		// Give the error message a higher precedence
		if (reg_value != 0x1 || reg_value != 2) {
			/*
			 * Issue: Unknown delay between AF start and progress checks.
			 * Specially after setting the AF window, there would be
			 * some unknown delay before the AF can actually start.
			 * Unfortunately, it also seemed that it's independent of
			 * the frame rate. One characteristic of this problem
			 * is that it only happens at the start of each AF focusing.
			 *
			 * If we get an error at the start, try to skip it up to
			 * 7 times. When a single Progress is received, we go on
			 * as usual.
			 */
			if ((fresh == 1) && retries) {
				SENSOR_PRINT_ERR("[1st]AF -Setup");
				retries--;
				continue;
			}

			SENSOR_PRINT_ERR("[1st]AF -Error!");
		} else if (i <= 0) {
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
	}
	SENSOR_PRINT_HIGH("Done.");

	return rtn;
}

LOCAL uint32_t _s5k4ec_AutoFocusMultiZone(__attribute__((unused)) SENSOR_EXT_FUN_PARAM_T_PTR param_ptr)
{
	SENSOR_PRINT_ERR("Not yet implemented");
	return SENSOR_SUCCESS;
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
	SENSOR_PRINT_HIGH("Start x=%d, y=%d, w=%d, h=%d, width=%d height=%d",
		param_ptr->zone[0].x,param_ptr->zone[0].y, param_ptr->zone[0].w, param_ptr->zone[0].h,
		width, height
	);

	if (touch_x < 0 || touch_y < 0) {
		SENSOR_PRINT_HIGH("Invalid coordinates, reseting AF window setting");
		_s5k4ecgx_reset_focus_touch_position();
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
	 * Re-read the last paragraph if you didn't get something was up.
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

	s5k4ec_low_light_AF_check();

	// This function usualy is activated when doing AF mode is macro
	// But some camera apps doesn't even expose changing AF modes...
	_s5k4ecgx_set_focus_touch_position(ext_ptr);

	switch (ext_ptr->param) {
	case SENSOR_EXT_FOCUS_MULTI_ZONE:
		rtn = _s5k4ec_AutoFocusMultiZone(ext_ptr);
		break;
	case SENSOR_EXT_FOCUS_MACRO:
		rtn |= _s5k4ec_AutoFocusTrig(ext_ptr);
		break;
	case SENSOR_EXT_FOCUS_TRIG:
		// Function not in vendor blob, used to revert any changes by
		// _s5k4ecgx_set_focus_touch_position().
		// Uncomment when you used _s5k4ecgx_set_focus_touch_position()
		// only for AF Macro.
		// rtn = _s5k4ecgx_reset_focus_touch_position();
		rtn |= _s5k4ec_AutoFocusTrig(ext_ptr);
	case SENSOR_EXT_FOCUS_ZONE:
	default:
		break;
	}
	SENSOR_PRINT_HIGH("Done.");
	return rtn;
}

LOCAL uint32_t _s5k4ec_ExposureAuto(void)
{
	SENSOR_PRINT_ERR("Not implemented");

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_ExposureZone(__attribute__((unused)) SENSOR_EXT_FUN_T_PTR param_ptr)
{
	SENSOR_PRINT_ERR("Not implemented");

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4ec_StartExposure(uint32_t param)
{
	uint32_t rtn=SENSOR_SUCCESS;

	SENSOR_EXT_FUN_T_PTR ext_ptr=(SENSOR_EXT_FUN_T_PTR)param;

	SENSOR_PRINT("param=%d", ext_ptr->param);
	switch (ext_ptr->param) {
		case SENSOR_EXT_EXPOSURE_AUTO:
			rtn = _s5k4ec_ExposureAuto();
			break;
		case SENSOR_EXT_EXPOSURE_ZONE:
			rtn = _s5k4ec_ExposureZone(ext_ptr);
		/* Fall-through */
		default:
			break;
	}

	return rtn;
}

LOCAL uint32_t _s5k4ec_SetEV(uint32_t param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr = (SENSOR_EXT_FUN_PARAM_T_PTR) param;

	SENSOR_PRINT("SENSOR: _s5k4ec_SetEV param: 0x%x", ext_ptr->param);
	uint32_t shutter = 0;
	uint32_t gain = 0;

	uint32_t ev = ext_ptr->param;
	switch(ev) {
	case SENSOR_HDR_EV_LEVE_0:
		s5k4ec_I2C_write(s5k4ec_ev_tab[0]);
		break;
	case SENSOR_HDR_EV_LEVE_1:
		s5k4ec_I2C_write(s5k4ec_ev_tab[3]);
		break;
	case SENSOR_HDR_EV_LEVE_2:
		s5k4ec_I2C_write(s5k4ec_ev_tab[6]);
		break;
	default:
		break;
	}
	return rtn;
}

LOCAL uint8_t af_firmware[] = {
	#if 0 //\B2\BB\D3\C3AF
	0x80,
	0x00,
	#endif
};

LOCAL int _s5k4ec_init_firmware(uint32_t param)
{
	int ret = 0;
	uint32_t i = 0;
	uint32_t init_num = NUMBER_OF_ARRAY(af_firmware);
	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr = (SENSOR_EXT_FUN_PARAM_T_PTR)param;
	uint8_t  *reg_ptr = af_firmware;
	uint16_t reg_val_1,reg_val_2;

	SENSOR_PRINT_HIGH("SENSOR: _s5k4ec_init_firmware: cmd=%d!.\n", ext_ptr->cmd);
	switch (ext_ptr->param)	{
	 case SENSOR_EXT_FOCUS_TRIG:	//auto focus
		    reg_ptr = af_firmware;
		    break;
	 default:
		    break;
	}

	Sensor_WriteReg(0x3000, 0x20);
	for (i = 0; i < 4; i++) {
		ret = Sensor_WriteData(reg_ptr, init_num);
		if (ret != 0) {
			SENSOR_PRINT_ERR("SENSOR: write sensor reg fai, ret : %d\n", ret);
			continue;
		}
		break;
	}
// 	Sensor_WriteReg(0x3022, 0x00);
	Sensor_WriteReg(0x3023, 0x00);
	Sensor_WriteReg(0x3024, 0x00);
	Sensor_WriteReg(0x3025, 0x00);
	Sensor_WriteReg(0x3026, 0x00);
	Sensor_WriteReg(0x3027, 0x00);
	Sensor_WriteReg(0x3028, 0x00);
	Sensor_WriteReg(0x3029, 0x7F);
	Sensor_WriteReg(0x3000, 0x00);

	reg_val_1 = Sensor_ReadReg(0x3000);
	reg_val_2 = Sensor_ReadReg(0x3004);
	//      sc8810_i2c_set_clk(1,100000); //wjp
	SENSOR_PRINT_HIGH("SENSOR: _s5k4ec_init_firmware: E!.\n");
#if 0
	//SENSOR_PRINT_HIGH("SENSOR: 0x3029=0x%x,0x3000=0x%x,0x3004=0x%x.\n",Sensor_ReadReg(0x3029),reg_val_1,reg_val_2);
	//SENSOR_PRINT_HIGH("SENSOR: 0x8000=0x%x,0x8002=0x%x,0x8f57=0x%x.\n",Sensor_ReadReg(0x8000),Sensor_ReadReg(0x8002),Sensor_ReadReg(0x8f57));
#endif
	return ret;
}


LOCAL uint32_t _s5k4ec_ExtFunc(uint32_t ctl_param)
{
	uint32_t rtn = SENSOR_SUCCESS;

	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr =(SENSOR_EXT_FUN_PARAM_T_PTR) ctl_param;
	SENSOR_PRINT_HIGH("cmd:0x%x", ext_ptr->cmd);

	switch (ext_ptr->cmd)
	{
	 case SENSOR_EXT_FUNC_INIT:
		rtn = _s5k4ec_init_firmware(ctl_param);
		break;
	 case SENSOR_EXT_FOCUS_START:
		rtn = _s5k4ec_StartAutoFocus(ctl_param);
		break;
	case SENSOR_EXT_EXPOSURE_START:
		rtn = _s5k4ec_StartExposure(ctl_param);
		break;
	case SENSOR_EXT_EV:
		rtn = _s5k4ec_SetEV(ctl_param);
	default:
		    break;
	}

	return rtn;
}

LOCAL uint32_t _s5k4ec_recovery_init()
{
	SENSOR_PRINT("SENSOR: s5k4ec_steamon recovery\n");

	Sensor_WriteReg(0x0028, 0x7000);
	Sensor_WriteReg(0x002A, 0x0242);
	Sensor_WriteReg(0x0F12, 0x0000);	//#REG_TC_GP_EnablePreview

	Sensor_WriteReg(0x0028, 0xD000);
	Sensor_WriteReg(0x002A, 0xB0A0);
	Sensor_WriteReg(0x0F12, 0x0000);	//Clear cont. clock befor config change

	Sensor_WriteReg(0x0028, 0x7000);
	Sensor_WriteReg(0x002A, 0x0244);
	Sensor_WriteReg(0x0F12, 0x0001);	//#REG_TC_GP_EnablePreviewChanged
	return 0;
}

LOCAL uint32_t _s5k4ec_StreamOn(__attribute__((unused)) uint32_t param)
{
	SENSOR_PRINT_HIGH("SENSOR:Start s5k4ec_steamon 1613");

	if (1 != is_cap) {
		SENSOR_PRINT_HIGH("zxdbg preview stream on");
		Sensor_WriteReg(0x0028, 0x7000);
		Sensor_WriteReg(0x002A, 0x023E);
		Sensor_WriteReg(0x0F12, 0x0001);  //#REG_TC_GP_EnablePreview
		Sensor_WriteReg(0x0F12, 0x0001);  //#REG_TC_GP_EnablePreviewChanged

		Sensor_WriteReg (0x0028, 0xD000);
		Sensor_WriteReg(0x002A, 0x1000);
		Sensor_WriteReg(0x0F12, 0x0001);
		SENSOR_Sleep(10);
	} else {
		SENSOR_PRINT_HIGH("zxdbg capture stream on");
		s5k4ec_I2C_write(s5k4ec_capture_start);
	}

	return 0;
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
	if(metering_mode >= 3) {
		SENSOR_PRINT_ERR("Undefined Metering mode %u", metering_mode);
		return SENSOR_OP_PARAM_ERR;
	}

	switch(metering_mode) {
		case 0:
			s5k4ec_I2C_write(s5k4ec_metering_matrix);
			break;
		case 1:
			s5k4ec_I2C_write(s5k4ec_metering_spot);
			break;
		case 2:
			s5k4ec_I2C_write(s5k4ec_metering_center_weighted);
			break;
	}

	SENSOR_PRINT_HIGH(
		"Apply %s Metering mode ",
		0 == metering_mode ? "Matrix" :
		1 == metering_mode ? "Spot" :
		"Center Weighted"
	);
	s_metering_mode = metering_mode;

	return 0;
}

LOCAL uint32_t s5k4ec_set_sharpness(uint32_t level)
{
	if(level >= 8) {
		SENSOR_PRINT_ERR("Undefined Sharpness level %u", level);
		return SENSOR_OP_PARAM_ERR;
	}

	SENSOR_PRINT_HIGH("Apply Sharpness level %u", level);
	s5k4ec_I2C_write((SENSOR_REG_T*) s5k4ec_saturation_tab[level]);

	return 0;

}

LOCAL uint32_t s5k4ec_lightcheck()
{
	uint16_t low_word = 0;
	uint16_t high_word = 0;


	Sensor_WriteReg(0xFCFC,0xD000);
	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x2C18);
	low_word = Sensor_ReadReg(0x0F12);
	Sensor_WriteReg(0x002E, 0x2C1A);
	high_word = Sensor_ReadReg(0x0F12);

	SENSOR_PRINT_HIGH("Luminance results high=%x low=%x", low_word, high_word);

	return low_word | (high_word <<16);

}

LOCAL uint32_t s5k4ec_flash(uint32_t param)
{
	uint32_t *autoflash = (uint32_t *)param;
	uint32_t lux;

	SENSOR_PRINT_HIGH("Start");

	lux = s5k4ec_lightcheck();
	if (LIGHT_STATUS_IS_LOW(lux)) {
		SENSOR_PRINT_HIGH("Low light, using flash");
		(*autoflash) = 1;
	} else {
		SENSOR_PRINT_HIGH("Normal light levels, not using flash");
	}

	SENSOR_PRINT_HIGH("Done.");

	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_get_ISO_rate(void)
{
	uint16_t iso_a_gain= 0;
	uint16_t iso_d_gain= 0;
	uint32_t iso_gain, iso_rate;

	SENSOR_PRINT_HIGH("Get ISO gain");

	Sensor_WriteReg(0x002C, 0x7000);
	Sensor_WriteReg(0x002E, 0x2BC4);
	iso_a_gain = Sensor_ReadReg(0x00F12);
	// Sensor_WriteReg(0x002E, 0x2BC6);
	iso_d_gain = Sensor_ReadReg(0x00F12);

	iso_gain = (iso_a_gain * iso_d_gain) / 384 /*200*/;

	/* Convert ISO value */
	if(iso_gain > 0x400)
		iso_rate = 400;
	else if(iso_gain > 0x200)
		iso_rate = 200;
	else if(iso_gain > 0x100)
		iso_rate = 100;
	else
		iso_rate = 50;


	if (!iso_gain)
		SENSOR_PRINT_ERR("Failed. [ISO rate:%u gain: %u]", iso_rate, iso_gain);
	else
		SENSOR_PRINT_HIGH("Done. [ISO rate:%u gain: %u]", iso_rate, iso_gain);

	return iso_rate;
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

	Sensor_WriteReg(0x002E, 0x2BC2);
	msb = Sensor_ReadReg(0x00F12);

	/*
	 * Formula:
	 * x = ((msb << 16) | lsb) / 400
	 * shutter_speed = 1000 / x
	 */
	exposure_time = (msb << 16) | lsb;

	if (!exposure_time) {
		SENSOR_PRINT_HIGH("Failed, sensor values are 0");
		return 0;
	} else {
		exposure_time = 400000 / exposure_time;
		if (!exposure_time) {
			SENSOR_PRINT_HIGH("exposure time still results to 0");
			exposure_time = 1;
		}
	}

	SENSOR_PRINT_HIGH("Done. [Shutter Speed : %u]", exposure_time);
	return exposure_time;
}

LOCAL uint16_t s5k4ecgx_get_frame_time()
{
	uint16_t frame_time = 0;
	uint16_t temp1 = 0;
	int err;

	SENSOR_PRINT_HIGH("Start");

	Sensor_WriteReg(0xFCFC, 0xD000);
	Sensor_WriteReg(0x002C, 0x7000);

	Sensor_WriteReg(0x002E, 0x2128);
	temp1 = Sensor_ReadReg (0x0F12);

	frame_time = temp1/400;

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

	if (2 == mode) { //Macro
		s5k4ec_I2C_write(s5k4ec_AF_macro_mode_1);
		SENSOR_Sleep(delay);

		s5k4ec_I2C_write(s5k4ec_AF_macro_mode_2);
		SENSOR_Sleep(delay);

		s5k4ec_I2C_write(s5k4ec_AF_macro_mode_3);
// 	} else if (3 == mode) { // Infinity
// 		s5k4ec_I2C_write(s5k4ec_AF_return_inf_pos);
	} else { // Auto and every other else
		s5k4ec_I2C_write(s5k4ec_AF_normal_mode_1);
		SENSOR_Sleep(delay);

		s5k4ec_I2C_write(s5k4ec_AF_normal_mode_2);
		SENSOR_Sleep(delay);

		s5k4ec_I2C_write(s5k4ec_AF_normal_mode_3);
	}
	return 0;
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
			SENSOR_Sleep(10);
		}
	}
	SENSOR_PRINT_HIGH("Done.");
	return 0;
}

LOCAL uint32_t s5k4ec_preflash_af(uint32_t on)
{
	SENSOR_PRINT_HIGH("Turn %s Pre Flash setting", on ? "On" : "Off");
	if (on)
		s5k4ec_I2C_write(s5k4ec_pre_flash_On);
	else
		s5k4ec_I2C_write(s5k4ec_pre_flash_Off);

	return 0;
}

LOCAL uint32_t s5k4ecgx_fast_ae(uint32_t on)
{
	SENSOR_PRINT_HIGH("Turn %s Fast AE mode", on ? "On" : "Off");
	if (on)
		s5k4ec_I2C_write(s5k4ec_FAST_AE_On);
	else
		s5k4ec_I2C_write(s5k4ec_FAST_AE_Off);

	return 0;
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

LOCAL uint32_t s5k4ec_low_light_AF_check(void)
{
	struct camera_context *cxt = camera_get_cxt();
	uint8_t flash_mode = cxt->cmr_set.flash;
	uint8_t use_ll_af = 0;

	SENSOR_PRINT_HIGH("Decide whether to use low light AF or not");

	/*
	 * The conditions for low light AF is that the conditions
	 * are considered low light and Flash is will never be used
	 */
	if ((flash_mode == FLASH_CLOSE) && LIGHT_STATUS_IS_LOW(s5k4ec_lightcheck())) {
		use_ll_af = 1;
		SENSOR_PRINT_HIGH("Using low light autofocus");
	} else {
		use_ll_af = 0;
		SENSOR_PRINT_HIGH("Not using low light autofocus");
	}

	if (use_ll_af == s_using_low_light_af) {
		SENSOR_PRINT_HIGH("Sensor already%s using Low light autofocus",
			use_ll_af ? "" : " not"
		);
		return SENSOR_SUCCESS;
	}

	if (use_ll_af)
		s5k4ec_I2C_write(s5k4ec_AF_low_light_mode_On);
	else
		s5k4ec_I2C_write(s5k4ec_AF_low_light_mode_Off);

	SENSOR_PRINT_HIGH("Done.");
	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_set_FPS_mode(uint32_t fps_mode)
{
	SENSOR_PRINT_HIGH("Apply FPS mode %d", fps_mode);

	if (s_fps_cur_max == fps_mode) {
		SENSOR_PRINT_HIGH("Already applied");
		return SENSOR_SUCCESS;
	}

	switch(fps_mode) {
		case 0:
			s5k4ec_I2C_write(s5k4ec_Auto30_FPS);
			break;
		case 1:
			s5k4ec_I2C_write(s5k4ec_7_FPS);
			break;
		case 2:
			s5k4ec_I2C_write(s5k4ec_12_FPS);
			break;
		case 3:
			s5k4ec_I2C_write(s5k4ec_15_FPS);
			break;
		case 4:
			s5k4ec_I2C_write(s5k4ec_30_FPS);
			break;
		case 5:
			s5k4ec_I2C_write(s5k4ec_25_FPS);
			break;
		default:
			SENSOR_PRINT_ERR("Undefined FPS mode %u", fps_mode);
			return SENSOR_FAIL;
	}
	s_fps_cur_max = fps_mode;
	SENSOR_Sleep(5);
	return SENSOR_SUCCESS;
}

LOCAL uint32_t s5k4ec_set_FPS(uint32_t fps)
{
	uint32_t mode;

	if (0 == fps) { /* Auto FPS */
		mode = 0;
	} else if (7 >= fps) {
		mode = 1;
	} else if (12 >= fps) {
		mode = 2;
	} else if (15 >= fps) {
		mode = 3;
	} else if (25 >= fps) {
		mode = 4;
	} else if (30 >= fps) {
		mode = 5;
	} else {
		mode = 64; /* An invalid mode */
	}

	return s5k4ec_set_FPS_mode(mode);
}
