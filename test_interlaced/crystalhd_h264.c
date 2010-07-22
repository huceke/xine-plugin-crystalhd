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

#include "crystalhd.h"
#include "crystalhd_hw.h"

#include "cpb.h"
#include "h264_parser.h"

#define MAX_FRAME_SIZE  1024*1024

struct buf_reader
{
  uint8_t *buf;
  uint8_t *cur_pos;
  int len;
  int cur_offset;
};


typedef struct {
  video_decoder_class_t   decoder_class;
} crystalhd_h264_class_t;

typedef struct crystalhd_h264_decoder_s {
	video_decoder_t   video_decoder;  /* parent video decoder structure */

	crystalhd_h264_class_t *class;
	xine_stream_t    *stream;

	/* these are traditional variables in a video decoder object */

	double            ratio;

	xine_t            *xine;

	uint8_t         	*transferbuff;

	uint64_t          video_step;
  int               got_frame;
  int               start_decoding;

	int								width;
	int								height;
	int								y_size;
	int								uv_size;

	int								have_frame_boundary_marks;
	int								wait_for_frame_start;
	int								decoder_timeout;

  xine_list_t       *image_buffer;

	xine_bmiheader    *bih;
	unsigned char     *extradata;
	int               extradata_size;

	BC_DTS_PROC_OUT		procOut;

	struct h264_parser *nal_parser;  /* h264 nal parser. extracts stream data for vdpau */
  struct coded_picture *completed_pic;

	int								interlaced;

	int								last_image;

	pthread_t         rec_thread;
	int								rec_thread_stop;

  metronom_clock_t  *clock;

  pthread_mutex_t   rec_mutex;

  int               reset;

  int64_t           last_pts;

} crystalhd_h264_decoder_t;

static void crystalhd_h264_flush (video_decoder_t *this_gen);
static void crystalhd_h264_reset (video_decoder_t *this_gen);

/**************************************************************************
 * crystalhd_h264 specific decode functions
 *************************************************************************/

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

static void set_video_step(crystalhd_h264_decoder_t *this, uint32_t frame_rate) {
	switch(frame_rate) {
		case vdecRESOLUTION_720p:
		case vdecRESOLUTION_576p:
		case vdecRESOLUTION_480p:
			this->video_step = 90000/60;
			break;
		case vdecRESOLUTION_SD_DVD:
			this->video_step = 90000/50;
			break;
		case vdecRESOLUTION_PAL1:
			this->video_step = 90000/50;
			break;
		case vdecRESOLUTION_NTSC:
			this->video_step = 90000/60;
			break;
		case vdecRESOLUTION_720p50:
			this->video_step = 90000/50;
			break;
		case vdecRESOLUTION_1080i25:
			this->video_step = 90000/25;
			break;
		case vdecRESOLUTION_1080p30:
		case vdecRESOLUTION_240p30:
			this->video_step = 90000/30;
			break;
		case vdecRESOLUTION_1080p25:
		case vdecRESOLUTION_576p25:
		case vdecRESOLUTION_288p25:
			this->video_step = 90000/25;
			break;
		case vdecRESOLUTION_1080p24:
		case vdecRESOLUTION_720p24:
			this->video_step = 90000/24;
			break;
		case vdecRESOLUTION_1080i29_97:
		case vdecRESOLUTION_1080p29_97:
		case vdecRESOLUTION_720p29_97:
		case vdecRESOLUTION_480p29_97:
		case vdecRESOLUTION_240p29_97:
			this->video_step = 90000/29.97;
			break;
		case vdecRESOLUTION_1080p23_976:
		case vdecRESOLUTION_720p23_976:
		case vdecRESOLUTION_480p23_976:
		case vdecRESOLUTION_1080p0:
		case vdecRESOLUTION_576p0:
		case vdecRESOLUTION_720p0:
		case vdecRESOLUTION_480p0:
			this->video_step = 90000/23.976;
			break;
		case vdecRESOLUTION_1080i:
		case vdecRESOLUTION_480i:
		case vdecRESOLUTION_1080i0:
		case vdecRESOLUTION_480i0:
			this->video_step = 90000/25;
			break;
		case vdecRESOLUTION_720p59_94:
			//this->video_step = 90000/59.94;
			this->video_step = 90000/59.94/2;
			break;
		case vdecRESOLUTION_CUSTOM:
		case vdecRESOLUTION_480p656:
		default:
			this->video_step = 90000/25;
			break;
	}
}

static void set_ratio(crystalhd_h264_decoder_t *this, uint32_t aspect_ratio) {
	lprintf("aspect_ratio %d\n", aspect_ratio);

	this->ratio = (double)this->width / (double)this->height;
	switch(aspect_ratio) {
    case vdecAspectRatio12_11:
			this->ratio *= 12.0/11.0;
	  case vdecAspectRatio10_11:
    	this->ratio *= 10.0/11.0;
    	break;
    case vdecAspectRatio16_11:
      this->ratio *= 16.0/11.0;
      break;
    case vdecAspectRatio40_33:
      this->ratio *= 40.0/33.0;
      break;
    case vdecAspectRatio24_11:
      this->ratio *= 24.0/11.0;
      break;
    case vdecAspectRatio20_11:
      this->ratio *= 20.0/11.0;
      break;
    case vdecAspectRatio32_11:
      this->ratio *= 32.0/11.0;
      break;
    case vdecAspectRatio80_33:
      this->ratio *= 80.0/33.0;
      break;
    case vdecAspectRatio18_11:
      this->ratio *= 18.0/11.0;
      break;
    case vdecAspectRatio15_11:
      this->ratio *= 15.0/11.0;
      break;
    case vdecAspectRatio64_33:
      this->ratio *= 64.0/33.0;
      break;
    case vdecAspectRatio160_99:
      this->ratio *= 160.0/99.0;
      break;
    case vdecAspectRatio4_3:
      this->ratio *= 4.0/3.0;
      break;
    case vdecAspectRatio16_9:
      this->ratio *= 16.0 / 9.0;
      break;
    case vdecAspectRatio221_1:
      this->ratio *= 2.0/1.0;
      break;
		case vdecAspectRatioUnknown:
		case vdecAspectRatioOther:
		default:
			this->ratio = (double)this->width / (double)this->height;
			break;
	}
}

static void print_setup(crystalhd_h264_decoder_t *this) {
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->width %d\n", this->width);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->height %d\n", this->height);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->interlaced %d\n", this->interlaced);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->video_step %ld\n", this->video_step);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->ratio %f\n", this->ratio);
}

static void set_video_params (crystalhd_h264_decoder_t *this) {
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*this->ratio) );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step );
	_x_meta_info_set_utf8( this->stream, XINE_META_INFO_VIDEOCODEC, "H264/AVC (crystalhd)" );
  xine_set_param(this->stream, XINE_PARAM_VO_DEINTERLACE, this->interlaced);
	xine_event_t event;
	xine_format_change_data_t data;
	event.type = XINE_EVENT_FRAME_FORMAT_CHANGE;
	event.stream = this->stream;
	event.data = &data;
	event.data_length = sizeof(data);
	data.width = this->width;
	data.height = this->height;
	data.aspect = this->ratio;
	data.pan_scan = 1;
	xine_event_send( this->stream, &event );

	print_setup(this);
} 

static void copy_frame(crystalhd_h264_decoder_t *this, image_buffer_t *img) {
  uint8_t *pDest;
  int line_width = img->stride/2;
  int offset = 0;
  int y;

  pDest = malloc(1920*1088*2);

  //memset(pDest, 0x80, 1920*1088*2);

  /*
  if(img->flags == VO_BOTTOM_FIELD) {
    offset = line_width*4;
  }
  */

  offset = line_width*4;

  for(y = img->height/2; y >= 0; y--) {
    xine_fast_memcpy(
        pDest + (y * line_width * 2) * 4,
        img->image + (y * line_width) * 4,
        line_width * 4
        );
    xine_fast_memcpy(
        pDest + (y * line_width * 2) * 4 + offset,
        img->image + (y * line_width) * 4,
        line_width * 4
        );
  }

  free(img->image);

  img->image = pDest;
  
}

static void crystalhd_h264_render (crystalhd_h264_decoder_t *this) {
	xine_list_iterator_t ite;

	ite = xine_list_front(this->image_buffer);

  if(ite!= NULL) {
  	image_buffer_t *img	= xine_list_get_value(this->image_buffer, ite);
		xine_list_remove(this->image_buffer, ite);

  	if(img->image_bytes > 0) {

      vo_frame_t	*vo_img;

     	vo_img = this->stream->video_out->get_frame (this->stream->video_out,
                            img->width, img->height, this->ratio, 
                            XINE_IMGFMT_YUY2, /*img->flags | this->reseti */ VO_BOTH_FIELDS);

      this->reset = 0;

     	yuy2_to_yuy2(
    		  	img->image, img->width * 2,
 		      	vo_img->base[0], vo_img->pitches[0],
 		  	    img->width, img->height);
     	vo_img->pts			 = img->pts;
     	vo_img->duration = this->video_step;
      vo_img->bad_frame = 0;

      if(this->interlaced) {
        vo_img->progressive_frame = 0;
        vo_img->top_field_first = 1; /*(img->flags == VO_TOP_FIELD) ? 1 : 0;*/
      } else {
        vo_img->progressive_frame = 1;
        vo_img->top_field_first = 0;
      }

     	vo_img->draw(vo_img, this->stream);

     	vo_img->free(vo_img);
    }

  	free(img->image);
  	free(img);
  }
}

static void* crystalhd_h264_rec_thread (void *this_gen) {
	crystalhd_h264_decoder_t *this = (crystalhd_h264_decoder_t *) this_gen;
	BC_STATUS ret;
	BC_DTS_STATUS pStatus;

	while(!this->rec_thread_stop) {

		/* read driver status. we need the frame ready count from it */
		if(!this->got_frame) {
			ret = DtsGetDriverStatus(hDevice, &pStatus);
			if( ret == BC_STS_SUCCESS && pStatus.ReadyListCount > 0) {
				this->got_frame = 1;
			}
		}
		
		if( this->got_frame && this->start_decoding) {

			memset(&this->procOut, 0, sizeof(BC_DTS_PROC_OUT));

			/* setup frame transfer structure */
			this->procOut.PicInfo.width = this->width;
			this->procOut.PicInfo.height = this->height;
			this->procOut.YbuffSz = this->y_size/4;
			this->procOut.UVbuffSz = this->uv_size/4;
			this->procOut.PoutFlags = BC_POUT_FLAGS_SIZE;
	
			this->procOut.PicInfo.picture_number = 0;
	
			if(this->transferbuff == NULL) {
				this->transferbuff = malloc(1920*1088*2);
			}
			this->procOut.Ybuff = this->transferbuff;
			this->procOut.PoutFlags = this->procOut.PoutFlags & 0xff;

			if(this->interlaced) {
				this->procOut.PoutFlags |= BC_POUT_FLAGS_INTERLACED;
			}
	
      //pthread_mutex_lock(&this->rec_mutex);
			ret = DtsProcOutput(hDevice, this->decoder_timeout, &this->procOut);
      //pthread_mutex_unlock(&this->rec_mutex);

			/* print statistics */
			switch (ret) {
				case BC_STS_NO_DATA:
					break;
	     	case BC_STS_FMT_CHANGE:
	       	if ((this->procOut.PoutFlags & BC_POUT_FLAGS_PIB_VALID) && 
							(this->procOut.PoutFlags & BC_POUT_FLAGS_FMT_CHANGE) ) {
	
						this->interlaced = (this->procOut.PicInfo.flags & VDEC_FLAG_INTERLACED_SRC ? 1 : 0);
	
	       		this->width = this->procOut.PicInfo.width;
						this->height = this->procOut.PicInfo.height;
						if(this->height == 1088) this->height = 1080;

				  	this->y_size = this->width * this->height;

						this->uv_size = 0;
	
						this->decoder_timeout = 2500;
				
						set_ratio(this, this->procOut.PicInfo.aspect_ratio);
						set_video_step(this, this->procOut.PicInfo.frame_rate);
						set_video_params(this);
	   	   	}
					break;
				case BC_STS_SUCCESS:
	     	 	if (this->procOut.PoutFlags & BC_POUT_FLAGS_PIB_VALID) {
	
						if(this->last_image == 0) {
							this->last_image = this->procOut.PicInfo.picture_number;
						}

            if((this->procOut.PicInfo.picture_number - this->last_image) > 0 ) {

							xprintf(this->xine, XINE_VERBOSITY_LOG,"ReadyListCount %d FreeListCount %d PIBMissCount %d picture_number %d gap %d tiemStamp %ld\n",
									pStatus.ReadyListCount, pStatus.FreeListCount, pStatus.PIBMissCount, 
									this->procOut.PicInfo.picture_number, 
									this->procOut.PicInfo.picture_number - this->last_image,
                  this->procOut.PicInfo.timeStamp);

              if((this->procOut.PicInfo.picture_number - this->last_image) > 1) {
							  xprintf(this->xine, XINE_VERBOSITY_NONE,"ReadyListCount %d FreeListCount %d PIBMissCount %d picture_number %d gap %d tiemStamp %ld\n",
									pStatus.ReadyListCount, pStatus.FreeListCount, pStatus.PIBMissCount, 
									this->procOut.PicInfo.picture_number, 
									this->procOut.PicInfo.picture_number - this->last_image,
                  this->procOut.PicInfo.timeStamp);
							  //xprintf(this->xine, XINE_VERBOSITY_NONE,"Lost frame\n");
              }

              xprintf(this->xine, XINE_VERBOSITY_LOG,"VDEC_FLAG_BOTTOMFIELD %d VDEC_FLAG_TOPFIELD %d VDEC_FLAG_FIELDPAIR %d VDEC_FLAG_PROGRESSIVE_SRC %d VDEC_FLAG_INTERLACED_SRC% d\n", 
                  this->procOut.PicInfo.flags & VDEC_FLAG_BOTTOMFIELD,
                  this->procOut.PicInfo.flags & VDEC_FLAG_TOPFIELD,
                  this->procOut.PicInfo.flags & VDEC_FLAG_FIELDPAIR,
                  this->procOut.PicInfo.flags & VDEC_FLAG_PROGRESSIVE_SRC,
                  this->procOut.PicInfo.flags & VDEC_FLAG_INTERLACED_SRC);

							if(this->procOut.PicInfo.picture_number != this->last_image) {
								this->last_image = this->procOut.PicInfo.picture_number;
							}

							/* allocate new image buffer and push it to the image list */
							image_buffer_t *img = valloc(sizeof(image_buffer_t));
							
							img->image = this->transferbuff;
							img->image_bytes = this->procOut.YbuffSz;
							img->width = this->width;
							img->height = this->height;
							img->pts = this->procOut.PicInfo.timeStamp;
						 	img->video_step = this->video_step;
							img->interlaced = this->interlaced;
							img->picture_number = this->procOut.PicInfo.picture_number;

              if(img->width <= 720 ) {
                img->stride = 720;
              } else if (img->width <= 1280 ) {
                img->stride = 1280;
              } else {
                img->stride = 1920;
              }

              if(this->interlaced) {
                if(this->procOut.PicInfo.picture_number & 1) {
                  img->flags = VO_BOTTOM_FIELD;
                } else {
                  img->flags = VO_TOP_FIELD;
                }

                copy_frame(this, img);
              } else {
                img->flags = VO_BOTH_FIELDS;
              }

							this->transferbuff = NULL;

						  xine_list_push_back(this->image_buffer, img);

						}
					}
					break;
	   		default:
	      	if (ret > 26) {
		        	lprintf("DtsProcOutput returned %d.\n", ret);
		     	} else {
	  	   	  	lprintf("DtsProcOutput returned %s.\n", g_DtsStatusText[ret]);
					}
	       	break;
	   	}
		}

    Sleep(5);

	}

	pthread_exit(NULL);
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void crystalhd_h264_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {

  crystalhd_h264_decoder_t *this = (crystalhd_h264_decoder_t *) this_gen;

  /*
  if(buf->pts) {
    this->last_pts = buf->pts;
  } 

  if(this->last_pts) {
    char *buff = valloc(buf->size);

    this->start_decoding = 1;
  
    memcpy(buff, buf->content, buf->size);
    DtsProcInput(hDevice, buff, buf->size, buf->pts, 0);
    free(buff);
  }
  */

  //crystalhd_h264_render(this);

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
		return;
	}

  if(buf->decoder_flags & BUF_FLAG_FRAME_START || buf->decoder_flags & BUF_FLAG_FRAME_END) {
    this->have_frame_boundary_marks = 1;
  }

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
  }

	if(buf->decoder_flags & BUF_FLAG_STDHEADER) {
    this->have_frame_boundary_marks = 0;

		int size = ((xine_bmiheader *) buf->content)->biSize;
		if(!this->bih) this->bih = valloc(size);
		xine_fast_memcpy(this->bih, buf->content, sizeof(xine_bmiheader));
		this->width		= this->bih->biWidth;
		this->height  = this->bih->biHeight;

		uint8_t *extradata = buf->content + sizeof(xine_bmiheader);
		uint32_t extradata_size = this->bih->biSize - sizeof(xine_bmiheader);
		this->extradata_size = extradata_size;
		this->extradata = valloc(extradata_size);
		xine_fast_memcpy(this->extradata, extradata, extradata_size);

		if(extradata_size > 0) {
			parse_codec_private(this->nal_parser, extradata, extradata_size);
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
        
        this->start_decoding = 1;

        decode_buffer.pts = this->completed_pic->pts;

	      BC_STATUS ret;
        
   	 	  ret = DtsProcInput(hDevice, decode_buffer.bytestream, decode_buffer.bytestream_bytes, this->completed_pic->pts, 0);

    	  if (ret == BC_STS_BUSY) {
	    	  xprintf(this->xine, XINE_VERBOSITY_LOG,"decoder BC_STS_BUSY\n");
          DtsFlushInput(hDevice, 4);
   	 	    DtsProcInput(hDevice, decode_buffer.bytestream, decode_buffer.bytestream_bytes, this->completed_pic->pts, 0);
        }

        crystalhd_h264_render(this);
      }
        
      if(decode_buffer.bytestream_bytes > 0) {
        free(decode_buffer.bytestream);
        decode_buffer.bytestream_bytes = 0;
      }

      if(this->completed_pic) {
        free_coded_picture(this->completed_pic);
      }

      if(this->nal_parser->last_nal_res == 3)
        crystalhd_h264_flush(this_gen);
		}
	}

	if(buf->decoder_flags & BUF_FLAG_FRAME_END) {
    //printf("next pts: %lld\n", buf->pts);
		this->wait_for_frame_start = 0;
	}
}

static void crystalhd_h264_clear_all_pts(crystalhd_h264_decoder_t *this) {

	xine_list_iterator_t ite;
	while ((ite = xine_list_front(this->image_buffer)) != NULL) {
		image_buffer_t	*img = xine_list_get_value(this->image_buffer, ite);
    img->pts = 0;
	}

	if(hDevice) {
		DtsFlushInput(hDevice, 2);
	}

}

static void crystalhd_h264_clear_worker_buffers(crystalhd_h264_decoder_t *this) {
	xine_list_iterator_t ite;

  lprintf("crystalhd_h264_clear_worker_buffers enter\n");

  this->got_frame = 0;

	if(hDevice) {
		DtsFlushInput(hDevice, 2);
	}

	while ((ite = xine_list_front(this->image_buffer)) != NULL) {
		image_buffer_t	*img = xine_list_get_value(this->image_buffer, ite);
		free(img->image);
		free(img);
		xine_list_remove(this->image_buffer, ite);
	}

  lprintf("crystalhd_h264_clear_worker_buffers leave\n");
}

static void crystalhd_h264_destroy_workers(crystalhd_h264_decoder_t *this) {

	if(this->rec_thread) {
		this->rec_thread_stop = 1;
    pthread_join(this->rec_thread, NULL);
	}

  pthread_mutex_destroy(&this->rec_mutex);

}

static void crystalhd_h264_setup_workers(crystalhd_h264_decoder_t *this) {
				
  pthread_attr_t thread_attr;
  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
  pthread_create(&this->rec_thread, &thread_attr,crystalhd_h264_rec_thread,(void *)this);
  pthread_attr_destroy(&thread_attr);

  pthread_mutexattr_t mutex_attr;
  pthread_mutexattr_init(&mutex_attr);
  pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_PRIVATE);
  pthread_mutex_init(&this->rec_mutex, &mutex_attr);
  pthread_mutexattr_destroy(&mutex_attr);

}

/*
 * This function is called when xine needs to flush the system.
 */
static void crystalhd_h264_flush (video_decoder_t *this_gen) {
  crystalhd_h264_decoder_t *this = (crystalhd_h264_decoder_t*) this_gen;

  crystalhd_h264_clear_worker_buffers(this);

  this->reset = VO_NEW_SEQUENCE_FLAG;

  xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_h264: crystalhd_h264_decode_flush\n");
}

/*
 * This function resets the video decoder.
 */
static void crystalhd_h264_reset (video_decoder_t *this_gen) {
  crystalhd_h264_decoder_t *this = (crystalhd_h264_decoder_t *) this_gen;

  crystalhd_h264_clear_worker_buffers(this);

  // Doing a full parser reinit here works more reliable than
  // resetting
  
  //reset_parser(this->nal_parser);
  free_parser(this->nal_parser);
  this->nal_parser = init_parser(this->xine);

  this->last_image        = 0;

	crystalhd_h264_clear_worker_buffers(this);

	if(this->extradata_size > 0) {
		parse_codec_private(this->nal_parser, this->extradata, this->extradata_size);
    this->wait_for_frame_start = this->have_frame_boundary_marks;
	}

  this->reset = VO_NEW_SEQUENCE_FLAG;

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_h264: crystalhd_h264_reset\n");
}

/*
 * The decoder should forget any stored pts values here.
 */
static void crystalhd_h264_discontinuity (video_decoder_t *this_gen) {
  crystalhd_h264_decoder_t *this = (crystalhd_h264_decoder_t *) this_gen;

  crystalhd_h264_clear_all_pts(this);

  this->reset = VO_NEW_SEQUENCE_FLAG;

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_h264: crystalhd_h264_discontinuity\n");
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void crystalhd_h264_dispose (video_decoder_t *this_gen) {

  crystalhd_h264_decoder_t *this = (crystalhd_h264_decoder_t *) this_gen;

	crystalhd_h264_destroy_workers(this);

	hDevice = crystalhd_stop(this->xine, hDevice);

	if( this->extradata ) {
		free( this->extradata );
		this->extradata = NULL;
	}

	if(this->bih) {
		free(this->bih);
		this->bih = NULL;
	}

	free(this->transferbuff);
	this->transferbuff = NULL;

	crystalhd_h264_clear_worker_buffers(this);

  free_parser (this->nal_parser);
  free (this_gen);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_h264: crystalhd_h264_dispose\n");
}

/*
 * exported plugin catalog entry
 */
/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  crystalhd_h264_decoder_t  *this ;

  this = (crystalhd_h264_decoder_t *) calloc(1, sizeof(crystalhd_h264_decoder_t));

  this->video_decoder.decode_data         = crystalhd_h264_decode_data;
  this->video_decoder.flush               = crystalhd_h264_flush;
  this->video_decoder.reset               = crystalhd_h264_reset;
  this->video_decoder.discontinuity       = crystalhd_h264_discontinuity;
  this->video_decoder.dispose             = crystalhd_h264_dispose;


  this->stream                            = stream;
  this->xine                              = stream->xine;
  this->class                             = (crystalhd_h264_class_t *) class_gen;
  this->clock                             = this->xine->clock;

	this->wait_for_frame_start = 0;
	this->transferbuff      = NULL;

  this->video_step  	    = 0;
  this->ratio  				    = 0;
  this->got_frame         = 0;

	this->width							= 1920;
	this->height						= 1082;
	this->y_size						= this->width * this->height * 2;
	this->uv_size						= 0;

	this->bih								= NULL;
	this->extradata					= NULL;

	this->start_decoding		= 0;
  this->decoder_timeout   = 2500;

	memset(&this->procOut, 0, sizeof(BC_DTS_PROC_OUT));

	this->interlaced        = 0;
	this->last_image				= 0;

	this->rec_thread_stop	  = 0;

	this->image_buffer      = xine_list_new();

  this->nal_parser        = init_parser(this->xine);

	if(hDevice) {
    hDevice = crystalhd_start(this->xine, hDevice, BC_STREAM_TYPE_ES, BC_VID_ALGO_H264);
	}

  this->reset            = VO_NEW_SEQUENCE_FLAG;

  this->last_pts         = 0;

	crystalhd_h264_setup_workers(this);

  return &this->video_decoder;
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
void *init_h264_plugin (xine_t *xine, void *data) {

  crystalhd_h264_class_t *this;

  this = (crystalhd_h264_class_t *) calloc(1, sizeof(crystalhd_h264_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "crystalhd_h264";
  this->decoder_class.description     =
	N_("crystalhd_h264: h264 decoder plugin using CrystalHD hardware decoding.");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  pthread_once( &once_control, init_once_routine );

  return this;
}

/*
 * This is a list of all of the internal xine video buffer types that
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0.
 */
uint32_t video_types_h264[] = {
  /* BUF_VIDEO_FOOVIDEO, */
  BUF_VIDEO_H264,
	/*BUF_VIDEO_MPEG,*/
  0
};

/*
 * This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n).
 */
decoder_info_t dec_info_crystalhd_h264 = {
  video_types_h264,    /* supported types */
  7                    /* priority        */
};
