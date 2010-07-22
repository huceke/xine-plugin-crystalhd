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

#ifndef CRYSTALHD_VC1_H
#define CRYSTALHD_VC1_H

#include "crystalhd_decoder.h"
#include "crystalhd_hw.h"

void crystalhd_vc1_reset_picture( picture_vc1_t *pic );
void crystalhd_vc1_init_picture( picture_vc1_t *pic );
void crystalhd_vc1_reset_sequence( sequence_vc1_t *sequence );
void crystalhd_vc1_init_sequence( sequence_vc1_t *sequence );
void crystalhd_vc1_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf);

#endif
