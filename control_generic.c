
/*
 * Control interface to generic front ends.
 * written/copyrights 1997/99 by Andreas Neuhaus (and Michael Hipp)
 *
 * command parsing/processing reworked and partially rewritten in 2004/5 by Thomas Orgis
 * ...and at bit of bugfixing...
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>

#include "mpg123.h"

#define MODE_STOPPED 0
#define MODE_PLAYING 1
#define MODE_PAUSED 2

extern struct audio_info_struct ai;
extern int buffer_pid;
extern int tabsel_123[2][3][16];

FILE *outstream;

void generic_sendmsg (char *fmt, ...)
{
	va_list ap;
	fprintf(outstream, "@");
	va_start(ap, fmt);
	vfprintf(outstream, fmt, ap);
	va_end(ap);
	fprintf(outstream, "\n");
}

static double compute_bpf (struct frame *fr)
{
	double bpf;
	switch(fr->lay) {
		case 1:
			bpf = tabsel_123[fr->lsf][0][fr->bitrate_index];
			bpf *= 12000.0 * 4.0;
			bpf /= freqs[fr->sampling_frequency] << (fr->lsf);
			break;
		case 2:
		case 3:
			bpf = tabsel_123[fr->lsf][fr->lay-1][fr->bitrate_index];
			bpf *= 144000;
			bpf /= freqs[fr->sampling_frequency] << (fr->lsf);
			break;
		default:
			bpf = 1.0;
	}
	return bpf;
}

static double compute_tpf (struct frame *fr)
{
	static int bs[4] = { 0, 384, 1152, 1152 };
	double tpf;

	tpf = (double) bs[fr->lay];
	tpf /= freqs[fr->sampling_frequency] << (fr->lsf);
	return tpf;
}

void generic_sendstat (struct frame *fr, int no)
{
	long buffsize;
	double bpf, tpf, tim1, tim2;
	double dt = 0;
	int sno, rno;

	/* this and the 2 above functions are taken from common.c.
	/ maybe the compute_* functions shouldn't be static there
	/ so that they can also used here (performance problems?).
	/ isn't there an easier way to compute the time? */

	buffsize = xfermem_get_usedspace(buffermem);
	if (!rd || !fr)
		return;
	bpf = compute_bpf(fr);
	tpf = compute_tpf(fr);
	if (buffsize > 0 && ai.rate > 0 && ai.channels > 0) {
		dt = (double) buffsize / ai.rate / ai.channels;
		if ((ai.format & AUDIO_FORMAT_MASK) == AUDIO_FORMAT_16)
			dt *= .5;
	}
	rno = 0;
	sno = no;
	if (rd->filelen >= 0) {
		long t = rd->tell(rd);
		rno = (int)((double)(rd->filelen-t)/bpf);
		sno = (int)((double)t/bpf);
	}
	tim1 = sno * tpf - dt;
	tim2 = rno * tpf + dt;

	generic_sendmsg("F %d %d %3.2f %3.2f", sno, rno, tim1, tim2);
}

extern char *genre_table[];
extern int genre_count;
void generic_sendinfoid3 (char *buf)
{
	char info[200] = "", *c;
	int i;
	unsigned char genre;
	for (i=0, c=buf+3; i<124; i++, c++)
		info[i] = *c ? *c : ' ';
	info[i] = 0;
	genre = *c;
	generic_sendmsg("I ID3:%s%s", info, (genre<=genre_count) ? genre_table[genre] : "Unknown");
}

void generic_sendinfo (char *filename)
{
	char *s, *t;
	s = strrchr(filename, '/');
	if (!s)
		s = filename;
	else
		s++;
	t = strrchr(s, '.');
	if (t)
		*t = 0;
	generic_sendmsg("I %s", s);
}

void control_generic (struct frame *fr)
{
	struct timeval tv;
	fd_set fds;
	int n;
	int mode = MODE_STOPPED;
	int init = 0;
	int framecnt = 0;

	/* ThOr */
	char alive = 1;
	char silent = 0;
	
	/* responses to stderr for frontends needing audio data from stdout */
	if (param.remote_err)
 		outstream = stderr;
 	else
 		outstream = stdout;
 		
 	setlinebuf(outstream);
	/* the command behaviour is different, so is the ID */
	fprintf(outstream, "@R MPG123 (ThOr)\n");

	while (alive) {
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);

		/* play frame if no command needs to be processed */
		if (mode == MODE_PLAYING) {
			n = select(32, &fds, NULL, NULL, &tv);
			if (n == 0) {
				if (!read_frame(fr)) {
					mode = MODE_STOPPED;
					audio_flush(param.outmode, &ai);
					rd->close(rd);
					generic_sendmsg("P 0");
					continue;
				}
				play_frame(init,fr);
				if (init) {
					 static char *modes[4] = {"Stereo", "Joint-Stereo", "Dual-Channel", "Single-Channel"};
					/* JMG */
					generic_sendmsg("S %s %d %ld %s %d %d %d %d %d %d %d %d",
						fr->mpeg25 ? "2.5" : (fr->lsf ? "2.0" : "1.0"),
						fr->lay,
						freqs[fr->sampling_frequency],
						modes[fr->mode],
						fr->mode_ext,
						fr->framesize+4,
						fr->stereo,
						fr->copyright ? 1 : 0,
						fr->error_protection ? 1 : 0,
						fr->emphasis,
						tabsel_123[fr->lsf][fr->lay-1][fr->bitrate_index],
						fr->extension);
					generic_sendmsg("%f", equalizer[0][0]);
					init = 0;
				}
				++framecnt;
				if(silent == 0) generic_sendstat(fr, framecnt);
			}
		}
		else {
			/* wait for command */
			while (1) {
				n = select(32, &fds, NULL, NULL, NULL);
				if (n > 0)
					break;
			}
		}

		/* exit on error */
		if (n < 0) {
			generic_sendmsg("E Error waiting for command: %s\n", strerror(errno));
			exit(1);
		}

		/* process command */
		if (n > 0) {
	
			short int len = 1; /* length of buffer */
			short int cnum = 0; /* number of commands */ 
			short int cind = 0; /* index for commands */
			char *cmd, *comstr, *arg; /* variables for parsing, */
			char buf[REMOTE_BUFFER_SIZE];
			char **coms; /* list of commands */
			short int counter;
			coms = malloc(sizeof(*coms)); /* malloc magic */
			coms[0] = &buf[0]; /* first command string */
			
			/* read as much as possible, maybe multiple commands */
			/* the break here means: go on with playing next frame */
			/* ...hoping that it won't happen next time */
			if((len = read(STDIN_FILENO, buf, REMOTE_BUFFER_SIZE)) < 1)	break;
			
			/* one command on a line - separation by \n -> C strings in a row */
			for(counter = 0; counter < len; ++counter) {
				if(buf[counter] == '\n') { /* line end is command end */
					buf[counter] = 0; /* now it's a properly ending C string */
					 /* next char is first of next command (if there) */
					if(counter < (len - 1)) coms[++cind] = &buf[counter+1];
				}
			}
			cnum = cind+1;

			/*
			   when last command had no \n... should I discard it?
			   Ideally, I should remember the part and wait for next
				 read() to get the rest up to a \n. But that can go
				 to infinity. Too long commands too quickly are just
				 bad. Cannot/Won't change that. So, discard the unfinished
				 command and have fingers crossed that the rest of this
				 unfinished one qualifies as "unknown". 
			*/
			if(buf[len-1] != 0){
				char lasti = buf[len-1];
				buf[len-1] = 0;
				generic_sendmsg("E Unfinished command: %s%c", coms[cind], lasti);
				--cnum;
			}
			
			for(cind = 0; cind < cnum; ++cind) {
				comstr = coms[cind];
				if(strlen(comstr) == 0) continue;

				/* PAUSE */
				if (!strcasecmp(comstr, "P") || !strcasecmp(comstr, "PAUSE")) {
					if (!(mode == MODE_STOPPED))
					{	
						if (mode == MODE_PLAYING) {
							mode = MODE_PAUSED;
							audio_flush(param.outmode, &ai);
							if (param.usebuffer)
								kill(buffer_pid, SIGSTOP);
							generic_sendmsg("P 1");
						} else {
							mode = MODE_PLAYING;
							if (param.usebuffer)
								kill(buffer_pid, SIGCONT);
							generic_sendmsg("P 2");
						}
					}
					continue;
				}

				/* STOP */
				if (!strcasecmp(comstr, "S") || !strcasecmp(comstr, "STOP")) {
					if (mode != MODE_STOPPED) {
						audio_flush(param.outmode, &ai);
						rd->close(rd);
						mode = MODE_STOPPED;
						generic_sendmsg("P 0");
					}
					continue;
				}

				/* SILENCE */
				if(!strcasecmp(comstr, "SILENCE")) {
					silent = 1;
					generic_sendmsg("silence");
					continue;
				}

				/* QUIT */
				if (!strcasecmp(comstr, "Q") || !strcasecmp(comstr, "QUIT")){
					alive = FALSE; continue;
				}

				/* commands with arguments */
				cmd = NULL;
				arg = NULL;
				cmd = strtok(comstr," \t"); /* get the main command */
				arg = strtok(NULL,""); /* get the args */

				if (cmd && strlen(cmd) && arg && strlen(arg))
				{
					/* Simple EQ: SEQ <BASS> <MID> <TREBLE>  */
					if (!strcasecmp(cmd, "S") || !strcasecmp(cmd, "SEQ")) {
						real b,m,t;
						int cn;
						equalfile = TRUE;
						if(sscanf(arg, "%f %f %f", &b, &m, &t) == 3){
							/* very raw line */
							if ((t >= 0) && (t <= 3))
							for(cn=0; cn < 1; ++cn)
							{
								equalizer[0][cn] = b;
								equalizer[1][cn] = b;
							}
							if ((m >= 0) && (m <= 3))
							for(cn=1; cn < 2; ++cn)
							{
								equalizer[0][cn] = m;
								equalizer[1][cn] = m;
							}
							if ((b >= 0) && (b <= 3))
							for(cn=2; cn < 32; ++cn)
							{
								equalizer[0][cn] = t;
								equalizer[1][cn] = t;
							}
							generic_sendmsg("bass: %f mid: %f treble: %f", b, m, t);
						}
						else generic_sendmsg("E invalid arguments for SEQ: %s", arg);
						continue;
					}

					/* Equalizer control :) (JMG) */
					if (!strcasecmp(cmd, "E") || !strcasecmp(cmd, "EQ")) {
						float e;
						int c, v;
						equalfile = TRUE;
						/*generic_sendmsg("%s",updown);*/
						if( sscanf(arg, "%i %i %f", &c, &v, &e) == 3){
							equalizer[c][v] = e;
							generic_sendmsg("%i : %i : %f", c, v, e);
						}
						else generic_sendmsg("E invalid arguments for EQ: %s", arg);
						continue;
					}

					/* JUMP */
					if (!strcasecmp(cmd, "J") || !strcasecmp(cmd, "JUMP")) {
						char *spos;
						int pos, ok;

						spos = arg;
						if (!spos)
							continue;
						if (spos[0] == '-')
							pos = framecnt + atoi(spos);
						else if (spos[0] == '+')
							pos = framecnt + atoi(spos+1);
						else
							pos = atoi(spos);

						if (mode == MODE_STOPPED)
							continue;
						ok = 1;
						if (pos < framecnt) {
							rd->rewind(rd);
							read_frame_init();
							for (framecnt=0; ok && framecnt<pos; framecnt++) {
								ok = read_frame(fr);
								if (fr->lay == 3)
									set_pointer(512);
							}
						} else {
							for (; ok && framecnt<pos; framecnt++) {
								ok = read_frame(fr);
								if (fr->lay == 3)
									set_pointer(512);
							}
						}
						generic_sendmsg("J %d", framecnt);
						continue;
					}

					/* LOAD - actually play */
					if (!strcasecmp(cmd, "L") || !strcasecmp(cmd, "LOAD")) {
						int erg = 0;
						audio_flush(param.outmode, &ai);
						if (mode != MODE_STOPPED) {
							rd->close(rd);
							mode = MODE_STOPPED;
						}
						if( (erg = open_stream_control(arg, -1)) < 0 ){
							generic_sendmsg("E Error %i opening stream: %s", -1*erg, arg);
							generic_sendmsg("P 0");
							continue;
						}
						if (rd && rd->flags & READER_ID3TAG)
							generic_sendinfoid3((char *)rd->id3buf);
						else
							generic_sendinfo(arg);
						mode = MODE_PLAYING;
						init = 1;
						framecnt = 0;
						read_frame_init();
						generic_sendmsg("P 2");
						continue;
					}

					/* no command matched */
					generic_sendmsg("E Unknown command: %s", cmd); /* unknown command */
				} /* end commands with arguments */
				else
				{
					generic_sendmsg("E Unknown command or no arguments: %s", comstr); /* unknown command */
				}

			} /*end command processing loop */

			free(coms); /* release memory of command string (pointer) array */
				
		} /* end command reading & processing */

	} /* end main (alive) loop */

	/* quit gracefully */
	if (param.usebuffer) {
		kill(buffer_pid, SIGINT);
		xfermem_done_writer(buffermem);
		waitpid(buffer_pid, NULL, 0);
		xfermem_done(buffermem);
	} else {
		audio_flush(param.outmode, &ai);
		free(pcm_sample);
	}
	if (param.outmode == DECODE_AUDIO)
		audio_close(&ai);
	if (param.outmode == DECODE_WAV)
		wav_close();
	exit(0);
}

/* EOF */
