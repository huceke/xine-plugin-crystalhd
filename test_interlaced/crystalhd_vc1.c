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
 * crystalhd_vc1.c: H264 Video Decoder utilizing Broadcom Crystal HD engine
 */

#define LOG

#include "crystalhd.h"
#include "crystalhd_hw.h"

#include "bits_reader.h"

#define sequence_header_code    0x0f
#define sequence_end_code       0x0a
#define entry_point_code        0x0e
#define frame_start_code        0x0d
#define field_start_code        0x0c
#define slice_start_code        0x0b

#define PICTURE_FRAME            0
#define PICTURE_FRAME_INTERLACE  2
#define PICTURE_FIELD_INTERLACE  3

#define I_FRAME   0
#define P_FRAME   1
#define B_FRAME   3
#define BI_FRAME  4

#define FIELDS_I_I    0
#define FIELDS_I_P    1
#define FIELDS_P_I    2
#define FIELDS_P_P    3
#define FIELDS_B_B    4
#define FIELDS_B_BI   5
#define FIELDS_BI_B   6
#define FIELDS_BI_BI  7

#define MODE_STARTCODE  0
#define MODE_FRAME      1

#define PROFILE_VC1_SIMPLE      1
#define PROFILE_VC1_ADVANCED    2
#define PROFILE_VC1_MAIN        3

const double aspect_ratio[] = {
  0.0,
  1.0,
  12./11.,
  10./11.,
  16./11.,
  40./33.,
  24./11.,
  20./11.,
  32./11.,
  80./33.,
  18./11.,
  15./11.,
  64./33.,
  160./99.
};

typedef struct {
  video_decoder_class_t   decoder_class;
} crystalhd_vc1_class_t;

typedef struct {
  int         slices;
  int         fptype;
  int         field;
  int         header_size;
  int         skipped;
  uint8_t     picture_type;
  uint8_t     finterpflag;
  uint8_t     maxbframes;
  uint8_t     interlace;
  uint8_t     rangered;
  uint8_t     frame_coding_mode;
} picture_t;

typedef struct {
  uint32_t    coded_width;
  uint32_t    coded_height;

  uint64_t    video_step; /* frame duration in pts units */
  double      ratio;
  uint32_t    profile;

  int         mode;
  int         have_header;

  uint8_t     *buf; /* accumulate data */
  int         bufseek;
  int         start;
  int         code_start, current_code;
  uint32_t    bufsize;
  uint32_t    bufpos;

  picture_t   picture;
  int64_t     seq_pts;
  int64_t     cur_pts;

  bits_reader_t br;
} sequence_t;

typedef struct crystalhd_vc1_decoder_s {
	video_decoder_t   video_decoder;  /* parent video decoder structure */

	crystalhd_vc1_class_t *class;
	xine_stream_t    *stream;

	/* these are traditional variables in a video decoder object */

	double            ratio;

	xine_t            *xine;

	unsigned char    	*transferbuff;

	int64_t           next_pts;
	uint64_t          video_step;
  int               got_frame;

	int								width;
	int								height;
	int								y_size;
	int								uv_size;

  int               nal_size_length;
	int								decoder_timeout;


	BC_DTS_PROC_OUT		procOut;
	uint32_t								videoAlg;

	sequence_t        sequence;

	int								interlaced;

	int								last_image;

	int 							start_decoding;

	xine_list_t       *image_buffer;
	xine_list_t       *decoder_buffer;

	pthread_t         render_thread;
	int								render_thread_stop;
	pthread_mutex_t		render_mutex;
	pthread_t         rec_thread;
	int								rec_thread_stop;
	pthread_mutex_t		rec_mutex;
	pthread_t         send_thread;
	int								send_thread_stop;
	pthread_mutex_t		send_mutex;

	pthread_mutex_t		image_mutex;
} crystalhd_vc1_decoder_t;

static void crystalhd_vc1_flush (video_decoder_t *this_gen);

/**************************************************************************
 * crystalhd_vc1 specific decode functions
 *************************************************************************/

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

static void reset_picture( picture_t *pic )
{
    pic->slices = 1;
}

static void init_picture( picture_t *pic )
{
    memset( pic, 0, sizeof( picture_t ) );
}

static void reset_sequence( sequence_t *sequence ) {
  lprintf( "reset_sequence\n" );
  sequence->bufpos = 0;
  sequence->bufseek = 0;
  sequence->start = -1;
  sequence->code_start = sequence->current_code = 0;
  sequence->seq_pts = sequence->cur_pts = 0;
  reset_picture( &sequence->picture );
}

static void init_sequence( sequence_t *sequence ) {
  lprintf( "init_sequence\n" );
  sequence->have_header = 0;
  sequence->profile = PROFILE_VC1_SIMPLE;
  sequence->ratio = 0;
  sequence->video_step = 0;
  reset_sequence( sequence );
}

static void set_video_step(crystalhd_vc1_decoder_t *this, uint32_t frame_rate) {
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
      this->interlaced = TRUE;
			this->video_step = 90000/50;
			break;
		case vdecRESOLUTION_NTSC:
      this->interlaced = TRUE;
			this->video_step = 90000/60;
			break;
		case vdecRESOLUTION_720p50:
			this->video_step = 90000/50;
			break;
		case vdecRESOLUTION_1080i25:
      this->interlaced = TRUE;
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
			this->video_step = 90000/23.97;
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
		case vdecRESOLUTION_720p59_94:
      this->interlaced = TRUE;
			this->video_step = 90000/59.94;
			break;
		case vdecRESOLUTION_CUSTOM:
		case vdecRESOLUTION_480p656:
		default:
			this->video_step = 90000/25;
			break;
	}
}

static void set_ratio(crystalhd_vc1_decoder_t *this, uint32_t aspect_ratio) {
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

static void print_setup(crystalhd_vc1_decoder_t *this) {
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->width %d\n", this->width);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->height %d\n", this->height);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->interlaced %d\n", this->interlaced);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->video_step %ld\n", this->video_step);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->ratio %f\n", this->ratio);
}

static void set_video_params (crystalhd_vc1_decoder_t *this) {
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*this->ratio) );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step );
	_x_meta_info_set_utf8( this->stream, XINE_META_INFO_VIDEOCODEC, "VC1/WMV9 (crystalhd)" );
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

static void set_video_vo_flags(crystalhd_vc1_decoder_t *this, uint32_t *flags) {
  *flags = VO_BOTH_FIELDS;
	*flags |= VO_PAN_SCAN_FLAG;
}

static void crystalhd_vc1_render (crystalhd_vc1_decoder_t *this, image_buffer_t *img) {
  vo_frame_t	*vo_img;

 	vo_img = this->stream->video_out->get_frame (this->stream->video_out,
                            img->width, (this->interlaced) ? img->height / 2 : img->height, this->ratio, 
														XINE_IMGFMT_YUY2, img->flags);

	yuy2_to_yuy2(
			img->image, img->width * 2,
			vo_img->base[0], vo_img->pitches[0],
			img->width, (this->interlaced) ? img->height / 2 : img->height);

	vo_img->pts			 = img->pts;
 	vo_img->duration = img->video_step;
	vo_img->bad_frame = 0;

 	vo_img->draw(vo_img, this->stream);

  /*
	lprintf("vo_img->pts %lld img->pts %lld vo_img->duration %d img->video_step %lld picture_number %d\n",
		vo_img->pts, img->pts, vo_img->duration, img->video_step, img->picture_number);
  */

 	vo_img->free(vo_img);
}

static void* crystalhd_vc1_render_thread (void *this_gen) {

	crystalhd_vc1_decoder_t *this = (crystalhd_vc1_decoder_t *) this_gen;

	while(!this->render_thread_stop) {

		pthread_mutex_lock(&this->render_mutex);

    pthread_mutex_lock(&this->image_mutex);

		xine_list_iterator_t ite;	

		ite = xine_list_front(this->image_buffer);

    //lprintf("xine_list_size(this->image_buffer) %d\n", xine_list_size(this->image_buffer));

    if(ite!= NULL) {
			image_buffer_t *img	= xine_list_get_value(this->image_buffer, ite);
			xine_list_remove(this->image_buffer, ite);
      pthread_mutex_unlock(&this->image_mutex);

  		if(img->image_bytes > 0) {
				crystalhd_vc1_render(this, img);
			}

			free(img->image);
			free(img);
    } else {
      pthread_mutex_unlock(&this->image_mutex);
    }

		pthread_mutex_unlock(&this->render_mutex);

	}

	pthread_exit(NULL);
}

static void* crystalhd_vc1_send_thread (void *this_gen) {

	BC_STATUS ret;
	crystalhd_vc1_decoder_t *this = (crystalhd_vc1_decoder_t *) this_gen;

	while(!this->send_thread_stop) {

		pthread_mutex_lock(&this->send_mutex);

		xine_list_iterator_t ite;
		ite = xine_list_front(this->decoder_buffer);

		if(ite != NULL) {

			decoder_buffer_t *decode_buffer = xine_list_get_value(this->decoder_buffer, ite);

		 	ret = DtsProcInput(hDevice, decode_buffer->bytestream, decode_buffer->bytestream_bytes, decode_buffer->pts, 0);

	 		if (ret == BC_STS_SUCCESS) {
				free(decode_buffer->bytestream);
				xine_list_remove(this->decoder_buffer, ite);
			} else if (ret == BC_STS_BUSY) {
 				lprintf("decoder BC_STS_BUSY\n");
				DtsFlushInput(hDevice, 4);
			}
		}

		pthread_mutex_unlock(&this->send_mutex);

	}
	pthread_exit(NULL);
}

static void crystalhd_vc1_handle_buffer (crystalhd_vc1_decoder_t *this, buf_element_t *buf, 
		uint8_t *bytestream, uint32_t bytestream_bytes) {

  //lprintf("crystalhd_vc1_handle_buffer size %d\n", bytestream_bytes);

	if(bytestream_bytes == 0) return;

  if(hDevice == 0) return;

	if(!this->start_decoding) return;

	decoder_buffer_t *decode_buffer = calloc(1, sizeof(decoder_buffer_t));

	decode_buffer->bytestream_bytes = bytestream_bytes;

	decode_buffer->bytestream = calloc(1, decode_buffer->bytestream_bytes);

  if(buf->pts != 0 && buf->pts != this->next_pts) {
 		this->next_pts = buf->pts;
  }
  decode_buffer->pts = this->next_pts;
  this->next_pts = 0;

	xine_fast_memcpy(decode_buffer->bytestream, bytestream, bytestream_bytes);

	//crystalhd_decode_package(decode_buffer->bytestream, decode_buffer->bytestream_bytes);

	pthread_mutex_lock(&this->send_mutex);
	xine_list_push_back(this->decoder_buffer, decode_buffer);
	pthread_mutex_unlock(&this->send_mutex);
}

static void* crystalhd_vc1_rec_thread (void *this_gen) {
	crystalhd_vc1_decoder_t *this = (crystalhd_vc1_decoder_t *) this_gen;
	BC_STATUS ret;
	BC_DTS_STATUS pStatus;

	while(!this->rec_thread_stop) {
	
		pthread_mutex_lock(&this->rec_mutex);

		/* read driver status. we need the frame ready count from it */
		if(!this->got_frame) {
			ret = DtsGetDriverStatus(hDevice, &pStatus);
			if( ret == BC_STS_SUCCESS && pStatus.ReadyListCount > 0) {
				this->got_frame = 1;
			}
		}
		
		if( this->got_frame ) {

			//DtsGetDriverStatus(hDevice, &pStatus);

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

			if(this->interlaced) {
				this->procOut.PoutFlags |= BC_POUT_FLAGS_INTERLACED;
			}

			this->procOut.PoutFlags = this->procOut.PoutFlags & 0xff;
	
			ret = DtsProcOutput(hDevice, this->decoder_timeout, &this->procOut);
	
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

						if(this->interlaced) {
							this->y_size = this->width * this->height;
						} else {
							this->y_size = this->width * this->height * 2;
						}

						this->uv_size = 0;
	
						this->decoder_timeout = 2000;
				
						set_ratio(this, this->procOut.PicInfo.aspect_ratio);
						set_video_step(this, this->procOut.PicInfo.frame_rate);
						set_video_params(this);
	   	   	}
					break;
				case BC_STS_SUCCESS:
	     	 	if (this->procOut.PoutFlags & BC_POUT_FLAGS_PIB_VALID ||
							this->procOut.PoutFlags & BC_POUT_FLAGS_FLD_BOT) {
	
						if(this->last_image == 0) {
							this->last_image = this->procOut.PicInfo.picture_number;
						}

            if((this->procOut.PicInfo.picture_number - this->last_image) > 0 ) {
						//if(this->procOut.PicInfo.timeStamp && (this->procOut.PicInfo.timeStamp != this->last_out_pts)) {

              /*
							lprintf("ReadyListCount %d FreeListCount %d PIBMissCount %d picture_number %d gap %d\n",
									pStatus.ReadyListCount, pStatus.FreeListCount, pStatus.PIBMissCount, 
									this->procOut.PicInfo.picture_number, 
									this->procOut.PicInfo.picture_number - this->last_image);
              */

							if(this->procOut.PicInfo.picture_number != this->last_image) {
								this->last_image = this->procOut.PicInfo.picture_number;
							}

							/* allocate new image buffer and push it to the image list */
							image_buffer_t *img = malloc(sizeof(image_buffer_t));
							
							img->image = this->transferbuff;
							img->image_bytes = this->procOut.YbuffSz;
							img->width = this->width;
							img->height = this->height;
							img->pts = this->procOut.PicInfo.timeStamp;
						 	img->video_step = this->video_step;
							img->interlaced = this->interlaced;
							img->picture_number = this->procOut.PicInfo.picture_number;

							set_video_vo_flags(this, &img->flags);

							this->transferbuff = NULL;

		          pthread_mutex_lock(&this->image_mutex);
							xine_list_push_back(this->image_buffer, img);
		          pthread_mutex_unlock(&this->image_mutex);
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

		pthread_mutex_unlock(&this->rec_mutex);

    //Sleep(2);

	}
	pthread_exit(NULL);
}

static void sequence_header_advanced( crystalhd_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  //lprintf( "sequence_header_advanced\n" );
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  if ( len < 5 )
    return;

  sequence->profile = PROFILE_VC1_ADVANCED;
  //lprintf("PROFILE_VC1_ADVANCED\n");
  bits_reader_set( &sequence->br, buf );
  read_bits( &sequence->br, 16 );
  sequence->coded_width = read_bits( &sequence->br, 12 )<<1;
  sequence->coded_height = (read_bits( &sequence->br, 12 )+1)<<1;
  read_bits( &sequence->br, 1 );
  sequence->picture.interlace = read_bits( &sequence->br, 1 );
  read_bits( &sequence->br, 1 );
  sequence->picture.finterpflag = read_bits( &sequence->br, 1 );
  read_bits( &sequence->br, 2 );
  sequence->picture.maxbframes = 7;

  sequence->have_header = 1;
}

static void sequence_header( crystalhd_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  //lprintf( "sequence_header\n" );
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  if ( len < 4 )
    return;

  bits_reader_set( &sequence->br, buf );
  switch ( read_bits( &sequence->br, 2 ) ) {
    case 0: sequence->profile = PROFILE_VC1_SIMPLE; lprintf("PROFILE_VC1_SIMPLE\n"); break;
    case 1: sequence->profile = PROFILE_VC1_MAIN; lprintf("PROFILE_VC1_MAIN\n"); break;
    case 2: sequence->profile = PROFILE_VC1_MAIN; lprintf("vc1_complex profile not supported by vdpau, trying vc1_main.\n"); break;
    case 3: return sequence_header_advanced( this_gen, buf, len ); break;
    default: return; /* illegal value, broken header? */
  }
  read_bits( &sequence->br, 22 );
  sequence->picture.rangered = read_bits( &sequence->br, 1 );
  sequence->picture.maxbframes = read_bits( &sequence->br, 3 );
  read_bits( &sequence->br, 2 );
  sequence->picture.finterpflag = read_bits( &sequence->br, 1 );

  sequence->have_header = 1;
}

static void picture_header( crystalhd_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  picture_t *pic = (picture_t*)&sequence->picture;
  int tmp;

  lprintf("picture_header\n");

  bits_reader_set( &sequence->br, buf );
  read_bits( &sequence->br, 2 );

  if ( pic->finterpflag )
    read_bits( &sequence->br, 1 );
  if ( pic->rangered ) {
    pic->rangered = (read_bits( &sequence->br, 1 ) << 1) +1;
  }
  if ( !pic->maxbframes ) {
    if ( read_bits( &sequence->br, 1 ) )
      pic->picture_type = P_FRAME;
    else
      pic->picture_type = I_FRAME;
  }
  else {
    if ( read_bits( &sequence->br, 1 ) )
      pic->picture_type = P_FRAME;
    else {
      if ( read_bits( &sequence->br, 1 ) )
        pic->picture_type = I_FRAME;
      else
        pic->picture_type = B_FRAME;
    }
  }
  if ( pic->picture_type == B_FRAME ) {
    tmp = read_bits( &sequence->br, 3 );
    if ( tmp==7 ) {
      tmp = (tmp<<4) | read_bits( &sequence->br, 4 );
      if ( tmp==127 )
        pic->picture_type = BI_FRAME;
    }
  }
}

static void picture_header_advanced( crystalhd_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  picture_t *pic = (picture_t*)&sequence->picture;

  //lprintf("picture_header_advanced\n");

  bits_reader_set( &sequence->br, buf );

  if ( pic->interlace ) {
    //lprintf("frame->interlace=1\n");
    if ( !read_bits( &sequence->br, 1 ) ) {
      lprintf("progressive frame\n");
      pic->frame_coding_mode = PICTURE_FRAME;
    }
    else {
      if ( !read_bits( &sequence->br, 1 ) ) {
        //lprintf("frame interlaced\n");
        pic->frame_coding_mode = PICTURE_FRAME_INTERLACE;
      }
      else {
        //lprintf("field interlaced\n");
        pic->frame_coding_mode = PICTURE_FIELD_INTERLACE;
      }
    }
  }
  if ( pic->interlace && pic->frame_coding_mode == PICTURE_FIELD_INTERLACE ) {
    pic->fptype = read_bits( &sequence->br, 3 );
    switch ( pic->fptype ) {
      case FIELDS_I_I:
      case FIELDS_I_P:
        pic->picture_type = I_FRAME; break;
      case FIELDS_P_I:
      case FIELDS_P_P:
        pic->picture_type = P_FRAME; break;
      case FIELDS_B_B:
      case FIELDS_B_BI:
        pic->picture_type = B_FRAME; break;
      default:
        pic->picture_type = BI_FRAME;
    }
  } else {
    if ( !read_bits( &sequence->br, 1 ) )
      pic->picture_type = P_FRAME;
    else {
      if ( !read_bits( &sequence->br, 1 ) )
        pic->picture_type = B_FRAME;
      else {
        if ( !read_bits( &sequence->br, 1 ) )
          pic->picture_type = I_FRAME;
        else {
          if ( !read_bits( &sequence->br, 1 ) )
            pic->picture_type = BI_FRAME;
          else {
            pic->picture_type = P_FRAME;
            pic->skipped = 1;
          }
        }
      }
    }
  }
}

static void parse_header( crystalhd_vc1_decoder_t *this_gen, uint8_t *buf, int len ) {
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  int off=0;

  while ( off < (len-4) ) {
    uint8_t *buffer = buf+off;
    if ( buffer[0]==0 && buffer[1]==0 && buffer[2]==1 ) {
      switch ( buffer[3] ) {
        case sequence_header_code: 
          sequence_header( this_gen, buf+off+4, len-off-4 ); 
          break;
      }
    }
    ++off;
  }
  if ( !sequence->have_header ) {
    sequence_header( this_gen, buf, len );
  }
}

static void remove_emulation_prevention( uint8_t *src, uint8_t *dst, int src_len, int *dst_len )
{
  int i;
  int len = 0;
  int removed = 0;

  for ( i=0; i<src_len-3; ++i ) {
    if ( src[i]==0 && src[i+1]==0 && src[i+2]==3 ) {
      //lprintf("removed emulation prevention byte\n");
      dst[len++] = src[i];
      dst[len++] = src[i+1];
      i += 2;
      ++removed;
    }
    else {
      memcpy( dst+len, src+i, 4 );
      ++len;
    }
  }
  for ( ; i<src_len; ++i )
    dst[len++] = src[i];
  *dst_len = src_len-removed;
}

static int parse_code( crystalhd_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  if ( !sequence->have_header && buf[3]!=sequence_header_code )
    return 0;

  if ( sequence->code_start == frame_start_code ) {
    if ( sequence->current_code==field_start_code || sequence->current_code==slice_start_code ) {
      return -1;
	}
    return 1; /* frame complete, decode */
  }

  switch ( buf[3] ) {
    int dst_len;
    uint8_t *tmp;
    case sequence_header_code:
      //lprintf("sequence_header_code\n");
      tmp = malloc( len );
      remove_emulation_prevention( buf, tmp, len, &dst_len );
      sequence_header( this_gen, tmp+4, dst_len-4 );
      free( tmp );
      break;
    case entry_point_code:
      //lprintf("entry_point_code\n");
      tmp = malloc( len );
      remove_emulation_prevention( buf, tmp, len, &dst_len );
      free( tmp );
      break;
    case sequence_end_code:
      //lprintf("sequence_end_code\n");
      break;
    case frame_start_code:
      //lprintf("frame_start_code, len=%d\n", len);
      break;
    case field_start_code:
      //lprintf("field_start_code\n");
      break;
    case slice_start_code:
      //lprintf("slice_start_code, len=%d\n", len);
      break;
  }
  return 0;
}

/*
static int search_field( crystalhd_vc1_decoder_t *vd, uint8_t *buf, int len )
{
  int i;
  lprintf("search_fields, len=%d\n", len);
  for ( i=0; i<len-4; ++i ) {
    if ( buf[i]==0 && buf[i+1]==0 && buf[i+2]==1 && buf[i+3]==field_start_code ) {
      lprintf("found field_start_code at %d\n", i);
      return i;
    }
  }
  return 0;
}
*/

static void decode_picture( crystalhd_vc1_decoder_t *vd, buf_element_t *bufelem)
{
  sequence_t *seq = (sequence_t*)&vd->sequence;
  picture_t *pic = (picture_t*)&seq->picture;

  uint8_t *buf;
  int len;

  pic->skipped = 0;
  pic->field = 0;

  if ( seq->mode == MODE_FRAME ) {
    buf = seq->buf;
    len = seq->bufpos;
    if ( seq->profile==PROFILE_VC1_ADVANCED )
      picture_header_advanced( vd, buf, len );
    else
      picture_header( vd, buf, len );

    if ( len < 2 )
      pic->skipped = 1;
  } else {
    buf = seq->buf+seq->start+4;
    len = seq->bufseek-seq->start-4;
    if ( seq->profile==PROFILE_VC1_ADVANCED ) {
      int tmplen = (len>50) ? 50 : len;
      uint8_t *tmp = malloc( tmplen );
      remove_emulation_prevention( buf, tmp, tmplen, &tmplen );
      picture_header_advanced( vd, tmp, tmplen );
      free( tmp );
    }
    else
      picture_header( vd, buf, len );

    if ( len < 2 )
      pic->skipped = 1;
  }

  /*
  if ( pic->skipped )
    pic->picture_type = P_FRAME;
  */

  if(pic->skipped) {
	  reset_picture( &seq->picture );
    return;
  }

  seq->seq_pts +=seq->video_step;
  reset_picture( &seq->picture );
}


/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void crystalhd_vc1_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  crystalhd_vc1_decoder_t *this = (crystalhd_vc1_decoder_t *) this_gen;
  sequence_t *seq = (sequence_t*)&this->sequence;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
		return;
	}

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    //lprintf("BUF_FLAG_FRAMERATE=%d\n", buf->decoder_info[0]);
    if ( buf->decoder_info[0] > 0 ) {
      this->video_step = buf->decoder_info[0];
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
    }
  }

  if (buf->decoder_flags & BUF_FLAG_ASPECT) {
    this->ratio = (double)buf->decoder_info[1]/(double)buf->decoder_info[2];
    //lprintf("arx=%d ary=%d ratio=%f\n", buf->decoder_info[1], buf->decoder_info[2], this->ratio);
  }

  if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
    //lprintf("BUF_FLAG_FRAME_START\n");
    seq->seq_pts = buf->pts;
  }

  if ( !buf->size )
    return;

  seq->cur_pts = buf->pts;

	if(buf->decoder_flags & BUF_FLAG_STDHEADER) {
    //lprintf("BUF_FLAG_STDHEADER\n");
    xine_bmiheader *bih = (xine_bmiheader *) buf->content;
    int bs = sizeof( xine_bmiheader );
		this->width		= bih->biWidth;
		this->height  = bih->biHeight;
    //lprintf( "width=%d height=%d\n", bih->biWidth, bih->biHeight );
    this->start_decoding = 1;
    if ( buf->size > bs ) {
      seq->mode = MODE_FRAME;
      parse_header( this, buf->content+bs, buf->size-bs );
    }
    return;
  }

  int size = seq->bufpos+buf->size;
  if ( seq->bufsize < size ) {
    seq->bufsize = size+10000;
    seq->buf = realloc( seq->buf, seq->bufsize );
    //lprintf("sequence buffer realloced = %d\n", seq->bufsize );
  }
  xine_fast_memcpy( seq->buf+seq->bufpos, buf->content, buf->size );
  seq->bufpos += buf->size;

  if ( seq->mode == MODE_FRAME ) {
    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {
      //lprintf("BUF_FLAG_FRAME_END\n");

      //crystalhd_decode_package( seq->buf, 4);
      this->start_decoding = 1;
      crystalhd_vc1_handle_buffer( this, buf, seq->buf, seq->bufpos);

      decode_picture(this, buf);
      seq->bufpos = 0;
    }
  } else {
    int res;
    while ( seq->bufseek <= seq->bufpos-4 ) {
      uint8_t *buffer = seq->buf+seq->bufseek;
      if ( buffer[0]==0 && buffer[1]==0 && buffer[2]==1 ) {

        //crystalhd_decode_package( buffer, 4);
        this->start_decoding = 1;
        crystalhd_vc1_handle_buffer( this, buf, seq->buf+seq->start, seq->bufseek-seq->start);

        seq->current_code = buffer[3];
        //lprintf("current_code = %d\n", seq->current_code);
        if ( seq->start<0 ) {
          seq->start = seq->bufseek;
          seq->code_start = buffer[3];
          //lprintf("code_start = %d\n", seq->code_start);
          if ( seq->cur_pts ) {
            seq->seq_pts = seq->cur_pts;
          }
        } else {
          res = parse_code( this, seq->buf+seq->start, seq->bufseek-seq->start );
          if ( res==1 ) {
            decode_picture(this, buf);
            parse_code( this, seq->buf+seq->start, seq->bufseek-seq->start );
          }
          if ( res!=-1 ) {
            uint8_t *tmp = (uint8_t*)malloc(seq->bufsize);
            xine_fast_memcpy( tmp, seq->buf+seq->bufseek, seq->bufpos-seq->bufseek );
            seq->bufpos -= seq->bufseek;
            seq->start = -1;
            seq->bufseek = -1;
            free( seq->buf );
            seq->buf = tmp;
          }
        }
      }
      ++seq->bufseek;
    }
  }
}

static void crystalhd_vc1_clear_all_pts(crystalhd_vc1_decoder_t *this) {
	pthread_mutex_lock(&this->send_mutex);
	pthread_mutex_lock(&this->rec_mutex);
	pthread_mutex_lock(&this->render_mutex);

	xine_list_iterator_t ite;
	while ((ite = xine_list_front(this->image_buffer)) != NULL) {
		image_buffer_t	*img = xine_list_get_value(this->image_buffer, ite);
    img->pts = 0;
	}

	while ((ite = xine_list_front(this->decoder_buffer)) != NULL) {
		decoder_buffer_t *decode_buffer = xine_list_get_value(this->decoder_buffer, ite);
    decode_buffer->pts = 0;
	}

	if(hDevice) {
		DtsFlushInput(hDevice, 2);
	}

  pthread_mutex_unlock(&this->send_mutex);
  pthread_mutex_unlock(&this->rec_mutex);
  pthread_mutex_unlock(&this->render_mutex);
}

static void crystalhd_vc1_clear_worker_buffers(crystalhd_vc1_decoder_t *this) {
	xine_list_iterator_t ite;

  lprintf("crystalhd_vc1_clear_worker_buffers enter\n");

	pthread_mutex_lock(&this->send_mutex);
	pthread_mutex_lock(&this->rec_mutex);
	pthread_mutex_lock(&this->render_mutex);

  this->got_frame = 0;

	if(hDevice) {
		DtsFlushInput(hDevice, 2);
	}

	while ((ite = xine_list_front(this->decoder_buffer)) != NULL) {
		decoder_buffer_t *decode_buffer = xine_list_get_value(this->decoder_buffer, ite);
		free(decode_buffer->bytestream);
    free(decode_buffer);
		xine_list_remove(this->decoder_buffer, ite);
	}

	while ((ite = xine_list_front(this->image_buffer)) != NULL) {
		image_buffer_t	*img = xine_list_get_value(this->image_buffer, ite);
		free(img->image);
		free(img);
		xine_list_remove(this->image_buffer, ite);
	}
	pthread_mutex_unlock(&this->send_mutex);
	pthread_mutex_unlock(&this->rec_mutex);
	pthread_mutex_unlock(&this->render_mutex);

  lprintf("crystalhd_vc1_clear_worker_buffers leave\n");
}

static void crystalhd_vc1_destroy_workers(crystalhd_vc1_decoder_t *this) {

	pthread_mutex_lock(&this->render_mutex);
	if(this->render_thread) {
		this->render_thread_stop = 1;
	}
	pthread_mutex_unlock(&this->render_mutex);
	pthread_mutex_destroy(&this->render_mutex);

	pthread_mutex_lock(&this->send_mutex);
	if(this->send_thread) {
		this->send_thread_stop = 1;
	}
	pthread_mutex_unlock(&this->send_mutex);
	pthread_mutex_destroy(&this->send_mutex);

	pthread_mutex_lock(&this->rec_mutex);
	if(this->rec_thread) {
		this->rec_thread_stop = 1;
	}
	pthread_mutex_unlock(&this->rec_mutex);
	pthread_mutex_destroy(&this->rec_mutex);

	pthread_mutex_destroy(&this->image_mutex);
}

static void crystalhd_vc1_setup_workers(crystalhd_vc1_decoder_t *this) {
				
	pthread_mutex_init(&this->render_mutex, NULL);
	pthread_mutex_init(&this->send_mutex, NULL);
	pthread_mutex_init(&this->rec_mutex, NULL);
	pthread_mutex_init(&this->image_mutex, NULL);

  pthread_attr_t thread_attr;
  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
  pthread_create(&this->render_thread, &thread_attr,crystalhd_vc1_render_thread,(void *)this);
  pthread_attr_destroy(&thread_attr);

  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
  pthread_create(&this->send_thread, &thread_attr,crystalhd_vc1_send_thread,(void *)this);
  pthread_attr_destroy(&thread_attr);

  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
  pthread_create(&this->rec_thread, &thread_attr,crystalhd_vc1_rec_thread,(void *)this);
  pthread_attr_destroy(&thread_attr);

}

/*
 * This function is called when xine needs to flush the system.
 */
static void crystalhd_vc1_flush (video_decoder_t *this_gen) {
  crystalhd_vc1_decoder_t *this = (crystalhd_vc1_decoder_t*) this_gen;

	crystalhd_vc1_clear_worker_buffers(this);

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_vc1: crystalhd_vc1_decode_flush\n");
}

/*
 * This function resets the video decoder.
 */
static void crystalhd_vc1_reset (video_decoder_t *this_gen) {
  crystalhd_vc1_decoder_t *this = (crystalhd_vc1_decoder_t *) this_gen;

  this->video_step				= 0;
  this->next_pts					= 0;
  this->last_image        = 0;

	crystalhd_vc1_clear_worker_buffers(this);

  reset_sequence( &this->sequence );

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_vc1: crystalhd_vc1_reset\n");
}

/*
 * The decoder should forget any stored pts values here.
 */
static void crystalhd_vc1_discontinuity (video_decoder_t *this_gen) {
  crystalhd_vc1_decoder_t *this = (crystalhd_vc1_decoder_t *) this_gen;

  this->next_pts					= 0;
  this->last_image        = 0;

  crystalhd_vc1_clear_all_pts(this);

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_vc1: crystalhd_vc1_discontinuity\n");
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void crystalhd_vc1_dispose (video_decoder_t *this_gen) {

  crystalhd_vc1_decoder_t *this = (crystalhd_vc1_decoder_t *) this_gen;

	crystalhd_vc1_destroy_workers(this);

	hDevice = crystalhd_stop(this->xine, hDevice);

  reset_sequence( &this->sequence );

	free(this->transferbuff);
	this->transferbuff = NULL;

	crystalhd_vc1_clear_worker_buffers(this);

  free (this_gen);
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  crystalhd_vc1_decoder_t  *this ;

  this = (crystalhd_vc1_decoder_t *) calloc(1, sizeof(crystalhd_vc1_decoder_t));

  this->video_decoder.decode_data         = crystalhd_vc1_decode_data;
  this->video_decoder.flush               = crystalhd_vc1_flush;
  this->video_decoder.reset               = crystalhd_vc1_reset;
  this->video_decoder.discontinuity       = crystalhd_vc1_discontinuity;
  this->video_decoder.dispose             = crystalhd_vc1_dispose;


  this->stream                            = stream;
  this->xine                              = stream->xine;
  this->class                             = (crystalhd_vc1_class_t *) class_gen;

	this->decoder_timeout   = 20;
	this->transferbuff      = NULL;

  this->video_step  	    = 0;
  this->ratio  				    = 0;
  this->next_pts					= 0;
  this->got_frame         = 0;

	this->width							= 1920;
	this->height						= 1082;
	this->y_size						= this->width * this->height * 2;
	this->uv_size						= 0;

	this->start_decoding		= 0;

	memset(&this->procOut, 0, sizeof(BC_DTS_PROC_OUT));

	this->videoAlg				  = BC_VID_ALGO_VC1;
	
	this->interlaced        = 0;
	this->last_image				= 0;
  this->nal_size_length   = 0;

	this->render_thread_stop	= 0;
	this->rec_thread_stop	= 0;
	this->send_thread_stop	= 0;

  this->sequence.bufsize = 10000;
  this->sequence.buf = (uint8_t*)malloc(this->sequence.bufsize);
	init_sequence( &this->sequence );

  init_picture( &this->sequence.picture );

	this->decoder_buffer    = xine_list_new();
	this->image_buffer    = xine_list_new();

  if(hDevice) {
    hDevice = crystalhd_start(this->xine, hDevice, BC_STREAM_TYPE_ES, BC_VID_ALGO_VC1);
  }

	crystalhd_vc1_setup_workers(this);

  return &this->video_decoder;
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
void *init_vc1_plugin (xine_t *xine, void *data) {

  crystalhd_vc1_class_t *this;

  this = (crystalhd_vc1_class_t *) calloc(1, sizeof(crystalhd_vc1_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "crystalhd_vc1";
  this->decoder_class.description     =
	N_("crystalhd_vc1: VC1 decoder plugin using CrystalHD hardware decoding.");
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
uint32_t video_types_vc1[] = {
  BUF_VIDEO_VC1, BUF_VIDEO_WMV9,
  0
};

/*
 * This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n).
 */
decoder_info_t dec_info_crystalhd_vc1 = {
  video_types_vc1,     /* supported types */
  7                    /* priority        */
};

