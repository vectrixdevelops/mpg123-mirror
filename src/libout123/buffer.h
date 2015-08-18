/*
	buffer.h: output buffer

	copyright 1999-2015 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Daniel Kobras / Oliver Fromme
*/

/*
 * Application specific interaction between main and buffer
 * process. This is much less generic than the functions in
 * xfermem so I chose to put it in buffer.[hc].
 * 01/28/99 [dk]
 */

#ifndef _MPG123_BUFFER_H_
#define _MPG123_BUFFER_H_

#include "compat.h"
#include "audio.h"
/* TODO: Wrap xfermem usage into buffer API calls. */
#include "xfermem.h"
#include "out123_int.h"

void buffer_ignore_lowmem(audio_output_t *ao);
void buffer_end(audio_output_t *ao, int rude);
void buffer_resync(audio_output_t *ao);
void plain_buffer_resync(audio_output_t *ao);
void buffer_reset(audio_output_t *ao);
void buffer_start(audio_output_t *ao);
void buffer_stop(audio_output_t *ao);

/* To be reworked.
	Start and end buffer process without delay.
	This also handles the xfermem structure.
*/
int  buffer_init(audio_output_t *ao, size_t bytes);
void buffer_exit(audio_output_t *ao);

#endif
