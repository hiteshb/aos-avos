/*
 * Copyright 2017 Archos SA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "types.h"
#include "global.h"
#include "debug.h"
#include "astdlib.h"
#include "stream.h"
#include "util.h"
#include "subtitle_libass.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>

#ifdef CONFIG_STREAM
#ifdef CONFIG_VOBSUB

#define DBGS	if(Debug[DBG_STREAM])
#define DBG 	if(Debug[DBG_SUB])
#define DBG2 	if(Debug[DBG_SUB] > 1)

typedef struct SSA_PRIV {
	int start;
	int end;
	int text;
#ifdef CONFIG_LIBASS
	SUBTITLE_LIBASS_RENDERER *renderer;
	int use_libass;
#endif
} SSA_PRIV;

#define SKIP_SPACE(x) while(*x && isspace(*x)){x++;}

static char *get_line( char *c, char **line )
{
	*line = c;
	while( *c ) {
		if( *c == '\r' && *(c + 1) == '\n' ) {
			// end of line, terminate
			*c = '\0';
			c += 2;
			return c;
		} else if( *c == '\n' || *c == '\r' ) {
			// end of line, terminate
			*c = '\0';
			c++;
			return c;
		}
		c++;
	}
	
	if( c == *line )
		return NULL;
		
	return c;
}

static char *get_word( char *c, char **word )
{
	SKIP_SPACE( c );
	*word = c;
	while( *c ) {
		if( *c == '\r' && *(c + 1) == '\n' ) {
			// end of line, terminate
			*c = '\0';
			c += 2;
			return c;
		} else if( *c == ',' || *c == '\n' ) {
			// end of line, terminate
			*c = '\0';
			c++;
			return c;
		}
		c++;
	}
	if( c == *word )
		return NULL;
	return c;
}

static int SSA_parse_format( char *format, SSA_PRIV *priv )
{	
	char *c = format;
	int count = 0;
	
	// skip the format tag:
	c = strstr( c, ":" );
	if( !c )
		return 1;
	c++;
	SKIP_SPACE( c );
	
	char *word;
	
	while( (c = get_word( c, &word )) ) {
DBG2 serprintf("word: %d [%s]\r\n", count, word );		
		if( !strcmpNC( word, "Start" ) ) {
			priv->start = count;
		} else if( !strcmpNC( word, "End" ) ) {
			priv->end = count;
		} else if( !strcmpNC( word, "Text" ) ) {
			priv->text = count;
		}
		count++;
	}
	return 0;
}

static int SSA_parse_header( char *data, SSA_PRIV *priv )
{
	// check header
	if(!strstrNC(data,"[Script Info]")){
DBG serprintf( "SSA: not SSA!\n" );
		return 1;
	}
DBG serprintf( "SSA: found header!\n" );

	// look for events
	char *c    = data;
	char *line;
	while( (c = get_line( c, &line )) ) {
//printf("%s\r\n", line );
		if( strstrNC( line, "[Events]" ) ) {
			// next line is the format:
			char *format;
			c = get_line( c, &format );
			if( c ) {
				format = strstrNC( format, "Format:" );
				if( format ) {
DBG serprintf("FORMAT: %s\r\n", format );
					SSA_parse_format( format, priv );
				}
			} 
			break;
		}
	}
	if( !priv->start && !priv->end && !priv->text ) {
DBG serprintf("force defaults!\r\n" );
		// assume defaults:
		priv->start = 1;
		priv->end   = 2;
		priv->text  = 9;
	}
DBG serprintf("start %d  end %d  text %d\r\n", priv->start, priv->end, priv->text );
	return 0;
}

#define SS_TO_MS(x) ((x)*1000)
#define MM_TO_MS(x) ((SS_TO_MS(x))*60)
#define HH_TO_MS(x) ((MM_TO_MS(x))*60)

static int get_time(char *line)
{
	int hh, mm, ss, ms;
	if(sscanf(line,"%u:%u:%u.%u", &hh, &mm, &ss, &ms) != 4){
		return -1;
	}
	return (HH_TO_MS(hh) + MM_TO_MS(mm) + SS_TO_MS(ss) + ms * 10);
}

static void SSA_clean_text( char *text )
{
	char *in  = text;
	char *out = text;
	
	while( *in ) {
		// remove all style override codes
		if( *in == '{' && *(in + 1) == '\\' ) {
			in += 2;
			while( *in && *in != '}' )
				in++;
			in++;
			if( !*in )
				break;
		}
		// replace \N by \n (so only \n exported)
		if( *in == '\\' && *(in + 1) == 'N' ) {
			*(in + 1) = 'n';
		}
		*out++ = *in++;
	}
	*out++ = *in++;
}

static int SSA_parse_dialogue( char *line, int size, SSA_PRIV *priv, VIDEO_FRAME *frame )
{	
	char *c = line;
	int count = 0;
	
	// skip the dialogue tag:
	c = strstr( c, ":" );
	if( !c )
		return 1;
	c++;
	SKIP_SPACE( c );
	
	char *word;
	
	while( count < priv->text && (c = get_word( c, &word )) ) {
DBG2 serprintf("word: %d [%s]\r\n", count, word );		
		if( count == priv->start ) {
			frame->time = get_time( word );
DBGS serprintf("start: %s  %d  ", word, frame->time );
		} else if( count == priv->end ) {
			frame->duration = get_time( word ) - frame->time;
DBGS serprintf("end:   %s  %d  ", word, frame->duration  );
		} 
		count++;
	}
	if( count == priv->text ) {
		word = c;
DBGS serprintf("text:  %s\r\n", word );
		if( frame->data[0] )
			strnZcpy( frame->data[0], word, frame->size - 1 );
		//SSA_clean_text( frame->data[0] );
DBGS if( frame->data[0] ) serprintf("clean: %s\r\n", frame->data[0] );
	}	
	return 0;
}


static int _open( STREAM_DEC_SUB *dec, SUB_PROPERTIES *sub, void *ctx )
{
DBGS serprintf("sub_dec_open_SSA\r\n");
	SSA_PRIV *priv = (SSA_PRIV*)dec->priv;
#ifdef CONFIG_LIBASS
	sub->gfx = 0;
	serprintf( "codec_ssa: CONFIG_LIBASS enabled for embedded SSA/ASS, extraDataSize=%d\n", sub->extraDataSize2 );
	if( sub->extraData2 && sub->extraDataSize2 ) {
		priv->renderer = subtitle_libass_open_codec_private( sub->extraData2, sub->extraDataSize2 );
		if( priv->renderer ) {
			STREAM *stream = (STREAM*)ctx;
			if( stream && stream->parser && stream->parser->add_subtitle_font_attachments ) {
				int font_ret = stream->parser->add_subtitle_font_attachments( stream, priv->renderer );
				serprintf( "codec_ssa: embedded ASS/SSA font attachment load result=%d\n", font_ret );
			} else {
				serprintf( "codec_ssa: no parser font attachment callback available\n" );
			}
			priv->use_libass = 1;
			sub->gfx = 1;
			serprintf( "codec_ssa: embedded SSA/ASS will render through libass bitmap path\n" );
		} else {
			serprintf( "codec_ssa: libass open failed, falling back to legacy SSA text parser\n" );
		}
	} else {
		serprintf( "codec_ssa: no embedded SSA/ASS codec private data, falling back to legacy SSA text parser\n" );
	}
#endif
	// try to parse the header
	if( sub->extraData2 && sub->extraDataSize2 ) {
DBG Dump( sub->extraData2, MIN(1024, sub->extraDataSize2) );
		if( SSA_parse_header( sub->extraData2, priv ) ) {
#ifdef CONFIG_LIBASS
			if( priv->renderer ) {
				subtitle_libass_close( priv->renderer );
				priv->renderer = NULL;
				priv->use_libass = 0;
			}
#endif
			return 1;
		}
	}  
	
	// copy the props locally
	dec->subtitle = &dec->_subtitle;
	*dec->subtitle = *sub;
	
	dec->ctx = ctx;
	dec->is_open = 1;
	
	return 0;
}

static int _close( STREAM_DEC_SUB *dec )
{
DBGS serprintf( "sub_dec_close_SSA\r\n");
	if( !dec->is_open ) {
serprintf("SSA: not open!\r\n");
		return 1;
	}
#ifdef CONFIG_LIBASS
	SSA_PRIV *priv = (SSA_PRIV*)dec->priv;
	if( priv && priv->renderer ) {
		subtitle_libass_close( priv->renderer );
		priv->renderer = NULL;
		priv->use_libass = 0;
	}
#endif
	
	dec->is_open = 0;
 	return 0;
}

static int _decode( STREAM_DEC_SUB *dec, UCHAR *data, int size, int time, VIDEO_FRAME **pframe )
{
DBG serprintf("SSA_decode: size %5d  time %d  [%s]\r\n", size, time, data );	
DBG2 Dump( data, size );
	SSA_PRIV *priv = (SSA_PRIV*)dec->priv;
#ifdef CONFIG_LIBASS
	if( priv->use_libass && priv->renderer ) {
		VIDEO_FRAME *frame = *pframe;
		frame->time = time;
		int render_time = time;
		int duration = frame->duration > 0 ? frame->duration : 100000;

		serprintf( "codec_ssa: libass decode packet size=%d time=%d duration=%d frame=%dx%d\n",
			size, render_time, duration, frame->width, frame->height );
		subtitle_libass_process_chunk( priv->renderer, data, size, render_time, duration );
		if( subtitle_libass_render( priv->renderer, render_time, duration, frame->width, frame->height, frame ) ) {
			serprintf( "codec_ssa: libass rendered no bitmap for packet time=%d\n", render_time );
			*pframe = NULL;
		} else {
			serprintf( "codec_ssa: libass bitmap ready time=%d window=%d,%d %dx%d valid=%d\n",
				frame->time, frame->window.x, frame->window.y, frame->window.width, frame->window.height, frame->valid );
		}
		return 0;
	}
#endif
	SSA_parse_dialogue( data, size, priv, *pframe );
	return 0;
}

static int _flush( STREAM_DEC_SUB *dec )
{	
	return 0;
} 

static int _destroy( STREAM_DEC_SUB *dec )
{
	if( dec	) {
		if( dec->priv ) {
#ifdef CONFIG_LIBASS
			SSA_PRIV *priv = (SSA_PRIV*)dec->priv;
			if( priv->renderer )
				subtitle_libass_close( priv->renderer );
#endif
			afree( dec->priv );
		}
		afree( dec );
	}
	return 0;
} 

static STREAM_DEC_SUB *_new_dec_ssa( void ) 
{ 
	STREAM_DEC_SUB *dec = (STREAM_DEC_SUB *)amalloc( sizeof( STREAM_DEC_SUB ) );
	
	if( !dec )
		return NULL;
	memset( dec, 0, sizeof( STREAM_DEC_SUB ) );

	static char name[] = "SSA";
	dec->name    = name;
	dec->destroy = _destroy;
	dec->open    = _open;
	dec->close   = _close;
	dec->decode  = _decode;
	dec->flush   = _flush;

	// allocate private data
	if( !(dec->priv = (SSA_PRIV*)amalloc( sizeof( SSA_PRIV ) ) ) ) {
		afree( dec );
		return NULL;
	}
	memset( dec->priv, 0, sizeof( SSA_PRIV ) );
	
	return dec;
}

STREAM_REGISTER_DEC_SUB( SUB_FORMAT_SSA, _new_dec_ssa, "SSA" );

#endif
#endif
