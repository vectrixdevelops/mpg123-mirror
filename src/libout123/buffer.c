/*
	buffer.c: output buffer

	copyright 1997-2015 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Oliver Fromme

	I (ThOr) am reviewing this file at about the same daytime as Oliver's timestamp here:
	Mon Apr 14 03:53:18 MET DST 1997
	- dammed night coders;-)

	This has been heavily reworked to be barely recognizable for the creation of
	libout123. There is more structure in the communication, as is necessary if
	the libout123 functionality is offered via some API to unknown client
	programs instead of being used from mpg123 alone. The basic idea is the same,
	the xfermem part only sligthly modified for more synchronization, as I sensed
	potential deadlocks. --ThOr
*/

/*
	Communication to the buffer is normally via xfermem_putcmd() and blocking
	on a response, relying on the buffer process periodically checking for
	pending commands.

	For more immediate concerns, you can send SIGINT. The only result is that this
	interrupts a current device writing operation and causes the buffer to wait
	for a following command.
*/

#include "buffer.h"
#include "out123_int.h"
#include "xfermem.h"
#include <errno.h>
#include "debug.h"
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#define BUF_CMD_OPEN     XF_CMD_CUSTOM1
#define BUF_CMD_CLOSE    XF_CMD_CUSTOM2
#define BUF_CMD_START    XF_CMD_CUSTOM3
#define BUF_CMD_STOP     XF_CMD_CUSTOM4
#define BUF_CMD_AUDIOCAP XF_CMD_CUSTOM5
#define BUF_CMD_PARAM    XF_CMD_CUSTOM6

/* TODO: Dynamically allocate that to allow multiple instances. */
int outburst = 32768;

/* This is static and global for the forked buffer process.
   Another forked buffer process will have its on value. */
static int intflag = FALSE;

static void catch_interrupt (void)
{
	intflag = TRUE;
}

static int write_string(audio_output_t *ao, int who, const char *buf);
static int read_string(audio_output_t *ao, int who, char **buf);
static int buffer_loop(audio_output_t *ao);

static void catch_child(void)
{
	/* Disabled for now. We do not really need that.
	   Rather get return status in a controlled way in buffer_exit(). */
	/* while (waitpid(-1, NULL, WNOHANG) > 0); */
}

/*
	Functions called from the controlling process.
*/

/* Start a buffer process. */
int buffer_init(audio_output_t *ao, size_t bytes)
{
	buffer_exit(ao);
	if(bytes < outburst) bytes = 2*outburst;

	xfermem_init(&ao->buffermem, bytes, 0, 0);
	/* Is catch_child() really useful? buffer_exit() does waitpid().
	   And if buffer_exit() is not called, the main process might be
	   killed off and not be able to run a signal handler anyway. */
	catchsignal(SIGCHLD, catch_child);
	switch((ao->buffer_pid = fork()))
	{
		case -1: /* error */
			error("cannot fork!");
			goto buffer_init_bad;
		case 0: /* child */
		{
			int ret;
			/*
				Ensure the normal default value for buffer_pid to be able
				to call normal out123 routines from the buffer proess.
				One could keep it at zero and even use this for detecting the
				buffer process and do special stuff for that. But the point
				is that there shouldn't be special stuff.
			*/
			ao->buffer_pid = -1;
			/* Not preparing audio output anymore, that comes later. */
			xfermem_init_reader(ao->buffermem);
			ret = buffer_loop(ao); /* Here the work happens. */
			xfermem_done_reader(ao->buffermem);
			xfermem_done(ao->buffermem);
			/* Proper cleanup of output handle, including out123_close(). */
			out123_del(ao);
			exit(ret);
		}
		default: /* parent */
		{
			int cmd;
			xfermem_init_writer(ao->buffermem);
			debug("waiting for inital pong from buffer process");
			if( (cmd=xfermem_getcmd(ao->buffermem->fd[XF_WRITER], TRUE))
			    != XF_CMD_PONG )
			{
				if(!AOQUIET)
					error2("Got %i instead of expected initial response %i. Killing rogue buffer process."
					,	cmd, XF_CMD_PONG);
				kill(ao->buffer_pid, SIGKILL);
				buffer_exit(ao);
				return -1;
			}
		}
	}

	return 0;
buffer_init_bad:
	if(ao->buffermem)
	{
		xfermem_done(ao->buffermem);
		ao->buffermem = NULL;
	}
	return -1;
}

/* End a buffer process. */
void buffer_exit(audio_output_t *ao)
{
	int status = 0;
	if(ao->buffer_pid == -1) return;

	debug("ending buffer");
	buffer_stop(ao); /* Puts buffer into waiting-for-command mode. */
	buffer_end(ao);  /* Gives command to end operation. */
	xfermem_done_writer(ao->buffermem);
	waitpid(ao->buffer_pid, &status, 0);
	xfermem_done(ao->buffermem);
	ao->buffermem = NULL;
	ao->buffer_pid = -1;
	if(WIFEXITED(status))
	{
		int ret = WEXITSTATUS(status);
		if(ret && !AOQUIET)
			error1("Buffer process isses arose, non-zero return value %i.", ret);
	}
	else if(!AOQUIET)
		error("Buffer process did not exit normally.");
}

/*
	Communication from writer to reader (buffer process).
	Remember: The ao struct here is the writer's instance.
*/

static int buffer_cmd_finish(audio_output_t *ao)
{
	/* Only if buffer returns XF_CMD_OK we got lucky. Otherwise, we expect
	   the buffer to deliver a reason right after XF_CMD_ERROR. */
	switch(xfermem_getcmd(ao->buffermem->fd[XF_WRITER], TRUE))
	{
		case XF_CMD_OK: return 0;
		case XF_CMD_ERROR:
			if(!GOOD_READVAL(ao->buffermem->fd[XF_WRITER], ao->errcode))
				ao->errcode = OUT123_BUFFER_ERROR;
			return -1;
		break;
		default:
			ao->errcode = OUT123_BUFFER_ERROR;
			return -1;
	}
}

int buffer_sync_param(audio_output_t *ao)
{
	int writerfd = ao->buffermem->fd[XF_WRITER];
	if(xfermem_putcmd(writerfd, BUF_CMD_PARAM) != 1)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}
	/* Calling an external serialization routine to avoid forgetting
	   any fresh parameters here. */
	if(write_parameters(ao, writerfd))
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}
	return buffer_cmd_finish(ao);
}

int buffer_open(audio_output_t *ao, const char* driver, const char* device)
{
	int writerfd = ao->buffermem->fd[XF_WRITER];

	if(xfermem_putcmd(writerfd, BUF_CMD_OPEN) != 1)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}
	/* Passing over driver and device name. */
	if(  write_string(ao, XF_WRITER, driver)
	  || write_string(ao, XF_WRITER, device) )
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}

	if(buffer_cmd_finish(ao) == 0)
	{
		/* Retrieve driver and device name. */
		if(  read_string(ao, XF_WRITER, &ao->driver)
		  || read_string(ao, XF_WRITER, &ao->device) )
		{
			ao->errcode = OUT123_BUFFER_ERROR;
			return -1;
		}
	}
	return 0;
}

int buffer_encodings(audio_output_t *ao)
{
	int writerfd = ao->buffermem->fd[XF_WRITER];

	if(xfermem_putcmd(writerfd, BUF_CMD_AUDIOCAP) != 1)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}
	/* Now shoving over the parameters for opening the device. */
	if(
		!GOOD_WRITEVAL(writerfd, ao->channels)
	||	!GOOD_WRITEVAL(writerfd, ao->rate)
	)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}

	if(buffer_cmd_finish(ao) == 0)
	{
		int encodings;
		/* If all good, the answer can be read how. */
		if(!GOOD_READVAL(writerfd, encodings))
		{
			ao->errcode = OUT123_BUFFER_ERROR;
			return -1;
		}
		else return encodings;
	}
	else return -1;
}

int buffer_start(audio_output_t *ao)
{
	int writerfd = ao->buffermem->fd[XF_WRITER];
	if(xfermem_putcmd(writerfd, BUF_CMD_START) != 1)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}
	/* Now shoving over the parameters for opening the device. */
	if(
		!GOOD_WRITEVAL(writerfd, ao->format)
	||	!GOOD_WRITEVAL(writerfd, ao->channels)
	|| !GOOD_WRITEVAL(writerfd, ao->rate)
	)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}

	return buffer_cmd_finish(ao);
}

#define BUFFER_SIMPLE_CONTROL(name, cmd) \
void name(audio_output_t *ao) \
{ \
	xfermem_putcmd(ao->buffermem->fd[XF_WRITER], cmd); \
	xfermem_getcmd(ao->buffermem->fd[XF_WRITER], TRUE); \
}

BUFFER_SIMPLE_CONTROL(buffer_stop,  BUF_CMD_STOP)
BUFFER_SIMPLE_CONTROL(buffer_continue, XF_CMD_CONTINUE)
BUFFER_SIMPLE_CONTROL(buffer_ignore_lowmem, XF_CMD_IGNLOW)
BUFFER_SIMPLE_CONTROL(buffer_drain, XF_CMD_DRAIN)
BUFFER_SIMPLE_CONTROL(buffer_end, XF_CMD_TERMINATE)
BUFFER_SIMPLE_CONTROL(buffer_close, BUF_CMD_CLOSE)

#define BUFFER_SIGNAL_CONTROL(name, cmd) \
void name(audio_output_t *ao) \
{ \
	kill(ao->buffer_pid, SIGINT); \
	xfermem_putcmd(ao->buffermem->fd[XF_WRITER], cmd); \
	xfermem_getcmd(ao->buffermem->fd[XF_WRITER], TRUE); \
}

BUFFER_SIGNAL_CONTROL(buffer_pause, XF_CMD_PAUSE)
BUFFER_SIGNAL_CONTROL(buffer_drop, XF_CMD_DROP)

long buffer_fill(audio_output_t *ao)
{
	return xfermem_get_usedspace(ao->buffermem);
}

/* The workhorse: Send data to the buffer with some synchronization and even
   error checking. */
size_t buffer_write(audio_output_t *ao, void *buffer, size_t bytes)
{
	/*
		Writing the whole buffer in one piece is no good as that means
		waiting for the buffer being empty. That is called a buffer underrun.
		We want to refill the buffer before that happens. So, what is sane?
	*/
	size_t written = 0;
	size_t max_piece = ao->buffermem->size / 2;
	while(bytes)
	{
		size_t count_piece = bytes > max_piece
		?	max_piece
		:	bytes;
		int ret = xfermem_write(ao->buffermem
		,	(char*)buffer+written, count_piece);
		if(ret)
		{
			if(!AOQUIET)
				error("writing to buffer memory failed");
			if(ret == XF_CMD_ERROR)
			{
				/* Buffer tells me that it has an error waiting. */
				if(!GOOD_READVAL(ao->buffermem->fd[XF_WRITER], ao->errcode))
					ao->errcode = OUT123_BUFFER_ERROR;
			}
			return 0;
		}
		bytes   -= count_piece;
		written += count_piece;
	}
	return written;
}


/*
	Code for the buffer process itself.
*/

/*

buffer loop:

{
	1. normal operation: get data, feed to audio device
	   (if device open and alive, if data there, if no other command pending)
	2. command response: pause/unpause, open module/device, query caps

	One command at a time, synchronized ... writer process blocks, waiting for
	response.
}

*/

/*
	Fill buffer to that value when starting playback from stopped state or after
	experiencing a serious underrun.
	One might also define intermediate preload to recover from underruns. Earlier
	code used 1/8 of the buffer.
*/
static size_t preload_size(audio_output_t *ao)
{
	size_t preload = 0;
	txfermem *xf = ao->buffermem;
	/* Fill configured part of buffer on first run before starting to play.
	 * Live mp3 streams constantly approach buffer underrun otherwise. [dk]
	 */
	if(ao->preload > 0.)   preload = (size_t)(ao->preload*xf->size);
	if(preload > xf->size) preload = xf->size;

	return preload;
}

/* Play one piece of audio from the buffer after settling preload etc.
   On error, the device is closed and this naturally stops playback
   as that depends on ao->state == play_live.
   This plays _at_ _most_ the given amount of bytes, usually less. */
static void buffer_play(audio_output_t *ao, size_t bytes)
{
	int written;
	txfermem *xf = ao->buffermem;

	/* Settle amount of bytes accessible in one block. */
	if (bytes > xf->size - xf->readindex)
		bytes = xf->size - xf->readindex;
	/* Not more than configured output block. */
	if (bytes > outburst)
		bytes = outburst;
	/* The output can only take multiples of framesize. */
	bytes -= bytes % ao->framesize;
	/* Now do a normal ao->write(), with interruptions by signals
		being expected. */
	written = ao->write(ao, (unsigned char*)xf->data+xf->readindex, (int)bytes);
	if(written >= 0)
		/* Advance read pointer by the amount of written bytes. */
		xf->readindex = (xf->readindex + written) % xf->size;
	else
	{
		ao->errcode = OUT123_DEV_PLAY;
		if(!(ao->flags & OUT123_QUIET))
			error1("Error in writing audio (%s?)!", strerror(errno));
		out123_close(ao);
	}
}

/* Now I'm getting really paranoid: Helper to skip bytes from command
   channel if we cannot allocate enough memory to hold the data. */
static void skip_bytes(int fd, size_t count)
{
	while(count)
	{
		char buf[1024];
		if(!unintr_read(fd, buf, (count < sizeof(buf) ? count : sizeof(buf))))
			return;
	}
}

/* Write a string to command channel.
   Return 0 on success, set ao->errcode on issues. */
static int write_string(audio_output_t *ao, int who, const char *buf)
{
	txfermem *xf = ao->buffermem;
	int my_fd = xf->fd[who];
	size_t len;

	/* A NULL string is passed als zero bytes. */
	len = buf ? (strlen(buf)+1) : 0;
	if( !GOOD_WRITEVAL(my_fd, len)
	 || !GOOD_WRITEBUF(my_fd, buf, len) )
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return 2;
	}
	return 0;
}

/* Read a string from command channel.
   Return 0 on success, set ao->errcode on issues. */
static int read_string(audio_output_t *ao, int who, char **buf)
{
	txfermem *xf = ao->buffermem;
	int my_fd = xf->fd[who];
	size_t len;

	if(*buf)
		free(*buf);
	*buf = NULL;

	if(!GOOD_READVAL(my_fd, len))
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return 2;
	}
	/* If there is an insane length of given, that shall be handled. */
	if(len && !(*buf = malloc(len)))
	{
		ao->errcode = OUT123_DOOM;
		skip_bytes(my_fd, len);
		return -1;
	}
	if(!GOOD_READBUF(my_fd, *buf, len))
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return 2;
	}
	return 0;
}

/* The main loop, returns 0 when no issue occured. */
int buffer_loop(audio_output_t *ao)
{
	txfermem *xf = ao->buffermem;
	int my_fd = xf->fd[XF_READER];
	int preloading = FALSE;

	/* Be prepared to use SIGINT for communication. */
	catchsignal (SIGINT, catch_interrupt);
	/* sigprocmask (SIG_SETMASK, oldsigset, NULL); */
	/* Say hello to the writer. */
	xfermem_putcmd(my_fd, XF_CMD_PONG);

	while(1)
	{
		int cmd;
		/* If a device is opened and playing, it is our first duty to keep it playing. */
		if(ao->state == play_live)
		{
			size_t bytes = xfermem_get_usedspace(xf);
			if(preloading)
				preloading = (bytes < preload_size(ao));
			if(!preloading)
			{
				if(bytes < outburst)
					preloading = TRUE;
				else
					buffer_play(ao, bytes);
			}
		}
		/* Now always check for a pending command, in a blocking way if there is
		   no playback. */
		cmd = xfermem_getcmd(my_fd, (intflag || (ao->state != play_live)));
		/* The writer only ever signals before sending a command and also waiting for
		   a response. So, this is the right place to clear the flag, before giving
		   that response */
		intflag = FALSE;
		/* These actions should rely heavily on calling the normal out123
		   API functions, just with some parameter passing and error checking
		   wrapped around. If there is much code here, it is wrong. */
		switch(cmd)
		{
			case 0:
				debug("no command pending");
			break;
			case XF_CMD_PING:
				/* Expecting ping-pong only while playing! Otherwise, the writer
				   could get stuck waiting for free space forever. */
				if(!(ao->state == play_live))
					xfermem_putcmd(my_fd, XF_CMD_PONG);
				else
				{
					xfermem_putcmd(my_fd, XF_CMD_ERROR);
					if(ao->errcode == OUT123_OK)
						ao->errcode = OUT123_NOT_LIVE;
					if(!GOOD_WRITEVAL(my_fd, ao->errcode))
						return 2;
				}
			break;
			case BUF_CMD_PARAM:
				/* If that does not work, communication is broken anyway and
				   writer will notice soon enough. */
				read_parameters(ao, my_fd);
				xfermem_putcmd(my_fd, XF_CMD_OK);
			break;
			case BUF_CMD_OPEN:
			{
				char *driver  = NULL;
				char *device  = NULL;
				int success;
				success = (
					!read_string(ao, XF_READER, &driver)
				&&	!read_string(ao, XF_READER, &device)
				&&	!out123_open(ao, driver, device)
				);
				free(device);
				free(driver);
				if(success)
				{
					xfermem_putcmd(my_fd, XF_CMD_OK);
					if(  write_string(ao, XF_READER, ao->driver)
					  || write_string(ao, XF_READER, ao->device) )
						return 2;
				}
				else
				{
					xfermem_putcmd(my_fd, XF_CMD_ERROR);
					/* Again, no sense to bitch around about communication errors,
					   just quit. */
					if(!GOOD_WRITEVAL(my_fd, ao->errcode))
						return 2;
				}
			}
			break;
			case BUF_CMD_CLOSE:
				out123_close(ao);
				xfermem_putcmd(my_fd, XF_CMD_OK);
			break;
			case BUF_CMD_AUDIOCAP:
			{
				int encodings;
				if(
					!GOOD_READVAL(my_fd, ao->channels)
				||	!GOOD_READVAL(my_fd, ao->rate)
				)
					return 2;
				encodings = out123_encodings(ao, ao->channels, ao->rate);
				if(encodings >= 0)
				{
					xfermem_putcmd(my_fd, XF_CMD_OK);
					if(!GOOD_WRITEVAL(my_fd, encodings))
						return 2;
				}
				else
				{
					xfermem_putcmd(my_fd, XF_CMD_ERROR);
					if(!GOOD_WRITEVAL(my_fd, ao->errcode))
						return 2;
				}
			}
			break;
			case BUF_CMD_START:
				if(
					!GOOD_READVAL(my_fd, ao->format)
				||	!GOOD_READVAL(my_fd, ao->channels)
				||	!GOOD_READVAL(my_fd, ao->rate)
				)
					return 2;
				if(!out123_start(ao, ao->format, ao->channels, ao->rate))
				{
					preloading = TRUE;
					xfermem_putcmd(my_fd, XF_CMD_OK);
				}
				else
				{
					xfermem_putcmd(my_fd, XF_CMD_ERROR);
					if(!GOOD_WRITEVAL(my_fd, ao->errcode))
						return 2;
				}
			break;
			case BUF_CMD_STOP:
				if(ao->state == play_live)
				{ /* Drain is implied! */
					size_t bytes;
					while((bytes = xfermem_get_usedspace(xf)))
						buffer_play(ao, bytes);
				}
				out123_stop(ao);
				xfermem_putcmd(my_fd, XF_CMD_OK);
			break;
			case XF_CMD_CONTINUE:
				out123_continue(ao);
				preloading = TRUE;
				xfermem_putcmd(my_fd, XF_CMD_OK);
			break;
			case XF_CMD_IGNLOW:
				preloading = FALSE;
				xfermem_putcmd(my_fd, XF_CMD_OK);
			break;
			case XF_CMD_DRAIN:
				if(ao->state == play_live)
				{
					size_t bytes;
					while((bytes = xfermem_get_usedspace(xf)))
						buffer_play(ao, bytes);
					out123_drain(ao);
				}
				xfermem_putcmd(my_fd, XF_CMD_OK);
			break;
			case XF_CMD_TERMINATE:
				/* Will that response always reach the writer? Well, at worst,
				   it's an ignored error on xfermem_getcmd(). */
				xfermem_putcmd(my_fd, XF_CMD_OK);
				return 0;
			case XF_CMD_PAUSE:
				out123_pause(ao);
				xfermem_putcmd(my_fd, XF_CMD_OK);
			break;
			case XF_CMD_DROP:
				xf->readindex = xf->freeindex;
				out123_drop(ao);
			break;
			default:
				if(!AOQUIET)
				{
					if(cmd < 0)
						error1("Reading a command returned %i, my link is broken.", cmd);
					else
						error1("Unknown command %i encountered. Confused Suicide!", cmd);
				}
				return 1;
		}
	}
}
