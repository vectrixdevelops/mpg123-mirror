/*
	out123_int: internal header for libout123

	copyright ?-2015 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Michael Hipp (some traces left)
*/

#ifndef _MPG123_OUT123_INT_H_
#define _MPG123_OUT123_INT_H_

#include "config.h"
#include "out123_intsym.h"
#include "compat.h"
#include "out123.h"
#include "module.h"


/* 3% rate tolerance */
#define AUDIO_RATE_TOLERANCE	  3

typedef struct audio_output_struct
{
	int fn;			/* filenumber */
	void *userptr;	/* driver specific pointer */
	
	/* Callbacks */
	int (*open)(struct audio_output_struct *);
	int (*get_formats)(struct audio_output_struct *);
	int (*write)(struct audio_output_struct *, unsigned char *,int);
	void (*flush)(struct audio_output_struct *);
	int (*close)(struct audio_output_struct *);
	int (*deinit)(struct audio_output_struct *);
	
	/* the module this belongs to */
	mpg123_module_t *module;
	
	char *device;	/* device name */
	int   flags;	/* some bits; namely headphone/speaker/line */
	long rate;		/* sample rate */
	long gain;		/* output gain */
	int channels;	/* number of channels */
	int format;		/* format flags */
	int framesize;	/* Output needs data in chunks of framesize bytes. */
	int is_open;	/* something opened? */
	int auxflags; /* For now just one: quiet mode (for probing). */
} audio_output_t;

/* Lazy. */
#define AOQUIET (ao->auxflags & MPG123_OUT_QUIET)

struct audio_format_name {
	int  val;
	char *name;
	char *sname;
};

int cdr_open(audio_output_t *);
int au_open(audio_output_t *);
int wav_open(audio_output_t *);
int wav_write(unsigned char *buf,int len);
int cdr_close(void);
int au_close(void);
int wav_close(void);

#endif

