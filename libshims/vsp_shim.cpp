/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * This is a shim layer to intercept IOCTL calls towards the VSP
 * Kernel Driver particualrly the IOCTLs that change and check
 * the parent clock currently in used. This will help use newer
 * encoders that uses a newer VSP library get around the inverted
 * frequency table problem on older kernel version. All of it, without
 * requiring a kernel patch.
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

#include <dlfcn.h>
#include <sys/ioctl.h>
#include <stdio.h>

// #define LOG_NDEBUG 0
#define LOG_TAG "VSP_SHIM"
#include <cutils/log.h>

#define SPRD_VSP_IOCTL_MAGIC 'm'
#define VSP_CONFIG_FREQ _IOW(SPRD_VSP_IOCTL_MAGIC, 1, unsigned int)
#define VSP_GET_FREQ    _IOR(SPRD_VSP_IOCTL_MAGIC, 2, unsigned int)

// These are extensions which is safe not to pass as an ioctl
#define VSP_SET_SCENE   _IO(SPRD_VSP_IOCTL_MAGIC, 11)
#define VSP_GET_SCENE   _IO(SPRD_VSP_IOCTL_MAGIC, 12)

static int (*real_ioctl)(int fd, int request, void *arg) =
    (int (*)(int fd, int request, void *arg)) dlsym(RTLD_NEXT, "ioctl");
static unsigned int scene_mode = 0;

extern "C" int ___VSP_CONFIG_DEV_FREQ(int fd, int *param) {
	int mod_param;

	mod_param = 3 - *param;

	return real_ioctl(fd, VSP_CONFIG_FREQ, &mod_param);
}

extern "C" int ___VSP_GET_DEV_FREQ(int fd, int *param) {
	int mod_param, ret;

	ret = real_ioctl(fd, VSP_GET_FREQ, &mod_param);

	if (!ret)
		*param = 3 - mod_param;
	else
		*param = mod_param;

	return ret;
}


extern "C" int ioctl(int fd, int req, ...)
{
	int ret;
	unsigned int request = *((unsigned int *)(&req));
	union {
		void *p;
		uint32_t *ulong_p;
		uint16_t *uint_p;
		int *int_p;
	} param;

	va_list ap;
	va_start(ap, req);
	param.p = va_arg(ap, void*);
	va_end(ap);

	ALOGV("___VSP_IOCTL: request(%d)", request);


	if (request == VSP_CONFIG_FREQ) {
		ret = ___VSP_CONFIG_DEV_FREQ(fd, param.int_p);
	} else if (request == VSP_GET_FREQ) {
		ret = ___VSP_CONFIG_DEV_FREQ(fd, param.int_p);
	} else if (request == VSP_SET_SCENE) {
		scene_mode = *param.int_p;
		ret = 0;
	} else if (request == VSP_GET_SCENE) {
		*param.int_p = scene_mode;
		ret = 0;
	} else {
		ret = real_ioctl(fd, request, param.p);
	}

	return ret;
}

