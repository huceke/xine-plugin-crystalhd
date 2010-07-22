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
 * crystalhd.h: H264 Video Decoder utilizing Broadcom Crystal HD engine
 */

#ifndef CRYSTALHD_H
#define CRYSTALHD_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define XINE_ENGINE_INTERNAL

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

#ifndef __LINUX_USER__
#define __LINUX_USER__
#endif

#include <bc_dts_types.h>
#include <bc_dts_defs.h>
#include <libcrystalhd_if.h>

#include <semaphore.h>

extern HANDLE hDevice;

void *init_h264_plugin (xine_t *xine, void *data);
void *init_vc1_plugin (xine_t *xine, void *data);

extern decoder_info_t dec_info_crystalhd_h264;
extern decoder_info_t dec_info_crystalhd_vc1;

void Sleep(int ms);
void crystalhd_decode_package (uint8_t *buf, uint32_t size);

extern pthread_once_t once_control;
void init_once_routine(void);

extern pthread_mutex_t crystalhd_lock;

typedef struct decoder_buffer_s {
	uint8_t		*bytestream;
 	uint32_t	bytestream_bytes;
	int64_t   pts;
} decoder_buffer_t;

typedef struct image_buffer_s {
	uint8_t		*image;
 	uint32_t	image_bytes;
	int				width;
	int				height;
	int64_t   pts;
	uint64_t  video_step;
	uint32_t  flags;
	int				interlaced;
	uint32_t	picture_number;
  uint32_t  stride;
} image_buffer_t;

typedef uint32_t BCM_STREAM_TYPE;
typedef uint32_t BCM_VIDEO_ALGO;

#endif
