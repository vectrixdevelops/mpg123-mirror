/*
	raw_decoder: test program for libmpg123, using the raw api for decoding

	copyright 2011 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Patrick Dehne
*/

#include <mpg123.h>

/* unistd.h is not available under MSVC, 
 io.h defines the read and write functions */
#ifndef _MSC_VER
#include <unistd.h>
#else
#include <io.h>
#endif

#include <stdio.h>
#include <string.h>

#define INBUFF  16384

int main(int argc, char **argv)
{
	unsigned char buf[INBUFF];
	FILE *in;
	FILE *out;
	mpgraw_state s;
	int ret, rate, channels, enc, inc, outc, framec;
	off_t len;
	size_t left;
	int firstframefound, encoding;
	left = firstframefound = encoding = 0;

	inc=outc=framec=0;

	if(argc < 4)
	{
		fprintf(stderr,"Please supply one of encodings s8, s16, s32, f32, input and output filenames\n");
		return -1;
	}

	if(strcmp(argv[1], "s8") == 0)
		encoding = MPG123_ENC_SIGNED_8;
	else if(strcmp(argv[1], "s16") == 0)
		encoding = MPG123_ENC_SIGNED_16;
	else if(strcmp(argv[1], "s32") == 0)
		encoding = MPG123_ENC_SIGNED_32;
	else if(strcmp(argv[1], "f32") == 0)
		encoding = MPG123_ENC_FLOAT_32;
	else
	{
		fprintf(stderr,"Please supply one of encodings s8, s16, s32, f32\n");
		return -1;
	}

	in = fopen(argv[2], "rb");
	if(in == NULL)
	{
		fprintf(stderr, "Unable to open input file %s\n", argv[1]);
		return -1;
	}

	out = fopen(argv[3], "wb");
	if(out == NULL)
	{
		fprintf(stderr, "Unable to open output file %s\n", argv[2]);
		return -1;
	}

	mpg123_init();

	ret = mpgraw_open(&s, NULL, 0, MPG123_MONO | MPG123_STEREO, encoding);
	if(ret != MPG123_OK)
	{
		fprintf(stderr,"Unable to open stream: %s\n", mpg123_plain_strerror(ret));
		return -1;
	}
	
	while(1)
	{
		int ret = mpgraw_next(&s, rawFlagAudioFrame);
		if(ret == MPG123_OK||ret == MPG123_NEW_FORMAT)
		{
			if(!firstframefound)
			{
				firstframefound = 1;
				rate = s.rate;
				channels = s.channels;
				enc = s.encoding;
				fprintf(stderr, "New format: %li Hz, %i channels, encoding value %i\n", rate, channels, enc);
			}

			ret = mpgraw_decode(&s, NULL, 0); // for now the temporary fields in s are used
			fwrite(s.audio, sizeof(unsigned char), s.bytes, out);
			outc += s.bytes;
			framec++;
		}
		
		if(ret == MPG123_NEED_MORE)
		{
			left = s.bufend - s.next_frame;
			memcpy(buf, buf+(INBUFF-left), left);
			len = fread(buf+left, sizeof(unsigned char), INBUFF-left, in);
			if(len <= 0)
				break;
			inc += len;
			ret = mpgraw_feed(&s, buf, len+left);
		}
		else if(ret == MPG123_ERR)
		{
			fprintf(stderr, "Error: %s", mpg123_strerror(s.mh));
			break; 
		}
	}

	fprintf(stderr, "%lu bytes in, %lu bytes out, %lu frames\n", (unsigned long)inc, (unsigned long)outc, (unsigned long) framec);

	fclose(out);
	fclose(in);
	mpgraw_close(&s);
	mpg123_exit();
	return 0;
}
