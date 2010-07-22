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
 * crystalhd_hw.h: H264 Video Decoder utilizing Broadcom Crystal HD engine
 */

#ifndef CRYSTALHD_HW_H
#define CRYSTALHD_HW_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "crystalhd.h"

extern const char* g_DtsStatusText[];

HANDLE crystalhd_open();
/*
HANDLE crystalhd_close(xine_t *xine, HANDLE hDevice);
*/
HANDLE crystalhd_start(xine_t *xine, HANDLE hDevice, BCM_STREAM_TYPE stream_type, BCM_VIDEO_ALGO algo);
HANDLE crystalhd_stop(xine_t *xine, HANDLE hDevice);

#endif
