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
 * crystalhd_decoder.c: Main video decoder routines
 */

#define LOG

#include "crystalhd_decoder.h"
#include "crystalhd_hw.h"
#include "crystalhd_vc1.h"
#include "crystalhd_h264.h"
#include "crystalhd_mpeg.h"

static void crystalhd_video_flush (video_decoder_t *this);
static void crystalhd_video_destroy_workers(crystalhd_video_decoder_t *this);
static void crystalhd_video_setup_workers(crystalhd_video_decoder_t *this);
static void crystalhd_video_clear_worker_buffers(crystalhd_video_decoder_t *this);

HANDLE hDevice = 0;

int __nsleep(const struct  timespec *req, struct timespec *rem) {
  struct timespec temp_rem;
  if(nanosleep(req,rem)==-1)
    __nsleep(rem,&temp_rem);

  return 1;
}

int msleep(unsigned long milisec) {
  struct  timespec req={0},rem={0};
  time_t sec=(int)(milisec/1000);
  milisec=milisec-(sec*1000);
  req.tv_sec=sec;
  req.tv_nsec=milisec*1000000L;
  __nsleep(&req,&rem);
  return 1;
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

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

void print_setup(crystalhd_video_decoder_t *this) {
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->width %d\n", this->width);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->height %d\n", this->height);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->interlaced %d\n", this->interlaced);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->video_step %d\n", this->video_step);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->reported_video_step %d\n", this->reported_video_step);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->ratio %f\n", this->ratio);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->decoder_25p %d\n", this->decoder_25p);
}

void set_video_params (crystalhd_video_decoder_t *this) {

  this->decoder_25p = 0;

  if(this->decoder_25p_drop) {
    if(this->video_step < 3000) {
      xprintf(this->xine, XINE_VERBOSITY_LOG, "enable frame drop hack\n");
      this->decoder_25p = 1;
      this->video_step = this->video_step * 2;
    }
  }

	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*this->ratio) );
  _x_stream_info_set( this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step));
	_x_meta_info_set_utf8( this->stream, XINE_META_INFO_VIDEOCODEC, "crystalhd decoder" );

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

static void crystalhd_video_render (crystalhd_video_decoder_t *this, image_buffer_t *_img) {

	xine_list_iterator_t ite = NULL;
 	image_buffer_t *img	= NULL;

  if(this->use_threading) {
    //pthread_mutex_lock(&this->rec_mutex);
	  ite = xine_list_front(this->image_buffer);
    if(ite!= NULL) {
  	  img	= xine_list_get_value(this->image_buffer, ite);
		  xine_list_remove(this->image_buffer, ite);
    }
    //pthread_mutex_unlock(&this->rec_mutex);
  } else {
    img = _img;
  }

 	if(img != NULL && img->image_bytes > 0) {
    vo_frame_t	*vo_img;

   	vo_img = this->stream->video_out->get_frame (this->stream->video_out,
                      img->width, (img->interlaced) ? img->height / 2 : img->height, img->ratio, 
               				XINE_IMGFMT_YUY2, VO_BOTH_FIELDS | VO_PAN_SCAN_FLAG | this->reset);

    this->reset = 0;

   	yuy2_to_yuy2(
    		  	img->image, img->width * 2,
 		      	vo_img->base[0], vo_img->pitches[0],
 		  	    img->width, (img->interlaced) ? img->height / 2 : img->height);
   	vo_img->pts			 = img->pts;
   	vo_img->duration = img->video_step;
    vo_img->bad_frame = 0;

   	vo_img->draw(vo_img, this->stream);

   	vo_img->free(vo_img);
  }

  if(img != NULL && this->use_threading) {
  	free(img->image);
  }
  free(img);
}

static void* crystalhd_video_rec_thread (void *this_gen) {
	crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

	BC_STATUS         ret;
	BC_DTS_STATUS     pStatus;
  BC_DTS_PROC_OUT		procOut;
	unsigned char   	*transferbuff = NULL;
	int								decoder_timeout = 16;

	while(!this->rec_thread_stop) {
	
    if(!this->set_form) {
      if(!this->use_threading) {
        return NULL;
      }
      msleep(10);
      continue;
    }

		/* read driver status. we need the frame ready count from it */
    ret = DtsGetDriverStatus(hDevice, &pStatus);

		if( ret == BC_STS_SUCCESS && pStatus.ReadyListCount) {

			memset(&procOut, 0, sizeof(BC_DTS_PROC_OUT));

		  if(this->interlaced) {
			  procOut.PoutFlags |= BC_POUT_FLAGS_INTERLACED;
		  }
      procOut.b422Mode = OUTPUT_MODE422_YUY2;

      if(this->use_threading) {
			
        /* setup frame transfer structure */
			  procOut.PicInfo.width = this->width;
			  procOut.PicInfo.height = this->height;
			  procOut.YbuffSz = this->y_size/4;
			  procOut.UVbuffSz = this->uv_size/4;
			  procOut.PoutFlags = BC_POUT_FLAGS_SIZE;
	
			  procOut.PicInfo.picture_number = 0;
	
			  if(transferbuff == NULL) {
				  transferbuff = malloc(this->y_size);
		  	}
			  procOut.Ybuff = transferbuff;

			  procOut.PoutFlags = procOut.PoutFlags & 0xff;
	
  			ret = DtsProcOutput(hDevice, decoder_timeout, &procOut);
      } else {	
  			ret = DtsProcOutputNoCopy(hDevice, decoder_timeout, &procOut);
      }

			/* print statistics */
			switch (ret) {
				case BC_STS_NO_DATA:
					break;
	     	case BC_STS_FMT_CHANGE:
	       	if ((procOut.PoutFlags & BC_POUT_FLAGS_PIB_VALID) && 
							(procOut.PoutFlags & BC_POUT_FLAGS_FMT_CHANGE) ) {
	
            // Does not work with 0.9.27
						this->interlaced = (procOut.PicInfo.flags & VDEC_FLAG_INTERLACED_SRC ? 1 : 0);
						//this->interlaced = (procOut.PicInfo.flags & VDEC_FLAG_FIELDPAIR ? 1 : 0);
	
	       		this->width = procOut.PicInfo.width;
						this->height = procOut.PicInfo.height;
						if(this->height == 1088) this->height = 1080;

						if(this->interlaced) {
							this->y_size = this->width * this->height;
						} else {
							this->y_size = this->width * this->height * 2;
						}

						this->uv_size = 0;
	
						decoder_timeout = 16;
				
						this->ratio = set_ratio(this->width, this->height, procOut.PicInfo.aspect_ratio);
            set_video_params(this);
	   	   	}
					break;
				case BC_STS_SUCCESS:
	     	 	if (procOut.PoutFlags & BC_POUT_FLAGS_PIB_VALID) {
	
						if(this->last_image == 0) {
							this->last_image = procOut.PicInfo.picture_number;
						}

            if((procOut.PicInfo.picture_number - this->last_image) > 0 ) {

              if(this->extra_logging) {
                fprintf(stderr,"ReadyListCount %d FreeListCount %d PIBMissCount %d picture_number %d gap %d tiemStamp %" PRId64 " YbuffSz %d YBuffDoneSz %d\n",
									pStatus.ReadyListCount, pStatus.FreeListCount, pStatus.PIBMissCount, 
									procOut.PicInfo.picture_number, 
									procOut.PicInfo.picture_number - this->last_image,
                  procOut.PicInfo.timeStamp,
                  procOut.YbuffSz, procOut.YBuffDoneSz);
              }

              if((procOut.PicInfo.picture_number - this->last_image) > 1) {
							  xprintf(this->xine, XINE_VERBOSITY_NONE,"ReadyListCount %d FreeListCount %d PIBMissCount %d picture_number %d gap %d tiemStamp %" PRId64 "\n",
									pStatus.ReadyListCount, pStatus.FreeListCount, pStatus.PIBMissCount, 
									procOut.PicInfo.picture_number, 
									procOut.PicInfo.picture_number - this->last_image,
                  procOut.PicInfo.timeStamp);
							  //xprintf(this->xine, XINE_VERBOSITY_NONE,"Lost frame\n");
              }

							if(procOut.PicInfo.picture_number != this->last_image) {
								this->last_image = procOut.PicInfo.picture_number;
							}

              /* Hack to drop every second frame */
              if(this->decoder_25p && (procOut.PicInfo.picture_number % 2)) {
                continue;
              }

							image_buffer_t *img = malloc(sizeof(image_buffer_t));

							/* allocate new image buffer and push it to the image list */
              if(this->use_threading) {
							  img->image = transferbuff;
							  img->image_bytes = procOut.YbuffSz;
              } else {
							  img->image = procOut.Ybuff;
							  img->image_bytes = procOut.YBuffDoneSz;
              }

							img->width = this->width;
							img->height = this->height;
							img->pts = procOut.PicInfo.timeStamp;
						 	img->video_step = this->video_step;
						 	img->ratio = this->ratio;
							img->interlaced = this->interlaced;
							img->picture_number = procOut.PicInfo.picture_number;

              if(this->use_threading) {
  							transferbuff = NULL;

	  	          //pthread_mutex_lock(&this->rec_mutex);
		  					xine_list_push_back(this->image_buffer, img);
		            //pthread_mutex_unlock(&this->rec_mutex);
              } else {
                crystalhd_video_render(this, img);
              }

						}
					}
          if(!this->use_threading) {
            DtsReleaseOutputBuffs(hDevice, NULL, FALSE);
          }
					break;
        case BC_STS_DEC_NOT_OPEN:
        case BC_STS_DEC_NOT_STARTED:
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

    if(this->use_threading) {
      msleep(5);
    } else {
      break;
   }
	}

  if(transferbuff) {
	  free(transferbuff);
	  transferbuff = NULL;
  }

  if(this->use_threading) {
	  pthread_exit(NULL);
  }

  return NULL;
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void crystalhd_video_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

  this->deocder_type = buf->type;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) 
    return;

  if ( !buf->size )
    return;
  
  if (buf->decoder_flags & BUF_FLAG_ASPECT) {
    this->ratio = (double)buf->decoder_info[1]/(double)buf->decoder_info[2];
    //lprintf("arx=%d ary=%d ratio=%f\n", buf->decoder_info[1], buf->decoder_info[2], this->ratio);
  }

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
  }

  if (this->video_step != this->reported_video_step){
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step));
  }

  if(!this->use_threading) {
    crystalhd_video_rec_thread(this);
  }

  if(this->use_threading) {
    crystalhd_video_render(this, NULL);
  }

  switch(this->deocder_type) {
    case BUF_VIDEO_VC1:
    case BUF_VIDEO_WMV9:
      crystalhd_vc1_decode_data (this_gen, buf);
      break;
    case BUF_VIDEO_H264:
      crystalhd_h264_decode_data (this_gen, buf);
      break;
    case BUF_VIDEO_MPEG:
      crystalhd_mpeg_decode_data (this_gen, buf);
      lprintf("BUF_VIDEO_MPEG\n");
      break;
  }

}

static void crystalhd_video_clear_all_pts(crystalhd_video_decoder_t *this) {

	xine_list_iterator_t ite;
	while ((ite = xine_list_front(this->image_buffer)) != NULL) {
		image_buffer_t	*img = xine_list_get_value(this->image_buffer, ite);
    img->pts = 0;
	}

	if(hDevice) {
		DtsFlushInput(hDevice, 1);
	}

  this->last_pts = 0;

}

static void crystalhd_video_clear_worker_buffers(crystalhd_video_decoder_t *this) {
	xine_list_iterator_t ite;

  //lprintf("crystalhd_video_clear_worker_buffers enter\n");

	if(hDevice) {
		DtsFlushInput(hDevice, 1);
	}

	while ((ite = xine_list_front(this->image_buffer)) != NULL) {
		image_buffer_t	*img = xine_list_get_value(this->image_buffer, ite);
		free(img->image);
		free(img);
		xine_list_remove(this->image_buffer, ite);
	}

  //lprintf("crystalhd_video_clear_worker_buffers leave\n");
}

static void crystalhd_video_destroy_workers(crystalhd_video_decoder_t *this) {

  if(this->use_threading) {
  	if(this->rec_thread) {
	  	this->rec_thread_stop = 1;
	  }
	  pthread_mutex_destroy(&this->rec_mutex);
  }

}

static void crystalhd_video_setup_workers(crystalhd_video_decoder_t *this) {
				
  if(this->use_threading) {
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&this->rec_thread, &thread_attr,crystalhd_video_rec_thread,(void *)this);
    pthread_attr_destroy(&thread_attr);

	  pthread_mutex_init(&this->rec_mutex, NULL);
  }

}

/*
 * This function is called when xine needs to flush the system.
 */
static void crystalhd_video_flush (video_decoder_t *this_gen) {
  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t*) this_gen;

	crystalhd_video_clear_worker_buffers(this);

  this->reset = VO_NEW_SEQUENCE_FLAG;

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: crystalhd_video_decode_flush\n");
}

/*
 * This function resets the video decoder.
 */
static void crystalhd_video_reset (video_decoder_t *this_gen) {
  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

  this->last_image        = 0;
  this->last_pts          = 0;

  switch(this->deocder_type) {
    case BUF_VIDEO_VC1:
    case BUF_VIDEO_WMV9:
      crystalhd_vc1_reset_sequence( &this->sequence_vc1 );
      break;
    case BUF_VIDEO_H264:
      free_parser(this->nal_parser);
      this->nal_parser = init_parser(this->xine);
	    if(this->extradata_size > 0) {
		    parse_codec_private(this->nal_parser, this->extradata, this->extradata_size);
        this->wait_for_frame_start = this->have_frame_boundary_marks;
	    }
      break;
    case BUF_VIDEO_MPEG:
      crystalhd_mpeg_reset_sequence( &this->sequence_mpeg, 1 );
      break;
  }

	crystalhd_video_clear_worker_buffers(this);

  this->set_form          = 0;

  this->reset = VO_NEW_SEQUENCE_FLAG;

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: crystalhd_video_reset\n");
}

/*
 * The decoder should forget any stored pts values here.
 */
static void crystalhd_video_discontinuity (video_decoder_t *this_gen) {
  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

  switch(this->deocder_type) {
    case BUF_VIDEO_H264:
      crystalhd_video_clear_all_pts(this);
      break;
    case BUF_VIDEO_MPEG:
      crystalhd_mpeg_reset_sequence( &this->sequence_mpeg, 0 );
      break;
  }

  this->reset = VO_NEW_SEQUENCE_FLAG;

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: crystalhd_video_discontinuity\n");
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void crystalhd_video_dispose (video_decoder_t *this_gen) {

  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

	crystalhd_video_destroy_workers(this);

	hDevice = crystalhd_stop(this, hDevice);

	crystalhd_video_clear_worker_buffers(this);

  free(this->sequence_vc1.bytestream);
  this->sequence_vc1.bytestream_bytes = 0;
  this->sequence_vc1.bytestream = NULL;
  //crystalhd_vc1_reset_sequence( &this->sequence_vc1 );
  
  crystalhd_mpeg_free_sequence( &this->sequence_mpeg );
  free( this->sequence_mpeg.picture.slices );
  free( this->sequence_mpeg.buf );

  free_parser (this->nal_parser);

	if( this->extradata ) {
		free( this->extradata );
		this->extradata = NULL;
	}

  this->set_form      = 0;

  free (this);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: crystalhd_video_dispose\n");
}

void crystalhd_scaling_enable( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->scaling_enable = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: scaling_enable %d\n", this->scaling_enable);
}

void crystalhd_scaling_width( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->scaling_width = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: scaling_width %d\n", this->scaling_width);
}

void crystalhd_use_threading( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->use_threading = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: use_threading %d\n", this->use_threading);
}

void crystalhd_extra_logging( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->extra_logging = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: extra_logging %d\n", this->extra_logging);
}

void crystalhd_decoder_reopen( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->decoder_reopen = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: decoder_reopen %d\n", this->decoder_reopen);
}

void crystalhd_decoder_25p_drop( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->decoder_25p_drop = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: decoder_25p_drop %d\n", this->decoder_25p_drop);
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *crystalhd_video_open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  crystalhd_video_decoder_t  *this ;
  config_values_t *config;
  
  this = (crystalhd_video_decoder_t *) calloc(1, sizeof(crystalhd_video_decoder_t));

  this->video_decoder.decode_data         = crystalhd_video_decode_data;
  this->video_decoder.flush               = crystalhd_video_flush;
  this->video_decoder.reset               = crystalhd_video_reset;
  this->video_decoder.discontinuity       = crystalhd_video_discontinuity;
  this->video_decoder.dispose             = crystalhd_video_dispose;

  this->stream                            = stream;
  this->xine                              = stream->xine;
  this->class                             = (crystalhd_video_class_t *) class_gen;

  config                                  = this->xine->config;

  this->scaling_enable = config->register_bool( config, "video.crystalhd_decoder.scaling_enable", 0,
    _("crystalhd_video: enable decoder scaling"),
    _("Set to true if you want to enable scaling.\n"),
    10, crystalhd_scaling_enable, this );

  this->scaling_width = config->register_num( config, "video.crystalhd_decoder.scaling_width", 0,
    _("crystalhd_video: scaling width"),
    _("Set it to the scaled width.\n"),
    1920, crystalhd_scaling_width, this );

  this->use_threading = config->register_bool( config, "video.crystalhd_decoder.use_threading", 1,
    _("crystalhd_video: use threading"),
    _("Set this to false if you wanna have no recieve thread.\n"),
    10, crystalhd_use_threading, this );

  this->extra_logging = config->register_bool( config, "video.crystalhd_decoder.extra_logging", 0,
    _("crystalhd_video: enable extra logging"),
    _("Set this to true if you wanna have extra logging.\n"),
    10, crystalhd_extra_logging, this );

  this->decoder_reopen = config->register_bool( config, "video.crystalhd_decoder.decoder_reopen", 0,
    _("crystalhd_video: use full decoder reopen."),
    _("due a bug in bcm70015 set this to true for bcm70015.\n"),
    10, crystalhd_decoder_reopen, this );

  this->decoder_25p_drop = config->register_bool( config, "video.crystalhd_decoder.decoder_25p_drop", 0,
    _("crystalhd_video: on >=50p drop every second frame"),
    _("on >=50p drop every second frame. This is a hack for slow gfx cards.\n"),
    10, crystalhd_decoder_25p_drop, this );

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: scaling_enable %d\n", this->scaling_enable);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: scaling_width  %d\n", this->scaling_width);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: use_threading  %d\n", this->use_threading);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: extra_logging  %d\n", this->extra_logging);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: decoder_reopen %d\n", this->decoder_reopen);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: decoder_25p_drop %d\n", this->decoder_25p_drop);

  this->video_step  	    = 0;
  this->reported_video_step = 0;
  this->ratio  				    = 0;
  this->last_pts          = 0;

	this->width							= 1920;
	this->height						= 1082;
	this->y_size						= this->width * this->height * 2;
	this->uv_size						= 0;

	this->interlaced        = 0;
	this->last_image				= 0;

	this->rec_thread_stop 	= 0;

  this->sequence_mpeg.bufsize = 1024;
  this->sequence_mpeg.buf = (uint8_t*)malloc(this->sequence_mpeg.bufsize);
  crystalhd_mpeg_free_sequence( &this->sequence_mpeg );

  this->sequence_vc1.bufsize = 10000;
  this->sequence_vc1.buf = (uint8_t*)malloc(this->sequence_vc1.bufsize);
	crystalhd_vc1_init_sequence( &this->sequence_vc1 );

  this->nal_parser        = init_parser(this->xine);
  this->extradata         = NULL;
  this->wait_for_frame_start = 0;

	this->image_buffer      = xine_list_new();

  this->set_form          = 0;

  this->reset             = VO_NEW_SEQUENCE_FLAG;

  this->decoder_25p       = 0;

	crystalhd_video_setup_workers(this);

  return &this->video_decoder;
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
void *init_video_plugin (xine_t *xine, void *data) {

  crystalhd_video_class_t *this;
  int use_threading;

  this = (crystalhd_video_class_t *) calloc(1, sizeof(crystalhd_video_class_t));

  this->decoder_class.open_plugin     = crystalhd_video_open_plugin;
  this->decoder_class.identifier      = "crystalhd_video";
  this->decoder_class.description     =
	N_("crystalhd_video: Video decoder plugin using CrystalHD hardware decoding.");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  use_threading = xine->config->register_bool( xine->config, "video.crystalhd_decoder.use_threading", 1,
    _("crystalhd_video: use threading"),
    _("Set this to false if you wanna have no recieve thread.\n"),
    10, crystalhd_use_threading, this );

  hDevice = crystalhd_open(use_threading);

  return this;
}

/*
 * This is a list of all of the internal xine video buffer types that
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0.
 */
uint32_t video_types[] = {
  BUF_VIDEO_H264, BUF_VIDEO_VC1, BUF_VIDEO_WMV9, /*BUF_VIDEO_MPEG,*/
  0
};

/*
 * This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n).
 */
decoder_info_t dec_info_crystalhd_video = {
  video_types,     /* supported types */
  7                    /* priority        */
};

/*
 * The plugin catalog entry. This is the only information that this plugin
 * will export to the public.
 */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API, "name", version, special_info, init_function } */
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "crystalhd_decoder", XINE_VERSION_CODE, &dec_info_crystalhd_video, init_video_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

