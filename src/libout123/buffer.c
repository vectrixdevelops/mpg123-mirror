/*
	buffer.c: output buffer

	copyright 1997-2015 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Oliver Fromme

	I (ThOr) am reviewing this file at about the same daytime as Oliver's timestamp here:
	Mon Apr 14 03:53:18 MET DST 1997
	- dammed night coders;-)
*/

/*
	Communication to the buffer is normally via xfermem_putcmd() and blocking
	on a response, relying in the buffer process periodically checking for
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

/* TODO: Dynamically allocate that to allow multiple instances. */
int outburst = 32768;

/* Those are static and global for the forked buffer process.
   Another forked buffer process will have its on flags. */
static int intflag = FALSE;
static int usr1flag = FALSE;

static void catch_interrupt (void)
{
	intflag = TRUE;
}

/*
	Interfaces to writer process
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
			if(    unintr_read(writerfd, &ao->errcode, sizeof(ao->errcode))
			    != sizeof(ao->errcode) )
				ao->errcode = OUT123_BUFFER_ERROR;
			return -1;
		break;
		default:
			ao->errcode = OUT123_BUFFER_ERROR;
			return -1;
	}
}

int buffer_open(audio_output_t *ao)
{
	int writerfd = ao->buffermem->fd[XF_WRITER];
	size_t drvlen;
	size_t devlen;

	if(xfermem_putcmd(writerfd, BUF_CMD_OPEN) != 1)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}
	/* Passing over driver and device name. */
	drvlen = ao->driver ? (strlen(ao->driver) + 1) : 0;
	devlen = ao->device ? (strlen(ao->device) + 1) : 0;
	if(
	/* Size of string in memory, then string itself. */
		unintr_write(writerfd, &drvlen, sizeof(drvlen))
		!= sizeof(namelen)
	|| unintr_write(writerfd, ao->driver, drvlen)
		!= drvlen
	|| unintr_write(writerfd, &devlen, sizeof(devlen))
		!= sizeof(namelen)
	|| unintr_write(writerfd, ao->device, devlen)
		!= devlen
	)
	{
		ao->errocde = OUT123_BUFFER_ERROR;
		return -1;
	}

	return buffer_cmd_finish(ao);
}

int buffer_get_encodings(audio_output_t *ao)
{
	int writerfd = ao->buffermem->fd[XF_WRITER];

	if(xfermem_putcmd(writerfd, BUF_CMD_AUDIOCAP) != 1)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}
	/* Now shoving over the parameters for opening the device. */
	if(
		unintr_write(writerfd, &ao->encoding, sizeof(ao->encoding))
		!= sizeof(ao->encoding)
	||	unintr_write(writerfd, &ao->channels, sizeof(ao->channels))
		!= sizeof(ao->channels)
	||	unintr_write(writerfd, &ao->rate, sizeof(ao->rate))
		!= sizeof(ao->rate)
	)
	{
		ao->errocde = OUT123_BUFFER_ERROR;
		return -1;
	}

	if(buffer_cmd_finish(ao) == 0)
	{
		int encodings;
		/* If all good, the answer can be read how. */
		if(    unintr_read(writerfd, &encodings, sizeof(encodings))
			 != sizeof(encodings) )
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
	size_t namelen;
	if(xfermem_putcmd(writerfd, BUF_CMD_START) != 1)
	{
		ao->errcode = OUT123_BUFFER_ERROR;
		return -1;
	}
	/* Now shoving over the parameters for opening the device. */
	if(
		unintr_write(writerfd, &ao->encoding, sizeof(ao->encoding))
		!= sizeof(ao->encoding)
	||	unintr_write(writerfd, &ao->channels, sizeof(ao->channels))
		!= sizeof(ao->channels)
	||	unintr_write(writerfd, &ao->rate, sizeof(ao->rate))
		!= sizeof(ao->rate)
	)
	{
		ao->errocde = OUT123_BUFFER_ERROR;
		return -1;
	}

	return buffer_cmd_finish(ao);
}

#define BUFFER_SIMPLE_CONTROL(name, cmd) \
void name(audio_output_t *ao) \
{ \
	xfermem_putcmd(ao->buffermem->fd[XF_WRITER], cmd); \
	xfermem_getcmd(ao->buffermem->fd_XF_WRITER], TRUE); \
}

BUFFER_SIMPLE_CONTROL(buffer_stop,  BUF_CMD_STOP)
BUFFER_SIMPLE_CONTROL(buffer_continue, XF_CMD_CONTINUE)
BUFFER_SIMPLE_CONTROL(buffer_ignore_lowmem, XF_CMD_IGNLOW)
BUFFER_SIMPLE_CONTROL(buffer_drain, XF_CMD_DRAIN)
BUFFER_SIMPLE_CONTROL(buffer_end, XF_CMD_TERMINATE)

#define BUFFER_SIGNAL_CONTROL(name, cmd) \
void name(audio_output_t *ao) \
{ \
	kill(ao->buffer_pid, SIGINT); \
	xfermem_putcmd(ao->buffermem->fd[XF_WRITER], cmd); \
	xfermem_getcmd(ao->buffermem->fd_XF_WRITER], TRUE); \
}

BUFFER_SIGNAL_CONTROL(buffer_pause, XF_CMD_PAUSE)
BUFFER_SIGNAL_CONTROL(buffer_flush, XF_CMD_FLUSH)

size_t buffer_write(audio_outout_t *ao, unsigned char const * bytes, size_t count)
{
	/*
		Writing the whole buffer in one piece is no good as that means
		waiting for the buffer being empty. That is called a buffer underrun.
		We want to refill the buffer before that happens. So, what is sane?
	*/
	size_t written = 0;
	size_t max_piece = ao->buffermem->size / 2;
	while(count)
	{
		size_t count_piece = count > max_piece
		?	max_piece
		:	count;
		if(xfermem_write(buffermem, bytes+written, count_piece) != 0)
		{
			debug("writing to buffer memory failed");
			ao->errcode = OUT123_BUFFER_ERROR;
			return 0;
		}
		count   -= count_piece;
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
static size_t initial_preload(audio_output_t *ao)
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

void buffer_loop(audio_output_t *ao, sigset_t *oldsigset)
{
	int on_air = TRUE;
	size_t preload;
	txfermem *xf = ao->buffermem;
	int my_fd = xf->fd[XF_READER];
	int preloading = TRUE;

	/* Be prepared to use SIGINT and SIGUSR1 for communication. */
	catchsignal (SIGINT, catch_interrupt);
	sigprocmask (SIG_SETMASK, oldsigset, NULL);
	/* Say hello to the writer. */
	xfermem_putcmd(my_fd, XF_CMD_WAKEUP);

	preload = initial_preload(ao);
	while(on_air)
	{
		/* If a device is opened and playing, it is our first duty to keep it playing. */
		if(ao->state == play_live)
		{
			size_t bytes = xfermem_get_usedspace(xf);
			if(preloading)
			{
				if(!(preloading = (bytes < preload)))
					out123_continue(ao);
			}
			if(!preloading)
			{
				if(bytes < outburst)
				{
					/* underrun */
					out123_pause(ao);
					preloading = TRUE;
				}
				else
				{
					if (bytes > xf->size - xf->readindex)
						bytes = xf->size - xf->readindex;
					if (bytes > outburst)
						bytes = outburst;
					/* The output can only take multiples of framesize. */
					bytes -= bytes % ao->framesize;
Just ao->write(), possible interrupted by signal, or the old logic of flush_output, which
actually loops to avoid signal interruption?

				}
			}
		}
		/* */
	}
}

void buffer_loop(audio_output_t *ao, sigset_t *oldsigset)
{
	int bytes, outbytes;
	txfermem *xf = ao->buffermem;
	int my_fd = xf->fd[XF_READER];
	int done = FALSE;
	int preload;

	catchsignal (SIGINT, catch_interrupt);
	sigprocmask (SIG_SETMASK, oldsigset, NULL);

	xfermem_putcmd(my_fd, XF_CMD_WAKEUP);

	debug("audio output: waiting for cap requests");
	/* wait for audio setup queries */
	while(1)
	{
		int cmd;
		cmd = xfermem_block(XF_READER, xf);
		if(cmd == XF_CMD_AUDIOCAP)
		{
			ao->rate     = xf->rate;
			ao->channels = xf->channels;
			ao->format   = ao->get_formats(ao);
			debug3("formats for %liHz/%ich: 0x%x", ao->rate, ao->channels, ao->format);
			xf->format = ao->format;
			xfermem_putcmd(my_fd, XF_CMD_AUDIOCAP);
		}
		else if(cmd == XF_CMD_WAKEUP)
		{
			debug("got wakeup... leaving config mode");
			xfermem_putcmd(buffermem->fd[XF_READER], XF_CMD_WAKEUP);
			break;
		}
		else
		{
			error1("unexpected command %i", cmd);
			return;
		}
	}


int flag, usr1flag ... which one for generic communication?

	for (;;) {
		if (intflag) {
			debug("handle intflag... flushing");
			intflag = FALSE;
			ao->flush(ao);
			/* Either prepare for waiting or empty buffer now. */
			if(!xf->justwait) xf->readindex = xf->freeindex;
			else
			{
				int cmd;
				debug("Prepare for waiting; draining command queue. (There's a lot of wakeup commands pending, usually.)");
				do
				{
					cmd = xfermem_getcmd(my_fd, FALSE);
					/* debug1("drain: %i",  cmd); */
				} while(cmd > 0);
			}
			if(xf->wakeme[XF_WRITER]) xfermem_putcmd(my_fd, XF_CMD_WAKEUP);
		}
		if (usr1flag) {
			debug("handling usr1flag");
			usr1flag = FALSE;
			/*   close and re-open in order to flush
			 *   the device's internal buffer before
			 *   changing the sample rate.   [OF]
			 */
			/* writer must block when sending SIGUSR1
			 * or we will lose all data processed 
			 * in the meantime! [dk]
			 */
			xf->readindex = xf->freeindex;
			/* We've nailed down the new starting location -
			 * writer is now safe to go on. [dk]
			 */
			if (xf->wakeme[XF_WRITER])
				xfermem_putcmd(my_fd, XF_CMD_WAKEUP);
			ao->rate = xf->rate; 
			ao->channels = xf->channels; 
			ao->format = xf->format;
			if (reset_output(ao) < 0) {
				error1("failed to reset audio: %s", strerror(errno));
				exit(1);
			}
		}
		if ( (bytes = xfermem_get_usedspace(xf)) < outburst ) {
			/* if we got a buffer underrun we first
			 * fill 1/8 of the buffer before continue/start
			 * playing */
			if (preload < xf->size>>3)
				preload = xf->size>>3;
			if(preload < outburst)
				preload = outburst;
		}
		debug1("bytes: %i", bytes);
		if(xf->justwait || bytes < preload) {
			int cmd;
			if (done && !bytes) { 
				break;
			}
			
			if(xf->justwait || !done) {

				/* Don't spill into errno check below. */
				errno = 0;
				cmd = xfermem_block(XF_READER, xf);
				debug1("got %i", cmd);
				switch(cmd) {

					/* More input pending. */
					case XF_CMD_WAKEUP_INFO:
						continue;
					/* Yes, we know buffer is low but
					 * know we don't care.
					 */
					case XF_CMD_WAKEUP:
						break;	/* Proceed playing. */
					case XF_CMD_ABORT: /* Immediate end, discard buffer contents. */
						return; /* Cleanup happens outside of buffer_loop()*/
					case XF_CMD_TERMINATE: /* Graceful end, playing stuff in buffer and then return. */
						debug("going to terminate");
						done = TRUE;
						break;
					case XF_CMD_RESYNC:
						debug("ordered resync");
						if (param.outmode == DECODE_AUDIO) ao->flush(ao);

						xf->readindex = xf->freeindex;
						if (xf->wakeme[XF_WRITER]) xfermem_putcmd(my_fd, XF_CMD_WAKEUP);
						continue;
						break;
					case -1:
						if(intflag || usr1flag) /* Got signal, handle it at top of loop... */
						{
							debug("buffer interrupted");
							continue;
						}
						if(errno)
							error1("Yuck! Error in buffer handling... or somewhere unexpected: %s", strerror(errno));
						done = TRUE;
						xf->readindex = xf->freeindex;
						xfermem_putcmd(xf->fd[XF_READER], XF_CMD_TERMINATE);
						break;
					default:
						fprintf(stderr, "\nEh!? Received unknown command 0x%x in buffer process.\n", cmd);
				}
			}
		}
		/* Hack! The writer issues XF_CMD_WAKEUP when first adjust 
		 * audio settings. We do not want to lower the preload mark
		 * just yet!
		 */
		if (xf->justwait || !bytes)
			continue;
		preload = outburst; /* set preload to lower mark */
		if (bytes > xf->size - xf->readindex)
			bytes = xf->size - xf->readindex;
		if (bytes > outburst)
			bytes = outburst;

		/* The output can only take multiples of framesize. */
		bytes -= bytes % ao->framesize;

		debug("write");
		outbytes = flush_output(ao, (unsigned char*) xf->data + xf->readindex, bytes);

		if(outbytes < bytes)
		{
			if(outbytes < 0) outbytes = 0;
			if(!intflag && !usr1flag) {
				error1("Ouch ... error while writing audio data: %s", strerror(errno));
				/*
				 * done==TRUE tells writer process to stop
				 * sending data. There might be some latency
				 * involved when resetting readindex to 
				 * freeindex so we might need more than one
				 * cycle to terminate. (The number of cycles
				 * should be finite unless I managed to mess
				 * up something. ;-) [dk]
				 */
				done = TRUE;	
				xf->readindex = xf->freeindex;
				xfermem_putcmd(xf->fd[XF_READER], XF_CMD_TERMINATE);
			}
			else debug("buffer interrupted");
		}
		bytes = outbytes;

		xf->readindex = (xf->readindex + bytes) % xf->size;
		if (xf->wakeme[XF_WRITER])
			xfermem_putcmd(my_fd, XF_CMD_WAKEUP);
	}
}

static void catch_child(void)
{
  while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* Called from the controlling process. */

int buffer_init(audio_output_t *ao, size_t bytes)
{
	sigset_t newsigset, oldsigset;

	buffer_exit(ao);
	if(bytes < outburst) bytes = 2*outburst;

	xfermem_init (&ao->buffermem, bytes, 0, 0);
	catchsignal (SIGCHLD, catch_child);
	switch((ao->buffer_pid = fork()))
	{
		case -1: /* error */
			error("cannot fork!");
			goto buffer_init_bad;
		case 0: /* child */
		{
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
			buffer_loop(ao, &oldsigset); /* Here the work happens. */
			xfermem_done_reader(ao->buffermem);
			xfermem_done(ao->buffermem);
			exit(0);
		}
		default: /* parent */
			xfermem_init_writer(buffermem);
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

void buffer_exit(audio_output_t *ao)
{
	if(ao->buffer_pid == -1) return

	debug("ending buffer");
	buffer_stop(ao); /* Puts buffer into waiting-for-command mode. */
	buffer_end(ao, 1);  /* Gives command to end operation. */
	xfermem_done_writer(ao->buffermem);
	waitpid(ao->buffer_pid, NULL, 0);
	xfermem_done(ao->buffermem);
	ao->buffer_pid = -1;
	/* TODO: I sense that close(xf->fd[XF_WRITER]) is missing. */
}
