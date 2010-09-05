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
 */

#ifndef CRYSTALHD_H
#define CRYSTALHD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

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

#include "bits_reader.h"

#include "cpb.h"
#include "h264_parser.h"

extern HANDLE hDevice;

extern const char* g_DtsStatusText[];

typedef struct decoder_buffer_s {
	uint8_t		*bytestream;
 	uint32_t	bytestream_bytes;
	uint64_t   pts;
} decoder_buffer_t;

typedef struct image_buffer_s {
	uint8_t		*image;
 	uint32_t	image_bytes;
	int				width;
	int				height;
	uint64_t  pts;
  double    ratio;
	uint32_t  video_step;
	uint32_t  flags;
	int			  interlaced;
	uint32_t	picture_number;
  uint32_t  stride;
} image_buffer_t;

/* MGED Picture */
typedef struct {
  int                     slices_count, slices_count2;
  uint8_t                 *slices;
  int                     slices_size;
  int                     slices_pos, slices_pos_top;

  int                     progressive_frame;
  int                     state;
  int                     picture_structure;
} picture_mpeg_t;


/* MGEG Sequence */
typedef struct {
  uint32_t    coded_width;
  uint32_t    coded_height;

  int         profile;
  int         chroma;
  int         top_field_first;

  int         have_header;

  uint8_t     *buf; /* accumulate data */
  int         bufseek;
  uint32_t    bufsize;
  uint32_t    bufpos;
  int         start;

  picture_mpeg_t   picture;

  int64_t    cur_pts, seq_pts;

  bits_reader_t  br;

  int         reset;
} sequence_mpeg_t;

/* VC1 Picture */
typedef struct {
  int         slices;
  int         fptype;
  int         field;
  int         header_size;
  int         hrd_param_flag;
  int         hrd_num_leaky_buckets;
  int         skipped;
  uint8_t     picture_vc1_type;
  uint8_t     finterpflag;
  uint8_t     maxbframes;
  uint8_t     interlace;
  uint8_t     rangered;
  uint8_t     frame_coding_mode;
} picture_vc1_t;

/* VC1 Sequence */
typedef struct {
  uint32_t    coded_width;
  uint32_t    coded_height;

  uint32_t    profile;

  int         mode;
  int         have_header;

  uint8_t     *buf; /* accumulate data */
  int         bufseek;
  int         start;
  int         code_start, current_code;
  uint32_t    bufsize;
  uint32_t    bufpos;

  picture_vc1_t   picture;
  uint64_t    seq_pts;
  uint64_t    cur_pts;

  uint8_t     *bytestream;
  uint32_t    bytestream_bytes;

  bits_reader_t br;
} sequence_vc1_t;

typedef struct {
  video_decoder_class_t   decoder_class;
} crystalhd_video_class_t;

struct buf_reader
{
  uint8_t *buf;
  uint8_t *cur_pos;
  int len;
  int cur_offset;
};

typedef struct crystalhd_video_decoder_s {
	video_decoder_t   video_decoder;  /* parent video decoder structure */

	crystalhd_video_class_t *class;
	xine_stream_t    *stream;
	xine_t            *xine;

	/* these are traditional variables in a video decoder object */
	double            ratio;
	uint32_t          video_step;
  uint32_t          reported_video_step;  /* frame duration in pts units */
  uint64_t          last_pts;
  int               reset;

	int								width;
	int								height;
	int								y_size;
	int								uv_size;

  sequence_mpeg_t   sequence_mpeg;

	sequence_vc1_t    sequence_vc1;

  struct h264_parser *nal_parser;
  struct coded_picture *completed_pic;

	int								interlaced;
	int								last_image;
  int               got_frame;
  int               have_frame_boundary_marks;
  int               wait_for_frame_start;

	xine_list_t       *image_buffer;

	pthread_t         rec_thread;
	int								rec_thread_stop;
	pthread_mutex_t		rec_mutex;

  int               set_form;

  unsigned char     *extradata;
  int               extradata_size;

  uint32_t          deocder_type;

  int               scaling_enable;
  int               scaling_width;
  int               use_threading;
  int               extra_logging;
  int               decoder_reopen;
  int               decoder_25p;
  int               decoder_25p_drop;
} crystalhd_video_decoder_t;

typedef uint32_t BCM_STREAM_TYPE;
typedef uint32_t BCM_VIDEO_ALGO;

void *crystalhd_video_rec_thread (void *this_gen);
void crystalhd_decode_package (uint8_t *buf, uint32_t size);
void set_video_params (crystalhd_video_decoder_t *this);

#endif
