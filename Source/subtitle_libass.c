/*
 * Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#include "subtitle_libass.h"

#ifdef CONFIG_LIBASS

#include "astdlib.h"
#include "debug.h"

#include <ass/ass.h>
#include <libavutil/mem.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct SUBTITLE_LIBASS_RENDERER {
	ASS_Library *library;
	ASS_Renderer *renderer;
	ASS_Track *track;
	const char *default_font;
};

static int clamp_int( int value, int low, int high )
{
	if( value < low )
		return low;
	if( value > high )
		return high;
	return value;
}

static void subtitle_libass_message_cb( int level, const char *fmt, va_list va, void *data )
{
	char msg[512];
	(void)data;

	vsnprintf( msg, sizeof( msg ), fmt, va );
	msg[sizeof( msg ) - 1] = '\0';
	serprintf( "subtitle_libass: libass[%d]: %s\n", level, msg );
}

static const char *subtitle_libass_find_default_font( void )
{
	static const char *candidates[] = {
		"/system/fonts/NotoSans-Regular.ttf",
		"/system/fonts/Roboto-Regular.ttf",
		"/system/fonts/DroidSans.ttf",
		"/product/fonts/NotoSans-Regular.ttf",
		"/system_ext/fonts/NotoSans-Regular.ttf",
		NULL,
	};

	for( int i = 0; candidates[i]; i++ ) {
		if( access( candidates[i], R_OK ) == 0 ) {
			serprintf( "subtitle_libass: selected default font [%s]\n", candidates[i] );
			return candidates[i];
		}
	}

	serprintf( "subtitle_libass: no Android default font file found, using libass provider fallback\n" );
	return NULL;
}

static SUBTITLE_LIBASS_RENDERER *subtitle_libass_alloc( void )
{
	SUBTITLE_LIBASS_RENDERER *renderer = acalloc( 1, sizeof( *renderer ) );
	if( !renderer ) {
		serprintf( "subtitle_libass: renderer allocation failed\n" );
		return NULL;
	}

	renderer->library = ass_library_init();
	if( !renderer->library ) {
		serprintf( "subtitle_libass: ass_library_init failed\n" );
		goto ErrorExit;
	}

	ass_set_message_cb( renderer->library, subtitle_libass_message_cb, NULL );

	ass_set_extract_fonts( renderer->library, 1 );
	if( access( "/system/fonts", R_OK ) == 0 ) {
		ass_set_fonts_dir( renderer->library, "/system/fonts" );
		serprintf( "subtitle_libass: font directory set to [/system/fonts]\n" );
	}

	renderer->renderer = ass_renderer_init( renderer->library );
	if( !renderer->renderer ) {
		serprintf( "subtitle_libass: ass_renderer_init failed\n" );
		goto ErrorExit;
	}

	renderer->default_font = subtitle_libass_find_default_font();
	ass_set_fonts( renderer->renderer, renderer->default_font, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, NULL, 1 );
	serprintf( "subtitle_libass: renderer ready default_font=%s provider=autodetect\n",
		renderer->default_font ? renderer->default_font : "(null)" );
	return renderer;

ErrorExit:
	subtitle_libass_close( renderer );
	return NULL;
}

SUBTITLE_LIBASS_RENDERER *subtitle_libass_open_codec_private( const unsigned char *data, int size )
{
	if( !data || size <= 0 ) {
		serprintf( "subtitle_libass: missing embedded codec private data\n" );
		return NULL;
	}

	serprintf( "subtitle_libass: opening embedded ASS/SSA codec private data, size=%d\n", size );

	SUBTITLE_LIBASS_RENDERER *renderer = subtitle_libass_alloc();
	if( !renderer )
		return NULL;

	renderer->track = ass_new_track( renderer->library );
	if( !renderer->track ) {
		serprintf( "subtitle_libass: ass_new_track failed for embedded ASS/SSA\n" );
		goto ErrorExit;
	}

	ass_process_codec_private( renderer->track, (char*)data, size );
	serprintf( "subtitle_libass: embedded ASS/SSA track opened\n" );
	return renderer;

ErrorExit:
	subtitle_libass_close( renderer );
	return NULL;
}

SUBTITLE_LIBASS_RENDERER *subtitle_libass_open_file( const char *path )
{
	if( !path ) {
		serprintf( "subtitle_libass: missing ASS/SSA file path\n" );
		return NULL;
	}

	serprintf( "subtitle_libass: opening ASS/SSA file [%s]\n", path );

	SUBTITLE_LIBASS_RENDERER *renderer = subtitle_libass_alloc();
	if( !renderer )
		return NULL;

	renderer->track = ass_read_file( renderer->library, (char*)path, NULL );
	if( !renderer->track ) {
		serprintf( "subtitle_libass: ass_read_file failed for [%s]\n", path );
		goto ErrorExit;
	}

	serprintf( "subtitle_libass: ASS/SSA file opened [%s]\n", path );
	return renderer;

ErrorExit:
	subtitle_libass_close( renderer );
	return NULL;
}

void subtitle_libass_close( SUBTITLE_LIBASS_RENDERER *renderer )
{
	if( !renderer )
		return;
	serprintf( "subtitle_libass: closing renderer\n" );
	if( renderer->track )
		ass_free_track( renderer->track );
	if( renderer->renderer )
		ass_renderer_done( renderer->renderer );
	if( renderer->library )
		ass_library_done( renderer->library );
	afree( renderer );
}

int subtitle_libass_process_chunk( SUBTITLE_LIBASS_RENDERER *renderer, const unsigned char *data, int size, int time_ms, int duration_ms )
{
	if( !renderer || !renderer->track || !data || size <= 0 ) {
		serprintf( "subtitle_libass: dropping invalid chunk renderer=%p data=%p size=%d\n", renderer, data, size );
		return 1;
	}

	serprintf( "subtitle_libass: process chunk size=%d time=%d duration=%d\n", size, time_ms, duration_ms );
	ass_process_chunk( renderer->track, (char*)data, size, time_ms, duration_ms );
	return 0;
}

static void blend_pixel( unsigned char *dst, int r, int g, int b, int src_a )
{
	if( src_a <= 0 )
		return;
	if( src_a > 255 )
		src_a = 255;

	int dst_b = dst[0];
	int dst_g = dst[1];
	int dst_r = dst[2];
	int dst_a = dst[3];
	int inv_a = 255 - src_a;
	int dst_part = (dst_a * inv_a + 127) / 255;
	int out_a = src_a + dst_part;

	if( out_a <= 0 ) {
		memset( dst, 0, 4 );
		return;
	}

	dst[0] = ( b * src_a + dst_b * dst_part + out_a / 2 ) / out_a;
	dst[1] = ( g * src_a + dst_g * dst_part + out_a / 2 ) / out_a;
	dst[2] = ( r * src_a + dst_r * dst_part + out_a / 2 ) / out_a;
	dst[3] = out_a;
}

int subtitle_libass_render( SUBTITLE_LIBASS_RENDERER *renderer, int time_ms, int duration_ms, int width, int height, VIDEO_FRAME *frame )
{
	if( !renderer || !renderer->renderer || !renderer->track || !frame || width <= 0 || height <= 0 ) {
		serprintf( "subtitle_libass: invalid render request renderer=%p frame=%p size=%dx%d\n", renderer, frame, width, height );
		return 1;
	}

	ass_set_frame_size( renderer->renderer, width, height );
	serprintf( "subtitle_libass: render request time=%d duration=%d frame=%dx%d default_font=%s\n",
		time_ms, duration_ms, width, height, renderer->default_font ? renderer->default_font : "(null)" );

	int changed = 0;
	ASS_Image *images = ass_render_frame( renderer->renderer, renderer->track, time_ms, &changed );
	if( !images ) {
		serprintf( "subtitle_libass: render produced no images at time=%d changed=%d\n", time_ms, changed );
		return 1;
	}

	int left = width;
	int top = height;
	int right = 0;
	int bottom = 0;
	int image_count = 0;

	for( ASS_Image *img = images; img; img = img->next ) {
		if( !img->bitmap || img->w <= 0 || img->h <= 0 )
			continue;
		image_count++;

		int x0 = clamp_int( img->dst_x, 0, width );
		int y0 = clamp_int( img->dst_y, 0, height );
		int x1 = clamp_int( img->dst_x + img->w, 0, width );
		int y1 = clamp_int( img->dst_y + img->h, 0, height );

		if( x0 >= x1 || y0 >= y1 )
			continue;

		if( x0 < left )
			left = x0;
		if( y0 < top )
			top = y0;
		if( x1 > right )
			right = x1;
		if( y1 > bottom )
			bottom = y1;
	}

	if( left >= right || top >= bottom ) {
		serprintf( "subtitle_libass: render images had empty bounding box count=%d\n", image_count );
		return 1;
	}

	int bb_width = right - left;
	int bb_height = bottom - top;
	int stride = (bb_width * 4 + 31) & ~31;
	int size = stride * bb_height;
	unsigned char *buffer = av_mallocz( size );
	if( !buffer ) {
		serprintf( "subtitle_libass: cannot allocate %dx%d subtitle bitmap\n", bb_width, bb_height );
		return 1;
	}

	frame->data[0] = buffer;
	frame->linestep[0] = stride;
	frame->window.x = left;
	frame->window.y = top;
	frame->window.width = bb_width;
	frame->window.height = bb_height;
	frame->width = width;
	frame->height = height;
	frame->colorspace = AV_IMAGE_BGRA_32;
	frame->valid = size;
	frame->duration = duration_ms;
	serprintf( "subtitle_libass: render output images=%d bbox=%d,%d %dx%d stride=%d bytes=%d changed=%d\n",
		image_count, left, top, bb_width, bb_height, stride, size, changed );

	for( ASS_Image *img = images; img; img = img->next ) {
		if( !img->bitmap || img->w <= 0 || img->h <= 0 )
			continue;

		int x0 = clamp_int( img->dst_x, left, right );
		int y0 = clamp_int( img->dst_y, top, bottom );
		int x1 = clamp_int( img->dst_x + img->w, left, right );
		int y1 = clamp_int( img->dst_y + img->h, top, bottom );

		if( x0 >= x1 || y0 >= y1 )
			continue;

		uint32_t color = img->color;
		int r = (color >> 24) & 0xff;
		int g = (color >> 16) & 0xff;
		int b = (color >> 8) & 0xff;
		int base_a = 255 - (color & 0xff);

		for( int y = y0; y < y1; y++ ) {
			unsigned char *dst = buffer + (y - top) * stride + (x0 - left) * 4;
			const unsigned char *src = img->bitmap + (y - img->dst_y) * img->stride + (x0 - img->dst_x);

			for( int x = x0; x < x1; x++ ) {
				int src_a = (base_a * *src + 127) / 255;
				blend_pixel( dst, r, g, b, src_a );
				src++;
				dst += 4;
			}
		}
	}

	return 0;
}

#endif
