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
 * crystalhd_h264.c: H264 Video Decoder utilizing Broadcom Crystal HD engine
 */

#define LOG

#include "crystalhd_h264.h"

/**************************************************************************
 * crystalhd_h264 specific decode functions
 *************************************************************************/

void crystalhd_h264_free_parser (crystalhd_video_decoder_t *this) {
  if(this->completed_pic) {
    free_coded_picture(this->completed_pic);
    this->completed_pic = NULL;
  }
  free_parser(this->nal_parser);
  this->nal_parser = NULL;
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
void crystalhd_h264_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

  if(buf->decoder_flags & BUF_FLAG_FRAME_START || buf->decoder_flags & BUF_FLAG_FRAME_END) {
    this->have_frame_boundary_marks = 1;
  }

  if(buf->decoder_flags & BUF_FLAG_STDHEADER) {
    this->have_frame_boundary_marks = 0;

    xine_bmiheader *bih = (xine_bmiheader *) buf->content;
		//int size = bih->biSize;

		this->width		= bih->biWidth;
		this->height  = bih->biHeight;
    
		this->extradata_size = bih->biSize - sizeof(xine_bmiheader);
		this->extradata = valloc(this->extradata_size);
		xine_fast_memcpy(this->extradata, buf->content + sizeof(xine_bmiheader), this->extradata_size);

		if(this->extradata_size > 0) {
			parse_codec_private(this->nal_parser, this->extradata, this->extradata_size);
		}
	}  else if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    this->have_frame_boundary_marks = 0;

		if (buf->decoder_info[1] == BUF_SPECIAL_DECODER_CONFIG &&
				!this->extradata_size) {
			uint8_t *extradata = buf->decoder_info_ptr[2];
			uint32_t extradata_size = buf->decoder_info[2];
			this->extradata_size = extradata_size;
			this->extradata = valloc(extradata_size);
			xine_fast_memcpy(this->extradata, extradata, extradata_size);

			if(extradata_size > 0) {
				parse_codec_private(this->nal_parser, extradata, extradata_size);
			}
		}
  } else {
		int len = 0;
    decoder_buffer_t decode_buffer;
    decode_buffer.bytestream_bytes = 0;

		while(len < buf->size && !(this->wait_for_frame_start && !(buf->decoder_flags & BUF_FLAG_FRAME_START))) {

				this->wait_for_frame_start = 0;

			len += parse_frame(this->nal_parser, buf->content + len, buf->size - len,
          buf->pts,
          &decode_buffer.bytestream, &decode_buffer.bytestream_bytes, &this->completed_pic);

      if(this->completed_pic &&
          this->completed_pic->sps_nal != NULL &&
          this->completed_pic->sps_nal->sps.pic_width > 0 &&
          this->completed_pic->sps_nal->sps.pic_height > 0) {
        
        if(!this->set_form) {
          hDevice = crystalhd_start(this, hDevice, BC_STREAM_TYPE_ES, BC_VID_ALGO_H264, 0, NULL, 0, 0, 0,
              this->scaling_enable, this->scaling_width);
          this->set_form = 1;

          struct seq_parameter_set_rbsp *sps = &this->completed_pic->sps_nal->sps;

          if(sps->vui_parameters_present_flag &&
              sps->vui_parameters.timing_info_present_flag ) {
            this->video_step =  2*90000/(1/((double)sps->vui_parameters.num_units_in_tick/(double)sps->vui_parameters.time_scale));
          }
        };

        decode_buffer.pts = this->completed_pic->pts;

        if(this->completed_pic->pts) {
          this->last_pts = this->completed_pic->pts;
        }

        crystalhd_send_data(this, hDevice, decode_buffer.bytestream, decode_buffer.bytestream_bytes, this->last_pts);

      }
        
      if(decode_buffer.bytestream_bytes > 0) {
        free(decode_buffer.bytestream);
        decode_buffer.bytestream_bytes = 0;
      }

      if(this->completed_pic) {
        free_coded_picture(this->completed_pic);
        this->completed_pic = NULL;
      }

      /*
      if(this->nal_parser->last_nal_res == 3)
        crystalhd_h264_flush(this_gen);
      */
		}
	}

	if(buf->decoder_flags & BUF_FLAG_FRAME_END) {
    //printf("next pts: %lld\n", buf->pts);
		this->wait_for_frame_start = 0;
	}
}
