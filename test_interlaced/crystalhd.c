/*
 * Copyright (C) 2009 Edgar Hucek
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * crystalhd.c: H264 Video Decoder utilizing Broadcom Crystal HD engine
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "crystalhd.h"
#include "crystalhd_hw.h"

HANDLE hDevice = 0;

void Sleep(int ms) {
	struct timespec req;
	req.tv_sec=0;
	req.tv_nsec= 1000000 * ms;
	nanosleep(&req, 0);
}

void crystalhd_decode_package (uint8_t *buf, uint32_t size) {
	int i;

	if(size == 0) return;

	printf("Decode data: \n");

	for(i = 0; i < ((size < 80) ? size : 80); i++) {
		printf("%02x ", ((uint8_t*)buf)[i]);
		if((i+1) % 40 == 0)
			printf("\n");
	}
	printf("\n...\n");
}

pthread_once_t once_control = PTHREAD_ONCE_INIT;
pthread_mutex_t crystalhd_h264_lock;

void init_once_routine(void) {
  hDevice = crystalhd_open();
  pthread_mutex_init(&crystalhd_h264_lock, NULL);
}


/*
 * The plugin catalog entry. This is the only information that this plugin
 * will export to the public.
 */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API, "name", version, special_info, init_function } */
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "crystalhd_h264", XINE_VERSION_CODE, &dec_info_crystalhd_h264, init_h264_plugin },
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "crystalhd_vc1", XINE_VERSION_CODE, &dec_info_crystalhd_vc1, init_vc1_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

