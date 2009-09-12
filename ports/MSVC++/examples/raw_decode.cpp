/*
	raw_decode: test program for libmpg123, showing how to use the raw api to decode mp3
	copyright 2009 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org	
*/

#include <mpg123.h>
#include <stdio.h>
#include <tchar.h>
#include <wchar.h>
#include <memory.h>

#define INBUFF  16384 * 2 * 2
#define WAVE_FORMAT_PCM 0x0001
#define WAVE_FORMAT_IEEE_FLOAT 0x0003

FILE *out;
size_t totaloffset, dataoffset;
long rate;
int channels, enc;
unsigned short bitspersample, wavformat;

// write wav header
void initwav()
{
	unsigned int tmp32 = 0;
	unsigned short tmp16 = 0;

	fwrite("RIFF", 1, 4, out);
	totaloffset = ftell(out);

	fwrite(&tmp32, 1, 4, out); // total size
	fwrite("WAVE", 1, 4, out);
	fwrite("fmt ", 1, 4, out);
	tmp32 = 16;
	fwrite(&tmp32, 1, 4, out); // format length
	tmp16 = wavformat;
	fwrite(&tmp16, 1, 2, out); // format
	tmp16 = channels;
	fwrite(&tmp16, 1, 2, out); // channels
	tmp32 = rate;
	fwrite(&tmp32, 1, 4, out); // sample rate
	tmp32 = rate * bitspersample/8 * channels;
	fwrite(&tmp32, 1, 4, out); // bytes / second
	tmp16 = bitspersample/8 * channels; // float 16 or signed int 16
	fwrite(&tmp16, 1, 2, out); // block align
	tmp16 = bitspersample;
	fwrite(&tmp16, 1, 2, out); // bits per sample
	fwrite("data ", 1, 4, out);
	tmp32 = 0;
	dataoffset = ftell(out);
	fwrite(&tmp32, 1, 4, out); // data length
}

// rewrite wav header with final length infos
void closewav()
{
	unsigned int tmp32 = 0;
	unsigned short tmp16 = 0;

	long total = ftell(out);
	fseek(out, totaloffset, SEEK_SET);
	tmp32 = total - (totaloffset + 4);
	fwrite(&tmp32, 1, 4, out);
	fseek(out, dataoffset, SEEK_SET);
	tmp32 = total - (dataoffset + 4);

	fwrite(&tmp32, 1, 4, out);
}

// determine correct wav format and bits per sample
// from mpg123 enc value
void initwavformat()
{
	if(enc & MPG123_ENC_FLOAT_64)
	{
		bitspersample = 64;
		wavformat = WAVE_FORMAT_IEEE_FLOAT;
	}
	else if(enc & MPG123_ENC_FLOAT_32)
	{
		bitspersample = 32;
		wavformat = WAVE_FORMAT_IEEE_FLOAT;
	}
	else if(enc & MPG123_ENC_16)
	{
		bitspersample = 16;
		wavformat = WAVE_FORMAT_PCM;
	}
	else
	{
		bitspersample = 8;
		wavformat = WAVE_FORMAT_PCM;
	}
}

int _tmain(int argc, TCHAR **argv)
{
	unsigned char buf[INBUFF];
	FILE *in;
    mpgraw_state s;	
	int ret;
	size_t inc, outc;
	off_t len;
    int firstframedecoded;
    off_t* offsets;
	off_t step;
	size_t fill;
    size_t left;
	inc = outc = left = 0;
    firstframedecoded = 0;

	if(argc < 3)
	{
		fprintf(stderr,"Please supply in and out filenames\n");
		return -1;
	}

	mpg123_init();

	ret = mpgraw_open(&s, NULL, 44100, 2, MPG123_ENC_FLOAT_32);
	if(ret != MPG123_OK)
	{
		fprintf(stderr,"Unable to open stream: %s\n", mpg123_plain_strerror(ret));
		return -1;
	}

	in = _tfopen(argv[1], __T("rb"));
	if(in == NULL)
	{
		_ftprintf(stderr,__T("Unable to open input file %s\n"), argv[1]);
		return -1;
	}
	
	out = _tfopen(argv[2], __T("wb"));
	if(out == NULL)
	{
		_ftprintf(stderr,__T("Unable to open output file %s\n"), argv[2]);
		return -1;
	}
		
	while(1)
	{
        left = s.bufend - s.next_frame;
        memcpy(buf, buf+(INBUFF-left), left);
		len = fread(buf+left, sizeof(unsigned char), INBUFF-left, in);
		if(len <= 0)
			break;
		inc += len;
		ret = mpgraw_feed(&s, buf, len+left);

		while(ret != MPG123_ERR && ret != MPG123_NEED_MORE)
		{
            ret = mpgraw_next(&s, rawFlagAudioFrame);
            if(ret == MPG123_OK)
            {
			    ret = mpgraw_decode(&s, NULL, 0); // for now the temporary fields in s are used
			    if(ret == MPG123_OK && !firstframedecoded)
			    {
                    firstframedecoded = 1;
                    rate = s.rate;
                    channels = s.channels;
                    enc = s.encoding;
				    initwavformat();
				    initwav();
				    fprintf(stderr, "Format: %li Hz, %i channels, encoding value %i\n", rate, channels, enc);
			    }
			    fwrite(s.audio, sizeof(unsigned char), s.bytes, out);
			    outc += s.bytes;
            }
		}

		if(ret == MPG123_ERR)
		{
            fprintf(stderr, "Error: %s", mpg123_strerror(s.mh));
			break; 
		}
	}

    offsets = NULL;
    step = 0;
    fill = 0;	
    mpg123_index(s.mh, &offsets, &step, &fill);

	fprintf(stderr, "Finished\n", (unsigned long)inc, (unsigned long)outc);

	closewav();
	fclose(out);
	fclose(in);
    mpgraw_close(&s);
	mpg123_exit();
	return 0;
}
