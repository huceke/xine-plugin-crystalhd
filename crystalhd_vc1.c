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

//#define LOG

#include "crystalhd_vc1.h"

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

/**************************************************************************
 * crystalhd_vc1 specific decode functions
 *************************************************************************/

void crystalhd_vc1_reset_picture( picture_vc1_t *pic )
{
    pic->slices = 1;
}

void crystalhd_vc1_init_picture( picture_vc1_t *pic )
{
    memset( pic, 0, sizeof( picture_vc1_t ) );
}

void crystalhd_vc1_reset_sequence( sequence_vc1_t *sequence ) {
  lprintf( "crystalhd_vc1_reset_sequence\n" );
  sequence->bufpos = 0;
  sequence->bufseek = 0;
  sequence->start = -1;
  sequence->code_start = sequence->current_code = 0;
  sequence->seq_pts = sequence->cur_pts = 0;
  crystalhd_vc1_reset_picture( &sequence->picture );
}

void crystalhd_vc1_init_sequence( sequence_vc1_t *sequence ) {
  lprintf( "crystalhd_vc1_init_sequence\n" );
  sequence->have_header = 0;
  sequence->profile = PROFILE_VC1_SIMPLE;
  sequence->picture.hrd_param_flag = 0;
  sequence->bytestream = NULL;
  sequence->bytestream_bytes = 0;
  crystalhd_vc1_reset_sequence( sequence );
}

void crystalhd_vc1_handle_buffer (crystalhd_video_decoder_t *this,
		uint8_t *bytestream, uint32_t bytestream_bytes) {

  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;
  uint32_t buf_len = bytestream_bytes + sequence->bytestream_bytes;
  uint8_t *buf = valloc(buf_len + 4);
  uint8_t *p = buf;

	if(bytestream_bytes == 0) return;

  if(hDevice == 0) return;

  lprintf("handle buffer\n");

  if(sequence->profile == PROFILE_VC1_ADVANCED) {


    buf_len = bytestream_bytes + sequence->bytestream_bytes;

    //if ((bytestream[0] == 0x00) && (bytestream[1] == 0x00) && (bytestream[2] == 0x01) && (bytestream[3] == 0x0D)) {
    //      ((bytestream[3] == 0x0F) || (bytestream[3] == 0x0D) || (bytestream[3] == 0x0E))) {

    //if(sequence->mode == MODE_STARTCODE) {

    if ((bytestream[0] == 0x00) && (bytestream[1] == 0x00) && (bytestream[2] == 0x01)) {

      if(sequence->bytestream_bytes) {
        xine_fast_memcpy(p, sequence->bytestream, sequence->bytestream_bytes);
        p+=sequence->bytestream_bytes;
      }

      if(!this->set_form && hDevice) {
        this->set_form = 1;
        hDevice = crystalhd_start(this, hDevice, BC_STREAM_TYPE_ES, BC_VID_ALGO_VC1, 0, NULL, 0, 0, 0,
            this->scaling_enable, this->scaling_width);
      }

    } else {

      if(!this->set_form && hDevice) {
        this->set_form = 1;
        hDevice = crystalhd_start(this, hDevice, BC_STREAM_TYPE_ES, BC_VID_ALGO_VC1MP, 1, 
            sequence->bytestream, sequence->bytestream_bytes, 0, 0,
            this->scaling_enable, this->scaling_width);
      }
      buf_len = bytestream_bytes;

    }

    free(sequence->bytestream);
    sequence->bytestream = NULL;
    sequence->bytestream_bytes = 0;

  } else if (sequence->profile == PROFILE_VC1_MAIN) {

    if(sequence->have_header) {
      
      if(!this->set_form && hDevice) {

        hDevice = crystalhd_start(this, hDevice, BC_STREAM_TYPE_ES, BC_VID_ALGO_VC1MP, 0,
            sequence->bytestream, sequence->bytestream_bytes, sequence->coded_width, sequence->coded_height,
            this->scaling_enable, this->scaling_width);

        this->set_form = 1;

        buf_len = bytestream_bytes;

      }
    }
  }

  xine_fast_memcpy(p, bytestream, bytestream_bytes);
  //crystalhd_decode_package(buf, 20);

  if(this->set_form) {
    crystalhd_send_data(this, hDevice, buf, buf_len, sequence->seq_pts);
  }

  free(buf);
}

void update_metadata(crystalhd_video_decoder_t *this)
{

  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;

  if ( !sequence->have_header ) {
    sequence->have_header = 1;

    this->width   = sequence->coded_width;
    this->height  = sequence->coded_height;

    set_video_params(this);
  }
}

void sequence_header_advanced( crystalhd_video_decoder_t *this, uint8_t *buf, int len )
{
  lprintf( "sequence_header_advanced\n" );
  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;

  if ( len < 5 )
    return;

  sequence->profile = PROFILE_VC1_ADVANCED;
  lprintf("PROFILE_VC1_ADVANCED\n");
  bits_reader_set( &sequence->br, buf, len );
  skip_bits( &sequence->br, 16 );
  sequence->coded_width = read_bits( &sequence->br, 12 )<<1;
  sequence->coded_height = (read_bits( &sequence->br, 12 )+1)<<1;
  skip_bits( &sequence->br, 1 );
  sequence->picture.interlace = read_bits( &sequence->br, 1 );
  skip_bits( &sequence->br, 1 );
  sequence->picture.finterpflag = read_bits( &sequence->br, 1 );
  skip_bits( &sequence->br, 2 );
  sequence->picture.maxbframes = 7;
  if ( read_bits( &sequence->br, 1 ) ) {
    double w, h;
    int ar=0;
    w = read_bits( &sequence->br, 14 )+1;
    h = read_bits( &sequence->br, 14 )+1;
    if ( read_bits( &sequence->br, 1 ) ) {
      ar = read_bits( &sequence->br, 4 );
    }
    if ( ar==15 ) {
      w = read_bits( &sequence->br, 8 );
      h = read_bits( &sequence->br, 8 );
      this->ratio = w/h;
      lprintf("aspect_ratio (w/h) = %f\n", this->ratio);
    }
    else if ( ar && ar<14 ) {
      this->ratio = sequence->coded_width*aspect_ratio[ar]/sequence->coded_height;
      lprintf("aspect_ratio = %f\n", this->ratio);
    }

    if ( read_bits( &sequence->br, 1 ) ) {
      if ( read_bits( &sequence->br, 1 ) ) {
        int exp = read_bits( &sequence->br, 16 );
        lprintf("framerate exp = %d\n", exp);
        exp = 0;
      }
      else {
        double nr = read_bits( &sequence->br, 8 );
        switch ((int)nr) {
          case 1: nr = 24000; break;
          case 2: nr = 25000; break;
          case 3: nr = 30000; break;
          case 4: nr = 50000; break;
          case 5: nr = 60000; break;
          default: nr = 0;
        }
        double dr = read_bits( &sequence->br, 4 );
        switch ((int)dr) {
          case 2: dr = 1001; break;
          default: dr = 1000;
        }
        this->video_step = 90000/(nr/dr);
        //lprintf("framerate = %f video_step = %d\n", nr/dr, sequence->video_step);
      }
    }
    if ( read_bits( &sequence->br, 1 ) ) {
      int col = read_bits( &sequence->br, 8 );
      lprintf("color_standard = %d\n", col);
      col = 0;
      skip_bits( &sequence->br, 16 );
    }
  }
  
  sequence->picture.hrd_param_flag = read_bits( &sequence->br, 1 );
  if ( sequence->picture.hrd_param_flag )
    sequence->picture.hrd_num_leaky_buckets = read_bits( &sequence->br, 5 );

  update_metadata(this);
}

void sequence_header( crystalhd_video_decoder_t *this, uint8_t *buf, int len )
{
  lprintf( "sequence_header\n" );
  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;

  if ( len < 4 )
    return;

  bits_reader_set( &sequence->br, buf, len);
  switch ( read_bits( &sequence->br, 2 ) ) {
    case 0: sequence->profile = PROFILE_VC1_SIMPLE; lprintf("PROFILE_VC1_SIMPLE\n"); break;
    case 1: sequence->profile = PROFILE_VC1_MAIN; lprintf("PROFILE_VC1_MAIN\n"); break;
    case 2: sequence->profile = PROFILE_VC1_MAIN; lprintf("PROFILE_VC1_MAIN\n"); break;
    case 3: return sequence_header_advanced( this, buf, len ); break;
    default: return; /* illegal value, broken header? */
  }
  skip_bits( &sequence->br, 22 );
  sequence->picture.rangered = read_bits( &sequence->br, 1 );
  sequence->picture.maxbframes = read_bits( &sequence->br, 3 );
  skip_bits( &sequence->br, 2 );
  sequence->picture.finterpflag = read_bits( &sequence->br, 1 );

  update_metadata(this);
}

void entry_point( crystalhd_video_decoder_t *this, uint8_t *buf, int len ) {

  lprintf( "entry_point\n" );
  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;

  bits_reader_set( &sequence->br, buf, len );
  skip_bits( &sequence->br, 13 );

  if ( sequence->picture.hrd_param_flag ) {
    int i;
    for ( i=0; i<sequence->picture.hrd_num_leaky_buckets; ++i )
      skip_bits( &sequence->br, 8 );
  }

  if ( read_bits( &sequence->br, 1 ) ) {
    sequence->coded_width = (read_bits( &sequence->br, 12 )+1)<<1;
    sequence->coded_height = (read_bits( &sequence->br, 12 )+1)<<1;
  }

}

void picture_header( crystalhd_video_decoder_t *this, uint8_t *buf, int len )
{
  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;
  picture_vc1_t *pic = (picture_vc1_t*)&sequence->picture;
  int tmp;

  lprintf("picture_header\n");

  bits_reader_set( &sequence->br, buf, len );
  skip_bits( &sequence->br, 2 );

  if ( pic->finterpflag )
    skip_bits( &sequence->br, 1 );
  if ( pic->rangered ) {
    pic->rangered = (read_bits( &sequence->br, 1 ) << 1) +1;
  }
  if ( !pic->maxbframes ) {
    if ( read_bits( &sequence->br, 1 ) )
      pic->picture_vc1_type = P_FRAME;
    else
      pic->picture_vc1_type = I_FRAME;
  }
  else {
    if ( read_bits( &sequence->br, 1 ) )
      pic->picture_vc1_type = P_FRAME;
    else {
      if ( read_bits( &sequence->br, 1 ) )
        pic->picture_vc1_type = I_FRAME;
      else
        pic->picture_vc1_type = B_FRAME;
    }
  }
  if ( pic->picture_vc1_type == B_FRAME ) {
    tmp = read_bits( &sequence->br, 3 );
    if ( tmp==7 ) {
      tmp = (tmp<<4) | read_bits( &sequence->br, 4 );
      if ( tmp==127 )
        pic->picture_vc1_type = BI_FRAME;
    }
  }
}

void picture_header_advanced( crystalhd_video_decoder_t *this, uint8_t *buf, int len )
{
  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;
  picture_vc1_t *pic = (picture_vc1_t*)&sequence->picture;

  lprintf("picture_header_advanced\n");

  bits_reader_set( &sequence->br, buf, len );

  if ( pic->interlace ) {
    lprintf("frame->interlace=1\n");
    if ( !read_bits( &sequence->br, 1 ) ) {
      lprintf("progressive frame\n");
      pic->frame_coding_mode = PICTURE_FRAME;
    }
    else {
      if ( !read_bits( &sequence->br, 1 ) ) {
        lprintf("frame interlaced\n");
        pic->frame_coding_mode = PICTURE_FRAME_INTERLACE;
      }
      else {
        lprintf("field interlaced\n");
        pic->frame_coding_mode = PICTURE_FIELD_INTERLACE;
      }
    }
  }
  if ( pic->interlace && pic->frame_coding_mode == PICTURE_FIELD_INTERLACE ) {
    pic->fptype = read_bits( &sequence->br, 3 );
    switch ( pic->fptype ) {
      case FIELDS_I_I:
      case FIELDS_I_P:
        pic->picture_vc1_type = I_FRAME; break;
      case FIELDS_P_I:
      case FIELDS_P_P:
        pic->picture_vc1_type = P_FRAME; break;
      case FIELDS_B_B:
      case FIELDS_B_BI:
        pic->picture_vc1_type = B_FRAME; break;
      default:
        pic->picture_vc1_type = BI_FRAME;
    }
  } else {
    if ( !read_bits( &sequence->br, 1 ) )
      pic->picture_vc1_type = P_FRAME;
    else {
      if ( !read_bits( &sequence->br, 1 ) )
        pic->picture_vc1_type = B_FRAME;
      else {
        if ( !read_bits( &sequence->br, 1 ) )
          pic->picture_vc1_type = I_FRAME;
        else {
          if ( !read_bits( &sequence->br, 1 ) )
            pic->picture_vc1_type = BI_FRAME;
          else {
            pic->picture_vc1_type = P_FRAME;
            pic->skipped = 1;
          }
        }
      }
    }
  }
}

void parse_header( crystalhd_video_decoder_t *this, uint8_t *buf, int len ) 
{
  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;
  int off=0;

  lprintf("parse_header\n");

  while ( off < (len-4) ) {
    uint8_t *buffer = buf+off;
    if ( buffer[0]==0 && buffer[1]==0 && buffer[2]==1 ) {
      switch ( buffer[3] ) {
        case sequence_header_code: 
          sequence_header( this, buf+off+4, len-off-4 ); 

          free(sequence->bytestream);
          sequence->bytestream_bytes = len-off;
          sequence->bytestream = realloc( sequence->bytestream, sequence->bytestream_bytes );
          xine_fast_memcpy(sequence->bytestream, buf+off, sequence->bytestream_bytes);
          
          break;
      }
    }
    ++off;
  }
  if ( !sequence->have_header ) {

    free(sequence->bytestream);
    sequence->bytestream_bytes = len;
    sequence->bytestream = realloc( sequence->bytestream, sequence->bytestream_bytes );
    xine_fast_memcpy(sequence->bytestream, buf, sequence->bytestream_bytes);

    sequence_header( this, buf, len );
  }
}

void remove_emulation_prevention( uint8_t *src, uint8_t *dst, int src_len, int *dst_len )
{
  int i;
  int len = 0;
  int removed = 0;

  for ( i=0; i<src_len-3; ++i ) {
    if ( src[i]==0 && src[i+1]==0 && src[i+2]==3 ) {
      lprintf("removed emulation prevention byte\n");
      dst[len++] = src[i];
      dst[len++] = src[i+1];
      i += 2;
      ++removed;
    }
    else {
      xine_fast_memcpy( dst+len, src+i, 4 );
      ++len;
    }
  }
  for ( ; i<src_len; ++i )
    dst[len++] = src[i];
  *dst_len = src_len-removed;
}

int parse_code( crystalhd_video_decoder_t *this, uint8_t *buf, int len )
{
  sequence_vc1_t *sequence = (sequence_vc1_t*)&this->sequence_vc1;

  if ( !sequence->have_header && buf[3]!=sequence_header_code )
    return 0;

  if ( sequence->code_start == frame_start_code ) {
    if ( sequence->current_code==field_start_code || sequence->current_code==slice_start_code ) {
      sequence->picture.slices++;
      return -1;
	  }
    return 1; /* frame complete, decode */
  }


  switch ( buf[3] ) {
    int dst_len;
    uint8_t *tmp;
    case sequence_header_code:
      lprintf("sequence_header_code\n");
      tmp = malloc( len );
      remove_emulation_prevention( buf, tmp, len, &dst_len );
      sequence_header( this, tmp+4, dst_len-4 );
          
      free(sequence->bytestream);
      sequence->bytestream_bytes = dst_len;
      sequence->bytestream = realloc( sequence->bytestream, sequence->bytestream_bytes );
      xine_fast_memcpy(sequence->bytestream, tmp, sequence->bytestream_bytes);

      free( tmp );
      break;
    case entry_point_code:
      lprintf("entry_point_code\n");
      tmp = malloc( len );
      remove_emulation_prevention( buf, tmp, len, &dst_len );
      entry_point( this, tmp+4, dst_len-4 );
          
      sequence->bytestream_bytes += dst_len;
      sequence->bytestream = realloc( sequence->bytestream, sequence->bytestream_bytes);
      xine_fast_memcpy(sequence->bytestream + ( sequence->bytestream_bytes - dst_len), tmp, dst_len);

      free( tmp );
      break;
    case sequence_end_code:
      lprintf("sequence_end_code\n");
      break;
    case frame_start_code:
      lprintf("frame_start_code, len=%d\n", len);
      break;
    case field_start_code:
      lprintf("field_start_code\n");
      break;
    case slice_start_code:
      lprintf("slice_start_code, len=%d\n", len);
      break;
  }

  return 0;
}

void decode_picture( crystalhd_video_decoder_t *this )
{
  sequence_vc1_t *seq = (sequence_vc1_t*)&this->sequence_vc1;
  picture_vc1_t *pic = (picture_vc1_t*)&seq->picture;

  uint8_t *buf;
  int len;

  pic->skipped = 0;
  pic->field = 0;

  if ( seq->mode == MODE_FRAME ) {
    buf = seq->buf;
    len = seq->bufpos;

    if ( seq->profile==PROFILE_VC1_ADVANCED )
      picture_header_advanced( this, buf, len );
    else
      picture_header( this, buf, len );

    if ( len < 2 ) {
      pic->skipped = 1;
    } else {
      crystalhd_vc1_handle_buffer( this, seq->buf, seq->bufpos);
    }

  } 
  else {
    buf = seq->buf+seq->start+4;
    len = seq->bufseek-seq->start-4;


    if ( seq->profile==PROFILE_VC1_ADVANCED ) {
      int tmplen = (len>50) ? 50 : len;
      uint8_t *tmp = malloc( tmplen );
      remove_emulation_prevention( buf, tmp, tmplen, &tmplen );
      picture_header_advanced( this, tmp, tmplen );
      free( tmp );
    }
    else
      picture_header( this, buf, len );

    if ( len < 2 ) {
      pic->skipped = 1;
    } else {
      crystalhd_vc1_handle_buffer( this, seq->buf+seq->start, seq->bufseek-seq->start);
    }
  }

  if ( pic->skipped )
    pic->picture_vc1_type = P_FRAME;

  if(pic->skipped) {
	  crystalhd_vc1_reset_picture( &seq->picture );
    return;
  }

  seq->seq_pts += this->video_step;
  crystalhd_vc1_reset_picture( &seq->picture );
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
void crystalhd_vc1_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;
  sequence_vc1_t *seq = (sequence_vc1_t*)&this->sequence_vc1;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    //lprintf("BUF_FLAG_HEADER\n");
  }

  seq->cur_pts = buf->pts;

	if(buf->decoder_flags & BUF_FLAG_STDHEADER) {
    //lprintf("BUF_FLAG_STDHEADER\n");
    xine_bmiheader *bih = (xine_bmiheader *) buf->content;
    int bs = sizeof( xine_bmiheader );
		seq->coded_width  = this->width		= bih->biWidth;
		seq->coded_height = this->height  = bih->biHeight;
    //lprintf( "width=%d height=%d\n", bih->biWidth, bih->biHeight );
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

  if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
    //lprintf("BUF_FLAG_FRAME_START\n");
    seq->seq_pts = buf->pts;
    seq->mode = MODE_FRAME;
    if ( seq->bufpos > 3 ) {
      if ( seq->buf[0]==0 && seq->buf[1]==0 && seq->buf[2]==1 ) {
        seq->mode = MODE_STARTCODE;
      }
    }
  }

  if ( seq->mode == MODE_FRAME ) {
    if ( buf->decoder_flags & BUF_FLAG_FRAME_END ) {
      //lprintf("BUF_FLAG_FRAME_END\n");
      decode_picture( this );
      seq->bufpos = 0;
    }
    return;
  }

  int res, startcode=0;
  while ( seq->bufseek <= seq->bufpos-4 ) {
    uint8_t *buffer = seq->buf+seq->bufseek;
    if ( buffer[0]==0 && buffer[1]==0 && buffer[2]==1 ) {
      startcode = 1;
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
          seq->mode = MODE_STARTCODE;
          decode_picture(this);
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

