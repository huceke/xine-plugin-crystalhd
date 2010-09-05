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
 * crystalhd_mpeg.c: Mpeg Video Decoder utilizing Broadcom Crystal HD engine
 */

#define LOG

#include "crystalhd_decoder.h"
#include "crystalhd_hw.h"
#include "crystalhd_mpeg.h"
#include "bits_reader.h"

#define sequence_header_code    0xb3
#define sequence_error_code     0xb4
#define sequence_end_code       0xb7
#define group_start_code        0xb8
#define extension_start_code    0xb5
#define user_data_start_code    0xb2
#define picture_start_code      0x00
#define begin_slice_start_code  0x01
#define end_slice_start_code    0xaf

#define sequence_ext_sc         1
#define quant_matrix_ext_sc     3
#define picture_coding_ext_sc   8
#define sequence_display_ext_sc 2

#define I_FRAME   1
#define P_FRAME   2
#define B_FRAME   3

#define PICTURE_TOP     1
#define PICTURE_BOTTOM  2
#define PICTURE_FRAME   3

#define WANT_HEADER 1
#define WANT_EXT    2
#define WANT_SLICE  3

#define DECODER_PROFILE_MPEG1         1
#define DECODER_PROFILE_MPEG2_SIMPLE  2
#define DECODER_PROFILE_MPEG2_MAIN    3

/* default intra quant matrix, in zig-zag order */
static const uint8_t default_intra_quantizer_matrix[64] = {
  8,
  16, 16,
  19, 16, 19,
  22, 22, 22, 22,
  22, 22, 26, 24, 26,
  27, 27, 27, 26, 26, 26,
  26, 27, 27, 27, 29, 29, 29,
  34, 34, 34, 29, 29, 29, 27, 27,
  29, 29, 32, 32, 34, 34, 37,
  38, 37, 35, 35, 34, 35,
  38, 38, 40, 40, 40,
  48, 48, 46, 46,
  56, 56, 58,
  69, 69,
  83
};

uint8_t mpeg2_scan_norm[64] = {
  /* Zig-Zag scan pattern */
  0, 1, 8,16, 9, 2, 3,10,
  17,24,32,25,18,11, 4, 5,
  12,19,26,33,40,48,41,34,
  27,20,13, 6, 7,14,21,28,
  35,42,49,56,57,50,43,36,
  29,22,15,23,30,37,44,51,
  58,59,52,45,38,31,39,46,
  53,60,61,54,47,55,62,63
};

/**************************************************************************
 * crystalhd_mpeg specific decode functions
 *************************************************************************/

void crystalhd_mpeg_reset_picture( picture_mpeg_t *pic )
{
  lprintf( "crystalhd_mpeg_reset_picture\n" );
  pic->picture_structure = 0;
  pic->slices_count = 0;
  pic->slices_count2 = 0;
  pic->slices_pos = 0;
  pic->slices_pos_top = 0;
  pic->progressive_frame = 0;
  pic->state = WANT_HEADER;
}

void crystalhd_mpeg_init_picture( picture_mpeg_t *pic )
{
  pic->slices_size = 2048;
  pic->slices = (uint8_t*)malloc(pic->slices_size);
  crystalhd_mpeg_reset_picture( pic );
}

void crystalhd_mpeg_reset_sequence( sequence_mpeg_t *sequence, int free_refs )
{
  sequence->cur_pts = sequence->seq_pts = 0;

  if ( !free_refs )
    return;

  sequence->bufpos = 0;
  sequence->bufseek = 0;
  sequence->start = -1;
  sequence->top_field_first = 0;
  sequence->reset = VO_NEW_SEQUENCE_FLAG;
}

void crystalhd_mpeg_free_sequence( sequence_mpeg_t *sequence )
{
  lprintf( "crystalhd_mpeg_free_sequence\n" );
  sequence->have_header = 0;
  sequence->profile = DECODER_PROFILE_MPEG1;
  sequence->chroma = 0;
  crystalhd_mpeg_reset_sequence( sequence, 1 );
}

void mpeg_sequence_header( crystalhd_video_decoder_t *this, uint8_t *buf, int len )
{
  sequence_mpeg_t *sequence = (sequence_mpeg_t*)&this->sequence_mpeg;

  int i;

  if ( sequence->cur_pts ) {
    sequence->seq_pts = sequence->cur_pts;
    sequence->cur_pts = 0;
  }

  bits_reader_set( &sequence->br, buf, len );
  sequence->coded_width = read_bits( &sequence->br, 12 );
  lprintf( "coded_width: %d\n", sequence->coded_width );
  sequence->coded_height = read_bits( &sequence->br, 12 );
  lprintf( "coded_height: %d\n", sequence->coded_height );
  int rt = read_bits( &sequence->br, 4 );
  switch ( rt ) {
    case 1: this->ratio = 1.0; break;
    case 2: this->ratio = 4.0/3.0; break;
    case 3: this->ratio = 16.0/9.0; break;
    case 4: this->ratio = 2.21; break;
    default: this->ratio = (double)sequence->coded_width/(double)sequence->coded_height;
  }
  lprintf( "ratio: %d\n", rt );
  int fr = read_bits( &sequence->br, 4 );
  switch ( fr ) {
    case 1: this->video_step = 3913; break; /* 23.976.. */
    case 2: this->video_step = 3750; break; /* 24 */
    case 3: this->video_step = 3600; break; /* 25 */
    case 4: this->video_step = 3003; break; /* 29.97.. */
    case 5: this->video_step = 3000; break; /* 30 */
    case 6: this->video_step = 1800; break; /* 50 */
    case 7: this->video_step = 1525; break; /* 59.94.. */
    case 8: this->video_step = 1509; break; /* 60 */
  }
  if (this->reported_video_step != this->video_step){
    _x_stream_info_set( this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step) );
  }
  lprintf( "frame_rate: %d\n", fr );
  int tmp;
  tmp = read_bits( &sequence->br, 18 );
  lprintf( "bit_rate_value: %d\n", tmp );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "marker_bit: %d\n", tmp );
  tmp = read_bits( &sequence->br, 10 );
  lprintf( "vbv_buffer_size_value: %d\n", tmp );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "constrained_parameters_flag: %d\n", tmp );
  i = read_bits( &sequence->br, 1 );
  lprintf( "load_intra_quantizer_matrix: %d\n", i );
  if ( i ) {
    read_bits( &sequence->br, 8 * 64 );
  } 
  i = read_bits( &sequence->br, 1 );
  lprintf( "load_non_intra_quantizer_matrix: %d\n", i );
  if ( i ) {
    read_bits( &sequence->br, 8 * 64 );
  }
   
  if ( !sequence->have_header ) {
    sequence->have_header = 1;
    _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, sequence->coded_width );
    _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, sequence->coded_height );
    _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*this->ratio) );
    _x_stream_info_set( this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step) );
    _x_meta_info_set_utf8( this->stream, XINE_META_INFO_VIDEOCODEC, "MPEG1/2 (vdpau)" );
    xine_event_t event;
    xine_format_change_data_t data;
    event.type = XINE_EVENT_FRAME_FORMAT_CHANGE;
    event.stream = this->stream;
    event.data = &data;
    event.data_length = sizeof(data);
    data.width = sequence->coded_width;
    data.height = sequence->coded_height;
    data.aspect = this->ratio;
    xine_event_send( this->stream, &event );
  }

}

void mpeg_picture_header( sequence_mpeg_t *sequence, uint8_t *buf, int len )
{
  if ( sequence->picture.state!=WANT_HEADER )
    return;

  if ( sequence->cur_pts ) {
    sequence->seq_pts = sequence->cur_pts;
    sequence->cur_pts = 0;
  }

  if ( sequence->profile==DECODER_PROFILE_MPEG1 )
    sequence->picture.picture_structure = PICTURE_FRAME;

  if( sequence->picture.picture_structure && sequence->picture.slices_count2 )
    crystalhd_mpeg_reset_picture( &sequence->picture );

  if ( sequence->picture.picture_structure==PICTURE_FRAME ) {
    crystalhd_mpeg_reset_picture( &sequence->picture );
  }

  bits_reader_set( &sequence->br, buf, len );
  int tmp = read_bits( &sequence->br, 10 );
  lprintf( "temporal_reference: %d\n", tmp );
  read_bits( &sequence->br, 3 );
  skip_bits( &sequence->br, 16 );

  if ( sequence->profile==DECODER_PROFILE_MPEG1 )
    sequence->picture.state = WANT_SLICE;
  else
    sequence->picture.state = WANT_EXT;
}

void mpeg_sequence_extension( sequence_mpeg_t *sequence, uint8_t *buf, int len )
{
  bits_reader_set( &sequence->br, buf, len );
  int tmp = read_bits( &sequence->br, 4 );
  lprintf( "extension_start_code_identifier: %d\n", tmp );
  skip_bits( &sequence->br, 1 );
  switch ( read_bits( &sequence->br, 3 ) ) {
    case 5: sequence->profile = DECODER_PROFILE_MPEG2_SIMPLE; break;
    default: sequence->profile = DECODER_PROFILE_MPEG2_MAIN;
  }
  skip_bits( &sequence->br, 4 );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "progressive_sequence: %d\n", tmp );
  if ( read_bits( &sequence->br, 2 ) == 2 )
    sequence->chroma = VO_CHROMA_422;
  tmp = read_bits( &sequence->br, 2 );
  lprintf( "horizontal_size_extension: %d\n", tmp );
  tmp = read_bits( &sequence->br, 2 );
  lprintf( "vertical_size_extension: %d\n", tmp );
  tmp = read_bits( &sequence->br, 12 );
  lprintf( "bit_rate_extension: %d\n", tmp );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "marker_bit: %d\n", tmp );
  tmp = read_bits( &sequence->br, 8 );
  lprintf( "vbv_buffer_size_extension: %d\n", tmp );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "low_delay: %d\n", tmp );
  tmp = read_bits( &sequence->br, 2 );
  lprintf( "frame_rate_extension_n: %d\n", tmp );
  tmp = read_bits( &sequence->br, 5 );
  lprintf( "frame_rate_extension_d: %d\n", tmp );
}

void mpeg_picture_coding_extension( sequence_mpeg_t *sequence, uint8_t *buf, int len )
{
  if ( sequence->picture.state!=WANT_EXT )
    return;

  //if ( sequence->picture.picture_structure && sequence->picture.picture_structure!=PICTURE_FRAME )

  bits_reader_set( &sequence->br, buf, len );
  int tmp = read_bits( &sequence->br, 4 );
  lprintf( "extension_start_code_identifier: %d\n", tmp );
  lprintf( "f_code_0_0: %d\n", read_bits( &sequence->br, 4 ) );
  lprintf( "f_code_0_1: %d\n", read_bits( &sequence->br, 4 ) );
  lprintf( "f_code_1_0: %d\n", read_bits( &sequence->br, 4 ) );
  lprintf( "f_code_1_1: %d\n", read_bits( &sequence->br, 4 ) );
  lprintf( "intra_dc_precision: %d\n", read_bits( &sequence->br, 2 ) );
  sequence->picture.picture_structure = read_bits( &sequence->br, 2 );
  lprintf( "picture_structure: %d\n", sequence->picture.picture_structure );
  lprintf( "top_field_first: %d\n", read_bits( &sequence->br, 1 ) );
  lprintf( "frame_pred_frame_dct: %d\n", read_bits( &sequence->br, 1 ) );
  lprintf( "concealment_motion_vectors: %d\n", read_bits( &sequence->br, 1 ) );
  lprintf( "q_scale_type: %d\n", read_bits( &sequence->br, 1 ) );
  lprintf( "intra_vlc_format: %d\n", read_bits( &sequence->br, 1 ) );
  lprintf( "alternate_scan: %d\n", read_bits( &sequence->br, 1 ) );
  lprintf( "repeat_first_field: %d\n", read_bits( &sequence->br, 1 ) );
  lprintf( "chroma_420_type: %d\n", read_bits( &sequence->br, 1 ) );
  sequence->picture.progressive_frame = read_bits( &sequence->br, 1 );
  lprintf( "progressive_frame: %d\n", sequence->picture.progressive_frame );
  sequence->picture.state = WANT_SLICE;
}

void mpeg_copy_slice( sequence_mpeg_t *sequence, uint8_t *buf, int len )
{
  int size = sequence->picture.slices_pos+len;
  if ( sequence->picture.slices_size < size ) {
    sequence->picture.slices_size = size+1024;
    sequence->picture.slices = realloc( sequence->picture.slices, sequence->picture.slices_size );
  }
  xine_fast_memcpy( sequence->picture.slices+sequence->picture.slices_pos, buf, len );
  sequence->picture.slices_pos += len;
  if ( sequence->picture.slices_pos_top )
    sequence->picture.slices_count2++;
  else
    sequence->picture.slices_count++;
}

int mpeg_parse_code( crystalhd_video_decoder_t *this, uint8_t *buf, int len )
{
  sequence_mpeg_t *sequence = (sequence_mpeg_t*)&this->sequence_mpeg;

  if ( !sequence->have_header && buf[3]!=sequence_header_code ) {
    lprintf( " ----------- no sequence header yet.\n" );
    return 0;
  }

  if ( (buf[3] >= begin_slice_start_code) && (buf[3] <= end_slice_start_code) ) {
    lprintf( " ----------- slice_start_code\n" );
    if ( sequence->picture.state==WANT_SLICE )
      mpeg_copy_slice( sequence, buf, len );
    return 0;
  }
  else if ( sequence->picture.state==WANT_SLICE && sequence->picture.slices_count ) {
    if ( !sequence->picture.slices_count2 ) {
      sequence->picture.slices_pos_top = sequence->picture.slices_pos;
    }
    /* no more slices, decode */
    return 1;
  }

  switch ( buf[3] ) {
    case sequence_header_code:
      lprintf( " ----------- sequence_header_code\n" );
      mpeg_sequence_header( this, buf+4, len-4 );
      break;
    case extension_start_code: {
      switch ( buf[4]>>4 ) {
        case sequence_ext_sc:
          lprintf( " ----------- sequence_extension_start_code\n" );
          mpeg_sequence_extension( sequence, buf+4, len-4 );
          break;
        case quant_matrix_ext_sc:
          lprintf( " ----------- quant_matrix_extension_start_code\n" );
          break;
        case picture_coding_ext_sc:
          lprintf( " ----------- picture_coding_extension_start_code\n" );
          mpeg_picture_coding_extension( sequence, buf+4, len-4 );
          break;
        case sequence_display_ext_sc:
          lprintf( " ----------- sequence_display_extension_start_code\n" );
          break;
      }
      break;
    }
    case user_data_start_code:
      lprintf( " ----------- user_data_start_code\n" );
      break;
    case group_start_code:
      lprintf( " ----------- group_start_code\n" );
      break;
    case picture_start_code:
      lprintf( " ----------- picture_start_code\n" );
      mpeg_picture_header( sequence, buf+4, len-4 );
      break;
    case sequence_error_code:
      lprintf( " ----------- sequence_error_code\n" );
      break;
    case sequence_end_code:
      lprintf( " ----------- sequence_end_code\n" );
      break;
  }
  return 0;
}

void mpeg_decode_picture( crystalhd_video_decoder_t *this, uint8_t end_of_sequence ) {

  sequence_mpeg_t *seq = (sequence_mpeg_t*)&this->sequence_mpeg;
  picture_mpeg_t *pic = (picture_mpeg_t*)&seq->picture;

  pic->state = WANT_HEADER;

  if ( seq->profile == DECODER_PROFILE_MPEG1 )
    pic->picture_structure=PICTURE_FRAME;

  if ( pic->picture_structure!=PICTURE_FRAME && !pic->slices_count2 ) {
    /* waiting second field */
    lprintf("********************* no slices_count2 **********************\n");
    return;
  }

  seq->reset = 0;

  unsigned long len = (pic->picture_structure==PICTURE_FRAME)? pic->slices_pos : pic->slices_pos_top;
  unsigned char *buf = valloc(len);

  lprintf("crystalhd_mpeg: slice buf len %ld\n", len);

  xine_fast_memcpy(buf, pic->slices, len);

#define  FILEDEBUG 1
#ifdef FILEDEBUG
  FILE *fp = fopen("mpeg.dat", "a+");
  fwrite(buf, len, 1, fp);
  fclose(fp);
#endif

  crystalhd_send_data(this, hDevice, buf, len, seq->cur_pts);
  free(buf);
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
void crystalhd_mpeg_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;
  sequence_mpeg_t *seq = (sequence_mpeg_t*)&this->sequence_mpeg;

  if ( buf->pts )
    seq->cur_pts = buf->pts;

  int size = seq->bufpos+buf->size;
  if ( seq->bufsize < size ) {
    seq->bufsize = size+1024;
    seq->buf = realloc( seq->buf, seq->bufsize );
  }
  xine_fast_memcpy( seq->buf+seq->bufpos, buf->content, buf->size );
  seq->bufpos += buf->size;

  while ( seq->bufseek <= seq->bufpos-4 ) {
    uint8_t *buffer = seq->buf+seq->bufseek;
    if ( buffer[0]==0 && buffer[1]==0 && buffer[2]==1 ) {
      if ( seq->start<0 ) {
        seq->start = seq->bufseek;
      }
      else {
        if ( mpeg_parse_code( this, seq->buf+seq->start, seq->bufseek-seq->start ) ) {
          mpeg_decode_picture( this, 0 );
          mpeg_parse_code( this, seq->buf+seq->start, seq->bufseek-seq->start );
        }
        uint8_t *tmp = (uint8_t*)malloc(seq->bufsize);
        xine_fast_memcpy( tmp, seq->buf+seq->bufseek, seq->bufpos-seq->bufseek );
        seq->bufpos -= seq->bufseek;
        seq->start = -1;
        seq->bufseek = -1;
        free( seq->buf );
        seq->buf = tmp;
      }
    }
    ++seq->bufseek;
  }

  /* still image detection -- don't wait for further data if buffer ends in sequence end code */
  if (seq->start >= 0 && seq->buf[seq->start + 3] == sequence_end_code) {
    if (mpeg_parse_code(this, seq->buf+seq->start, 4)) {
      mpeg_decode_picture( this, 1 );
      mpeg_parse_code(this, seq->buf+seq->start, 4);
    }
    seq->start = -1;
  }
}

