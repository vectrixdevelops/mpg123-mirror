/*
	audio: audio output interface

	copyright ?-2015 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Michael Hipp, as out123.h by Thomas Orgis
*/


#ifndef _OUT123_H_
#define _OUT123_H_

#include "mpg123.h"
#include "module.h"

#define AUDIO_OUT_HEADPHONES       0x01
#define AUDIO_OUT_INTERNAL_SPEAKER 0x02
#define AUDIO_OUT_LINE_OUT         0x04

enum {
	DECODE_TEST,
	DECODE_AUDIO,
	DECODE_FILE,
	DECODE_BUFFER,
	DECODE_WAV,
	DECODE_AU,
	DECODE_CDR,
	DECODE_AUDIOFILE
};

struct audio_output_struct;
typedef struct audio_output_struct audio_output_t;

#define MPG123_OUT_QUIET 1


/* ------ Declarations from "audio.c" ------ */

audio_output_t* open_output_module( const char* name );
void close_output_module( audio_output_t* ao );
const char* audio_encoding_name(const int encoding, const int longer);

int init_output(audio_output_t **ao);
void exit_output(audio_output_t *ao, int rude);
int flush_output(audio_output_t *ao, unsigned char *bytes, size_t count);
int open_output(audio_output_t *ao);
void close_output(audio_output_t *ao );
int reset_output(audio_output_t *ao);
void output_pause(audio_output_t *ao);  /* Prepare output for inactivity. */
void output_unpause(audio_output_t *ao); /* Reactivate output (buffer process). */

int audio_enc_name2code(const char* name);
void audio_enclist(char** list); /* Make a string of encoding names. */

int audio_format_support(audio_output_t *ao, long rate, int channels);
void audio_prepare_format(audio_output_t *ao, int rate, int channels, int encoding);
const char* audio_module_name(audio_output_t *ao);
const char* audio_device_name(audio_output_t *ao);

int audio_reset(audio_output_t *ao, long rate, int channels, int format);
void audio_drain(audio_output_t *ao);
long audio_buffered_bytes(audio_output_t *ao);
void audio_start(audio_output_t *ao);
void audio_stop(audio_output_t *ao);
void audio_drop(audio_output_t *ao);
void audio_continue(audio_output_t *ao);
void audio_fixme_wake_buffer(audio_output_t *ao);

#endif

