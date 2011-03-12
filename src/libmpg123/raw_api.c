/*
	raw_api.c: raw api decoding interface

	copyright 2011 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Vincent Falco
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

/* in theory this should be 2896 */
#define MIN_FEED_BYTES 0

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
			/* Need to set this before open_raw() */
			rs->mh->rdat.rs = rs;

			if( failed( open_raw( rs->mh ) ) )
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
	if( buffer )
	{
		if( bytes >= MIN_FEED_BYTES )
		{
			rs->this_frame = 0;

			rs->buffer = buffer;
			rs->bufend = ((unsigned char*)buffer) + bytes;
			rs->this_frame = buffer;
			rs->next_frame = buffer;
			rs->pos=0;

			rs->error = MPG123_OK;
		}
		else
		{
			rs->error = MPG123_BAD_BUFFER;
		}
	}
	else
	{
		rs->error = MPG123_NULL_BUFFER;
	}

	return rs->error;
}

/*----------------------------------------------------------------------------*/

void mpgraw_seek(
	mpgraw_state* rs,
	size_t current_offset )
{
	/* clear bit reservoir */

	/* need to do this because frame_outs() gets called with the */
	/* frame number and its successor in mpg123_decode_frame() */
	rs->mh->num = 0;

	/* VFALCO: Not sure what this is all about */
	/* mh->buffer.fill=0; */
	/* mh->to_decode=FALSE; */
	/* frame_reset(mh); */

	rs->buffer = 0;
	rs->bufend = 0;
	rs->this_frame = 0;
	rs->next_frame = 0;
	rs->pos = 0;
	rs->mh->rdat.skip = 0;
	rs->mh->rdat.advance_this_frame = FALSE;

	rs->error = MPG123_OK;
}

/*----------------------------------------------------------------------------*/

int mpgraw_next(
	mpgraw_state* rs,
	int flags )
{
	mpg123_handle* mh = rs->mh;

	rs->error = MPG123_OK;

	do
	{
		/* Handle skip */
		if( mh->rdat.skip > 0 )
		{
			ssize_t needed = mh->rdat.skip;
			ssize_t available = mh->rdat.rs->bufend - mh->rdat.rs->next_frame;

			if( available == needed )
			{
				available = needed;
			}

			if( available > needed )
			{
				mh->rdat.rs->next_frame += needed;
				mh->rdat.rs->pos += needed;
				mh->rdat.skip = 0;
				mh->rdat.rs->this_frame = mh->rdat.rs->next_frame;
			}
			else
			{
				mh->rdat.rs->next_frame += available;
				mh->rdat.rs->pos += available;
				mh->rdat.skip = needed - available;
				rs->error = MPG123_NEED_MORE;
			}
		}

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

/*******************************************************************************
 *
 * RAW API (reader)
 *
 ******************************************************************************/

int raw_init(mpg123_handle *fr)
{
	fr->rdat.rs->buffer = 0;
	fr->rdat.rs->bufend = 0;
	fr->rdat.rs->next_frame = 0;
	fr->rdat.rs->pos = 0;
	fr->rdat.skip = 0;
	/* VFALCO: hack to make sure we never see MPG123_DONE */
	fr->rdat.filepos = 0;
	fr->rdat.filelen = 1;
/*	fr->rdat.flags |= READER_BUFFERED; */
	return 0;
}

/*----------------------------------------------------------------------------*/

ssize_t raw_read(mpg123_handle *fr, unsigned char *out, ssize_t count)
{
	ssize_t gotcount = fr->rdat.rs->bufend - fr->rdat.rs->next_frame;

	if( fr->rdat.advance_this_frame )
	{
		fr->rdat.rs->this_frame = fr->rdat.rs->next_frame;
		fr->rdat.advance_this_frame = FALSE;
	}

	if( gotcount >= count )
	{
		gotcount = count;
		memcpy( out, fr->rdat.rs->next_frame, count );
		fr->rdat.rs->next_frame += count;
		fr->rdat.rs->pos += count;
	}
	else
	{
		/* rewind */
		fr->rdat.rs->pos -= fr->rdat.rs->next_frame - fr->rdat.rs->this_frame;
		fr->rdat.rs->next_frame = fr->rdat.rs->this_frame;
		gotcount = MPG123_NEED_MORE;
	}
	return gotcount;
}

/*----------------------------------------------------------------------------*/

void raw_close(mpg123_handle *fr)
{
}

/*----------------------------------------------------------------------------*/

off_t raw_tell(mpg123_handle *fr)
{
	/* this returns the offset from the beginning of the
	 * buffer rather than the global position within the input */
	return fr->rdat.rs->pos;
}

/*----------------------------------------------------------------------------*/

/* This does not (fully) work for non-seekable streams... You have to check for that flag, pal! */
void raw_rewind(mpg123_handle *fr)
{
	/* can't work for raw */
}

/*----------------------------------------------------------------------------*/

/* returns reached position... negative ones are bad... */
off_t raw_skip_bytes(mpg123_handle *fr,off_t len)
{
	ssize_t avail = fr->rdat.rs->bufend - fr->rdat.rs->next_frame;

	if( fr->rdat.advance_this_frame )
	{
		fr->rdat.rs->this_frame = fr->rdat.rs->next_frame;
		fr->rdat.advance_this_frame = FALSE;
	}

	if( len < avail )
	{
		fr->rdat.rs->next_frame += len;
		fr->rdat.rs->pos += len;
		return fr->rdat.rs->pos;
	}
	else
	{
		fr->rdat.rs->next_frame += avail;
		fr->rdat.rs->pos += avail;
		fr->rdat.skip = len - avail;
		return READER_MORE;
	}
}

/*----------------------------------------------------------------------------*/

int raw_back_bytes(mpg123_handle *fr, off_t bytes)
{
	if( fr->rdat.advance_this_frame )
	{
		fr->rdat.rs->this_frame = fr->rdat.rs->next_frame;
		fr->rdat.advance_this_frame = FALSE;
	}

	if( bytes>=0 )
	{
		/* Which one is right? */
		ssize_t avail=fr->rdat.rs->next_frame-fr->rdat.rs->buffer;
		/*ssize_t avail=fr->rdat.rs->next_frame-fr->rdat.rs->this_frame; */
		if( bytes<=avail )
		{
			fr->rdat.rs->next_frame-=bytes;
			fr->rdat.rs->pos-=bytes;
			return fr->rdat.rs->pos;
		}
		else
		{
			return READER_ERROR;
		}
	}
	else
	{
		return raw_skip_bytes(fr, -bytes) >= 0 ? 0 : READER_ERROR;
	}
}

/*----------------------------------------------------------------------------*/

int raw_seek_frame(mpg123_handle *fr, off_t num)
{
	return READER_ERROR;
}

/*----------------------------------------------------------------------------*/

void raw_forget(mpg123_handle *fr)
{
	fr->rdat.advance_this_frame = TRUE;
}
