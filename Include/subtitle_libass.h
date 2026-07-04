/*
 * Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef _SUBTITLE_LIBASS_H
#define _SUBTITLE_LIBASS_H

#include "av.h"

typedef struct SUBTITLE_LIBASS_RENDERER SUBTITLE_LIBASS_RENDERER;

#ifdef CONFIG_LIBASS
SUBTITLE_LIBASS_RENDERER *subtitle_libass_open_codec_private( const unsigned char *data, int size );
SUBTITLE_LIBASS_RENDERER *subtitle_libass_open_file( const char *path );
void subtitle_libass_close( SUBTITLE_LIBASS_RENDERER *renderer );
int subtitle_libass_process_chunk( SUBTITLE_LIBASS_RENDERER *renderer, const unsigned char *data, int size, int time_ms, int duration_ms );
int subtitle_libass_render( SUBTITLE_LIBASS_RENDERER *renderer, int time_ms, int duration_ms, int width, int height, VIDEO_FRAME *frame );
#endif

#endif
