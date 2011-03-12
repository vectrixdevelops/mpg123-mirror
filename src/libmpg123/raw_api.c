/*
	raw_api.c: raw api decoding interface

	copyright 2011 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Vincent Falco, then meanly beaten up by Thomas Orgis...
*/

#include "mpg123lib_intern.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
/* For select(), I need select.h according to POSIX 2001, else: sys/time.h sys/types.h unistd.h (the latter two included in compat.h already). */
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef _MSC_VER
#include <io.h>
#endif

#include "compat.h"
#include "debug.h"

/* VFALCO: Convert an ambiguous return value into a clear description */
static int failed( int returnValue )
{
	return returnValue != 0;
}

/* VFALCO: This is used to make sure we return a sensible error code */
static int error_code( mpgraw_state* rs )
{
	int result = rs->mh->err;
	if( ! result ) /* VFALCO: Assuming MPG123_OK == 0 everywhere for brevity */
		result = MPG123_ERR; /* VFALCO: Lets hope we never get here */
	return result;
}

/*----------------------------------------------------------------------------*/

int mpgraw_open(
	mpgraw_state *rs,
	const char *decoder,
	long samplerate,
	int channels,
	int encodings )
{
	size_t i, nrates;
	const long *rates;

	memset( rs, 0, sizeof( *rs ) );

	rs->mh = mpg123_new( decoder, &rs->error );

	if( rs->mh )
	{
		if( ! rs->error )
		{
			if( failed( mpg123_format_none( rs->mh ) ) )
				rs->error = error_code( rs );
		}

		if( !rs->error )
		{
			if( failed( mpg123_param (rs->mh, MPG123_ADD_FLAGS, MPG123_SKIP_ID3V2, 0)))
				rs->error = error_code (rs);
		}

		if( !rs->error )
		{
			/* GAPLESS must be off when using raw api */
			if( failed( mpg123_param (rs->mh, MPG123_REMOVE_FLAGS, MPG123_GAPLESS, 0)))
				rs->error = error_code (rs);
		}

		if( ! rs->error )
		{
			if(samplerate == 0)
			{
				rates = NULL;
				nrates = 0;
				mpg123_rates(&rates, &nrates);
				for(i=0; i<nrates; i++)
				{
					if( failed( mpg123_format( rs->mh, rates[i], channels,  encodings ) ) )
						rs->error = error_code( rs );
				}
			}
			else
			{
				if( failed( mpg123_format( rs->mh, samplerate, channels, encodings ) ) )
					rs->error = error_code( rs );
			}
		}

		if( ! rs->error )
		{
			mpg123_param(rs->mh, MPG123_FEEDPOOL, 2, 0.);
			mpg123_param(rs->mh, MPG123_FEEDBUFFER, 16384, 0.);
			/* One might want to tune the number and size of internal buffers. */
			if( failed( mpg123_open_feed( rs->mh ) ) )
				rs->error = error_code( rs );
		}
		
		if( rs->error )
		{
			/* clean up */
			mpg123_delete( rs->mh );
			rs->mh = 0;
		}
	}
	else
	{
		/* in case mpg123_new() didn't set the error */
		if( rs->error == MPG123_OK )
			rs->error = MPG123_ERR;
	}

	return rs->error;
}

/*----------------------------------------------------------------------------*/

/* VFALCO: Should allow this to be called with buffer=0 meaning clear
 * the input and return MPG123_NEED_MORE in the next call to next. */
int mpgraw_feed(
	mpgraw_state *rs,
	void *buffer,
	size_t bytes )
{
	return mpg123_feed(rs->mh, buffer, bytes);
}

/*----------------------------------------------------------------------------*/

void mpgraw_seek(
	mpgraw_state* rs,
	off_t current_offset )
{
	/* Let's be brutal: Clean up the internal buffering by simply closing/reopening.
	   Then set the file position... for whateever use it has. */
	mpg123_close(rs->mh);
	if( failed( mpg123_open_feed( rs->mh ) ) )
		rs->error = error_code( rs );
	else
		rs->error = MPG123_OK;

	feed_set_pos(rs->mh, current_offset);
}

/*----------------------------------------------------------------------------*/

int mpgraw_next(
	mpgraw_state* rs,
	int flags )
{
	mpg123_handle* mh = rs->mh;

	rs->error = MPG123_OK;

	/* That ripped loop is funky now. */
	do
	{
		if( ! rs->error )
		{
			rs->error = mpg123_framebyframe_next(mh);
			if( rs->error == MPG123_NEW_FORMAT )
			{
				/* pick up new format information */
				mpg123_getformat( mh, &rs->rate, &rs->channels, &rs->encoding );
				rs->frame_count = spf(mh);
			}
			if( rs->error == MPG123_OK ||
				rs->error == MPG123_DONE ||
				rs->error == MPG123_NEW_FORMAT)
			{
				/* Get the whole frame info */
				mpg123_info( mh, &rs->frameinfo );
				if(failed(mpg123_framedata( mh, &rs->header, &rs->body, &rs->body_bytes)))
				rs->error = MPG123_ERR;

				/* Raw users never want to see MPG123_DONE */
				if( rs->error == MPG123_DONE)
					rs->error = MPG123_OK;

				/* Exit loop */
				break;
			}
		}
	}
	while( ! rs->error );

	return rs->error;
}

/*----------------------------------------------------------------------------*/

int mpgraw_decode(
	mpgraw_state* rs,
	void* dest,
	size_t bytes )
{
	mpg123_handle* mh = rs->mh;

	rs->error = mpg123_framebyframe_decode( mh, &rs->num, &rs->audio, &rs->bytes );

	/* Copy to caller's buffer */
	if( dest )
	{
		if( bytes > rs->bytes )
			bytes = rs->bytes;
		memcpy( dest, rs->audio, bytes );
	}

	rs->error = MPG123_OK;

	return rs->error;
}

/*----------------------------------------------------------------------------*/

int attribute_align_arg mpg123_decode_raw(mpg123_handle *fr, off_t *num, unsigned char **audio, size_t *bytes)
{
	mpgraw_state* rs = fr->rdat.rs;

	int error;

	error = mpgraw_next( rs, 0 );

	if( ! error )
	{
		error = mpgraw_decode( rs, 0, 0 );
	}

	if( ! error )
	{
		if( audio )
			*audio = rs->audio;
		if( bytes )
			*bytes = rs->bytes;
	}

	return error;
}

/*----------------------------------------------------------------------------*/

void mpgraw_close(
	mpgraw_state *rs )
{
	mpg123_close( rs->mh );
	mpg123_delete( rs->mh );
	memset( rs, 0, sizeof( *rs ) );
}

