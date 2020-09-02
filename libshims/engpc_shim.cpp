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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define LOG_NDEBUG 0
#define LOG_TAG "ENGPC_SHIM"
#include <cutils/log.h>

/*
 * Do not try to statically scan the symbol here as the linker
 * may have not yet loaded the library yet, which is weird.
 * I tried that and kept getting NULL from dlsym() so I'd deferred it
 * till the first open()
 */
typedef int (*real_open_type)(const char *pathname, int flags, mode_t mode);
static real_open_type real_open = NULL;

extern "C" int open(const char *pathname, int flags, ...)
{
	int mode_int;
	mode_t mode;
	va_list ap;
	va_start(ap, flags);
	mode_int = va_arg(ap, int);
	va_end(ap);

	// open() only have one extra argument but it takes a byte
	// rather than a word which va_args uses.
	mode = (unsigned short) mode_int;

	// Hint that the initialization (i.e. looking up the symbol)
	// will only occur only once. when it is loaded we can just reuse it.
	if (__builtin_expect(!real_open, 0)) {
		real_open = (real_open_type)dlsym(RTLD_NEXT,"open");
		if (!real_open) {
			ALOGI("dlsym() cannot find the open() symbol: %s", dlerror());
			return -1;
		}
	}

	if (!pathname)
		goto default_open;

	if (__builtin_expect(strncmp("/etc/audio_para", pathname, 15) == 0, 0)){
		ALOGI("Matched path '%s' overriding to '/vendor/etc/audio_para'", pathname);
		return real_open("/vendor/etc/audio_para", flags, mode);
	}

default_open:
	return real_open(pathname, flags, mode);
}
