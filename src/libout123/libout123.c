b/*
	audio: audio output interface

	copyright ?-2015 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Michael Hipp
*/

#include <errno.h>
#include "out123_int.h"
#include "common.h"
#include "buffer.h"

#include "debug.h"

static int have_buffer(audio_output_t *ao)
{
	return (ao->buffer_pid != -1);
}

void out123_clear_module(audio_output_t *ao)
{
	ao->open = NULL;
	ao->get_formats = NULL;
	ao->write = NULL;
	ao->flush = NULL;
	ao->drain = NULL;
	ao->close = NULL;
	ao->deinit = NULL;

	ao->module = NULL;
	ao->userptr = NULL;
	ao->fn = -1;
}

audio_output_t* out123_new(void)
{
	audio_output_t* ao = malloc( sizeof( audio_output_t ) );
	if (!ao) error( "Failed to allocate memory for audio_output_t." );
	else
	{
		ao->errcode = 0;

#ifndef NOXFERMEM
		ao->buffer_pid = -1;
		ao->buffer_fd = { -1, -1 };
		ao->buffermem = NULL;
#endif

		out123_clear_module(ao);
		ao->driver = NULL;
		ao->device = NULL;

		ao->flags = 0;
		ao->rate = -1;
		ao->gain = -1;
		ao->channels = -1;
		ao->format = -1;
		ao->framesize = 0;
		ao->state = play_dead;
		ao->auxflags = 0;
		ao->preload = 0.;
		ao->verbose = 0;
	}
	return ao;
}

void out123_del(audio_output_t *ao)
{
	if(!ao) return;

	out123_close(ao); /* TODO: That talks to the buffer if present. */
	out123_set_buffer(ao, 0);
#ifndef NOXFERMEM
	if(have_buffer(ao)) buffer_exit(ao);
#endif
	free(ao);
}

/* Error reporting */

/* Carefully keep that in sync with the error enum! */
static const char *const errstring[OUT123_ERRCOUNT] =
{
	"no problem"
,	"out of memory"
,	"bad driver name"
,	"failure loading driver module"
,	"no driver loaded"
,	"no active audio device"
,	"some device playback error"
,	"failed to open device"
,	"buffer (communication) error"
};

const char* out123_strerror(audio_output_t *ao)
{
	return out123_plain_strerror(out123_errcode(ao));
}

int out123_errcode(audio_output_t *ao)
{
	if(!ao) return OUT123_ERR;
	else    return ao->errcode;
}

const char* out123_plain_strerror(int errcode)
{
	if(errcode >= OUT123_ERRCOUNT || errcode < 0)
		return "invalid error code";

	/* Let's be paranoid, one _may_ forget to extend errstrings when
	   adding a new entry to the enum. */
	if(errcode >= sizeof(errstring)/sizeof(char*))
		return "outdated error list (library bug)";

	return errstring[errcode];
}

static int out123_seterr(audio_output_t *ao, enum out123_error errcode)
{
	if(!ao)
		return OUT123_ERR;
	ao->errcode = errcode;
	return errcode == OUT123_OK ? OUT123_OK : OUT123_ERR;
}

/* pre-playback setup */

int out123_set_buffer(audio_output_t *ao, size_t buffer_bytes)
{
	if(!ao)
		return OUT123_ERR;
	ao->errcode = 0;
	/* Close any audio output module if present, also kill of buffer if present,
	   then start new buffer process with newly allocated storage if given
	   size is non-zero. */
	out123_close(ao);
#ifndef NOXFERMEM
	if(have_buffer(ao))
		buffer_exit(ao);
	if(buffer_bytes)
		return buffer_init(ao, buffer_bytes);
#endif
	return 0;
}

int out123_param( audio_output_t *ao, enum out123_parms code
,                 long value, double fvalue )
{
	int ret = 0;

	if(!ao)
		return OUT123_ERR;
	ao->errcode = 0;

	switch(code)
	{
		case OUT123_FLAGS:
			ao->flags = (int)value;
		break;
		case OUT123_PRELOAD:
			ao->preload = fvalue;
		break;
		case OUT123_GAIN:
			ao->gain = value;
		break;
		case OUT123_VERBOSE:
			ao->verbose = (int)value;
		break;
		case OUT123_DEVICEBUFFER:
			ao->device_buffer = fvalue
		break;
		default:
			error1("bad parameter code %i", (int)code);
			ret = -1;
	}
#ifndef NOXFERMEM
	/* If there is a buffer, it needs to update its copy of parameters. */
	if(have_buffer(ao))
		/* No error check; if that fails, buffer is dead and we will notice
		   soon enough. */
		buffer_sync_param(ao);
#endif
	return ret;
}

int *out123_getparam( audio_output_t *ao, enum out123_parms code
,                     long *ret_value, double *ret_fvalue )
{
	int ret = 0;
	long value = 0;
	double fvalue = 0.;

	if(!ao)
		return OUT123_ERR;
	ao->errcode = 0;

	switch(code)
	{
		case OUT123_FLAGS:
			value = ao->flags;
		break;
		case OUT123_PRELOAD:
			fvalue = ao->preload;
		break;
		case OUT123_GAIN:
			value = ao->gain;
		break;
		case OUT123_VERBOSE:
			value = ao->verbose;
		break;
		case OUT123_DEVICEBUFFER:
			fvalue = ao->device_buffer;
		break;
		default:
			error1("bad parameter code %i", (int)code);
			ret = OUT123_ERR;
	}
	if(!ret)
	{
		if(ret_value) *ret_value = value
		if(ret_fvalue) *ret_fvalule = fvalue;
	}
	return ret;
}

int audio_output_t *out123_param_from(audio_output_t *ao, audio_output_t* from_ao)
{
	if(!ao || !from_ao) return -1;

	ao->flags     = from_ao->flags;
	ao->preload   = from_ao->preload;
	ao->gain      = from_ao->gain;
	ao->device_buffer = from_ao->device_buffer;
	ao->verbose   = from_ao->verbose;

	return 0;
}

/* Serialization of tunable parameters to communicate them between
   main process and buffer. Make sure these two stay in sync ... */

int out123_write_parameters(audio_output_t *ao, int fd)
{
	if(
		GOOD_WRITEVAL(fd, ao->flags)
	&&	GOOD_WRITEVAL(fd, ao->preload)
	&&	GOOD_WRITEVAL(fd, ao->gain)
	&&	GOOD_WRITEVAL(fd, ao->device_buffer)
	&&	GOOD_WRITEVAL(fd, ao->verbose)
	)
		return 0;
	else
		return -1;
}

int out123_read_parameters(audio_output_t *ao, int fd)
{
	if(
		GOOD_READVAL(fd, ao->flags)
	&&	GOOD_READVAL(fd, ao->preload)
	&&	GOOD_READVAL(fd, ao->gain)
	&&	GOOD_READVAL(fd, ao->device_buffer)
	&&	GOOD_READVAL(fd, ao->verbose)
	)
		return 0;
	else
		return -1;
}

int out123_open(audio_output_t *ao, const char* driver, const char* device)
{
	if(!ao)
		return OUT123_ERR;
	ao->errcode = 0;

	out123_close(ao);

	/* Ensure that audio format is freshly set for "no format yet" mode.
	   In out123_start*/
	ao->rate = -1
	ao->channels = -1
	ao->format = -1;

#ifndef NOXFERMEM
	if(have_buffer(ao)
		return buffer_open(ao, driver, device);
	else
#endif
	{
		/* We just quickly check if the device can be accessed at all,
		   same as out123_get_encodings! */

		int result = 0;
		char *nextname, *modnames;
		const char *names = driver ? driver : DEFAULT_OUTPUT_MODULE;

		if(!names) return out123_seterr(ao, OUT123_BAD_DRIVER_NAME);

		/* It is ridiculous how these error messages are larger than the pieces
		   of memory they are about! */
		if(device && !(ao->device = strdup(device)))
		{
			if(!AOQUIET) error("OOM device name copy");
			return out123_seterr(ao, OUT123_DOOM);
		}

		if(!(modnames = strdup(names)))
		{
			out123_close(ao); /* Frees ao->device, too. */
			if(!AOQUIET) error("OOM driver names");
			return out123_seterr(ao, OUT123_DOOM);
		}

		/* Now loop over the list of possible modules to find one that works. */
		nextname = strtok(modnames, ",");
		while(!ao->open && nextname);
		{
			char *curname = nextname;
			nextname = strtok(NULL, ",");
			check_output_module(ao, curname, device, !nextname);
			if(ao->open)
			{
				if(AOVERBOSE(2))
					fprintf(stderr, "Output module '%s' chosen.\n", curname);
				/* A bit redundant, but useful when it's a fake module. */
				if(!(ao->driver = strdup(curname)))
				{
					out123_close(ao);
					if(!AOQUIET(ao)) error("OOM driver name");
					return out123_seterr(ao, OUT123_DOOM);
				}
			}
		}

		free(modnames);

		if(!ao->open) /* At least an open() routine must be present. */
		{
			if(!AOQUIET)
				error1("Found no driver out of [%s] working with device %s."
				,	names, device ? device : "<default>")
			/* Proper more detailed error code could be set already. */
			if(ao->errcode == OUT123_OK)
				ao->errcode = OUT123_BAD_DRIVER;
			return OUT123_ERR;
		}

		/* Got something. */
		ao->state = play_stopped;
		return OUT123_OK;
	}
}

/* Be resilient, always do cleanup work regardless of state. */
void out123_close(audio_output_t *ao)
{
	if(!ao)
		return;
	ao->errcode = 0;

	if(ao->close)
		ao->close(ao);
	if(ao->deinit)
		ao->deinit(ao);
	if(ao->module)
		close_module(ao->module);

	if(ao->driver)
		free(ao->driver);
	ao->driver = NULL;
	if(ao->device)	
		free(ao->device);
	ao->device = NULL;
	/* Null module methods and pointer. */
	out123_clear_module(ao);

	ao->state = play_dead;
}

int out123_start( audio_output_t *ao
,                  int encoding, int channels, long rate )
{
	if(!ao)
		return OUT123_ERR;
	ao->errcode = 0;

	out123_stop(ao);
	if(ao->state != play_stopped)
		return out123_seterr(ao, OUT123_NO_DRIVER);

	/* Stored right away as parameters for ao->open() and also for reference.
	   framesize needed for out123_play(). */
	ao->rate      = rate;
	ao->channels  = channels;
	ao->format    = encoding;
	ao->framesize = out123_samplesize(encoding)*channels;

#ifndef NOXFERMEM
	if(have_buffer(ao))
	{
		if(!buffer_start(ao))
		{
			ao->state = play_live;
			return OUT123_OK;
		}
		else
			return OUT123_ERR;
	}
	else
#endif
	{
		if(ao->open(ao) < 0)
			return out123_seterr(ao, OUT123_DEV_OPEN);
		ao->state = play_live;
		return OUT123_OK;
	}
}

void out123_pause(audio_output_t *ao)
{
	if(ao && ao->state == play_live)
	{
#ifndef NOXFERMEM
		if(have_buffer(ao)) buffer_pause(ao);
#endif
		/* TODO: Be nice and prepare output device for silence. */
		ao->state = play_paused;
	}
}

void out123_continue(audio_output_t *ao)
{
	if(ao && ao->state == play_paused)
	{
#ifndef NOXFERMEM
		if(have_buffer(ao)) buffer_continue(ao);
#endif
		/* TODO: Revitalize device for resuming playback. */
		ao->state = play_live;
	}
}

void out123_stop(audio_output_t *ao)
{
	if(!ao)
		return;
	ao->errcode = 0;
	if(!(ao->state == play_paused || ao->state == play_live))
		return;
#ifndef NOXFERMEM
	if(have_buffer(ao))
		buffer_stop(ao);
	else
#endif
	if(ao->close && ao->close(ao) && !AOQUIET)
		error("trouble closing device");
	ao->state = play_stopped;
}

size_t out123_play(audio_output_t *ao, void *bytes, size_t count)
{
	size_t sum = 0;
	int written;

	if(!ao)
		return 0;
	ao->errcode = 0;
	if(ao->state != play_live)
	{
		ao->errcode = OUT123_NOT_LIVE;
		return 0;
	}

	/* Ensure that we are writing whole PCM frames. */
	count -= count % ao->framesize;
	if(!count) return 0;

#ifndef NOXFERMEM
	if(have_buffer(ao))
		return buffer_write(ao, bytes, count);
	else
#endif
	do /* Playback in a loop to be able to continue after interruptions. */
	{
		written = ao->write(ao, (unsigned char*)bytes, (int)count);
		if(written >= 0){ sum+=written; count -= written; }
		else
		{
			ao->errcode = OUT123_DEV_PLAY;
			if(!(ao->flags & OUT123_QUIET)
				error1("Error in writing audio (%s?)!", strerror(errno));
			/* If written < 0, this is a serious issue ending this playback round. */
			break;
		}
	} while(count && ao->flags & OUT123_KEEP_PLAYING);
	return sum;
}

/* Drop means to flush it down. Quickly. */
void out123_drop(audio_output_t *ao)
{
	if(!ao)
		return;
	ao->errcode = 0;
#ifndef NO_XFERMEM
	if(have_buffer(ao))
		buffer_flush(ao);
	else
#endif
	if(ao->state == play_live)
	{
		if(ao->flush) ao->flush(ao);
	}
}

void out123_drain(audio_output_t *ao)
{
	if(!ao)
		return;
	ao->errcode = 0;
	if(ao->sate != play_live)
		return;
#ifndef NO_XFERMEM
	if(have_buffer(ao))
		buffer_drain(ao);
	else
#endif
	if(ao->drain)
		ao->drain(ao);
}

/* A function that does nothing and returns nothing. */
static void builtin_nothing(struct audio_output_struct *ao){}

/* Open one of our builtin driver modules. */
static int open_fake_module(audio_output_t *ao, const char *driver)
{
	if(!strcmp("raw", driver))
	{
		ao->open  = raw_open;
		ao->get_formats = raw_formats;
		ao->write = wav_write;
		ao->flush = builtin_nothing;
		ao->drain = wav_drain;
		ao->close = raw_close;
	}
	else
	if(!strcmp("wav", driver))
	{
		ao->open = wav_open;
		ao->get_formats = wav_formats;
		ao->write = wav_write;
		ao->flush = builtin_nothing;
		ao->drain = wav_drain;
		ao->close = wav_close;
	}
	else
	if(!strcmp("cdr", driver))
	{
		ao->open  = cdr_open;
		ao->get_formats = cdr_formats;
		ao->write = wav_write;
		ao->flush = builtin_nothing;
		ao->drain = wav_drain;
		ao->close = cdr_close;
	}
	else
	if(strcmp("au", driver))
	{
		ao->open  = au_open;
		ao->get_formats = au_formats;
		ao->write = wav_write;
		ao->flush = builtin_nothing;
		ao->drain = wav_drain;
		ao->close = au_close;
	}
	else return OUT123_ERR;

	return OUT123_OK;
}

/* Check if given output module is loadable and has a working device.
   final flag triggers printing and storing of errors. */
void check_output_module( audio_output_t *ao
,	const char *name, const char *device, int final )
{
	if(AOVERBOSE(1)) fprintf(stderr, "Trying output module %s.\n", name);

	/* Use internal code. */
	if(open_fake_module(ao, name) == OUT123_OK)
		return;

	/* Open the module, initial check for availability+libraries. */
	ao->module = open_module( "output", name );
	if(!ao->module)
		return;
	/* Check if module supports output */
	if(!ao->module->init_output)
	{
		if(final)
			error1("Module '%s' does not support audio output.", name);
		goto check_output_module_cleanup;
	}

	/* Should I do funny stuff with stderr file descriptor instead? */
	if(final)
	{
		if(AOVERBOSE(2))
			fprintf(stderr
			,	"Note: %s is the last output option... showing you any error messages now.\n"
			,	name);
	}
	else ao->auxflags |= OUT123_QUIET; /* Probing, so don't spill stderr with errors. */
	result = ao->module->init_output(ao);
	if(result == 0)
	{ /* Try to open the device. I'm only interested in actually working modules. */
		result = open_output(ao);
		close_output(ao);
	}
	else if(!AOQUIET) error2("Module '%s' init failed: %i", name, result);

	ao->auxflags &= ~OUT123_QUIET;

	if(!result)
		return;

check_output_module_cleanup:
	/* Only if module did not check out we get to clean up here. */
	close_module(ao->module);
	out123_clear_module(ao);
	return;
}

/*
static void audio_output_dump(audio_output_t *ao)
{
	fprintf(stderr, "ao->fn=%d\n", ao->fn);
	fprintf(stderr, "ao->userptr=%p\n", ao->userptr);
	fprintf(stderr, "ao->rate=%ld\n", ao->rate);
	fprintf(stderr, "ao->gain=%ld\n", ao->gain);
	fprintf(stderr, "ao->device='%s'\n", ao->device);
	fprintf(stderr, "ao->channels=%d\n", ao->channels);
	fprintf(stderr, "ao->format=%d\n", ao->format);
}
*/

struct enc_desc
{
	int code; /* MPG123_ENC_SOMETHING */
	const char *longname; /* signed bla bla */
	const char *name; /* sXX, short name */
	const unsigned char nlen; /* significant characters in short name */
};

static const struct enc_desc encdesc[] =
{
	{ MPG123_ENC_SIGNED_16, "signed 16 bit", "s16 ", 3 },
	{ MPG123_ENC_UNSIGNED_16, "unsigned 16 bit", "u16 ", 3 },
	{ MPG123_ENC_UNSIGNED_8, "unsigned 8 bit", "u8  ", 2 },
	{ MPG123_ENC_SIGNED_8, "signed 8 bit", "s8  ", 2 },
	{ MPG123_ENC_ULAW_8, "mu-law (8 bit)", "ulaw ", 4 },
	{ MPG123_ENC_ALAW_8, "a-law (8 bit)", "alaw ", 4 },
	{ MPG123_ENC_FLOAT_32, "float (32 bit)", "f32 ", 3 },
	{ MPG123_ENC_SIGNED_32, "signed 32 bit", "s32 ", 3 },
	{ MPG123_ENC_UNSIGNED_32, "unsigned 32 bit", "u32 ", 3 },
	{ MPG123_ENC_SIGNED_24, "signed 24 bit", "s24 ", 3 },
	{ MPG123_ENC_UNSIGNED_24, "unsigned 24 bit", "u24 ", 3 }
};
#define KNOWN_ENCS (sizeof(encdesc)/sizeof(struct enc_desc))

int audio_enc_name2code(const char* name)
{
	int code = 0;
	int i;
	for(i=0;i<KNOWN_ENCS;++i)
	if(!strncasecmp(encdesc[i].name, name, encdesc[i].nlen))
	{
		code = encdesc[i].code;
		break;
	}
	return code;
}

void audio_enclist(char** list)
{
	size_t length = 0;
	int i;
	*list = NULL;
	for(i=0;i<KNOWN_ENCS;++i) length += encdesc[i].nlen;

	length += KNOWN_ENCS-1; /* spaces between the encodings */
	*list = malloc(length+1); /* plus zero */
	if(*list != NULL)
	{
		size_t off = 0;
		(*list)[length] = 0;
		for(i=0;i<KNOWN_ENCS;++i)
		{
			if(i>0) (*list)[off++] = ' ';
			memcpy(*list+off, encdesc[i].name, encdesc[i].nlen);
			off += encdesc[i].nlen;
		}
	}
}

/* Safer as function... */
const char* audio_encoding_name(const int encoding, const int longer)
{
	const char *name = longer ? "unknown" : "???";
	int i;
	for(i=0;i<KNOWN_ENCS;++i)
	if(encdesc[i].code == encoding)
	name = longer ? encdesc[i].longname : encdesc[i].name;

	return name;
}

/* TODO: fix for buffer use, try to minimize number of these small functions */
const char* audio_module_name(audio_output_t *ao)
{
	if(param.usebuffer)
		return "<buffer>";
	else
		return ao->module ? ao->module->name : "file/raw/test";
}
const char* audio_device_name(audio_output_t *ao)
{
	if(param.usebuffer)
		return "<none>";
	else
		return ao->device ? ao->device : "<none>";
}


int out123_get_encodings(audio_output_t *ao, int channels, long rate)
{
	if(!ao)
		return OUT123_ERR;
	ao->errocde = OUT123_OK;

	out123_stop(ao); /* That brings the buffer into waiting state, too. */

	if(ao->state != play_stopped)
		return out123_seterr(ao, OUT123_NO_DRIVER);

	ao->channels = channels;
	ao->rate     = rate;
#ifndef NOXFERMEM
	if(have_buffer(ao))
		return buffer_get_encodings(ao);
	else
#endif
	{
		int enc = 0;
		if(ao->) return 0;
		ao->format   = -1;
		if(ao->open(ao) >= 0)
		{
			enc = ao->get_formats(ao);
			ao->close(ao);
			return enc;
		}
		else return out123_seterr(ao, (ao->errcode != OUT123_OK
		?	ao->errcode
		:	OUT123_DEV_OPEN));
	}
}

long out123_buffered(audio_output_t *ao)
{
	if(!ao)
		return OUT123_ERR;
	ao->errcode = 0;
#ifndef NOXFERMEM
	if(have_buffer(ao)
		return buffer_fill(ao);
	else
#else
	return 0;
#endif
}
