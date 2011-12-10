#include "config.h"
#include "mpg123.h"

int real_set_synth_functions(struct frame *fr, struct audio_info_struct *ai)
{
	typedef int (*func)(real *,int,unsigned char *,int *);
	typedef int (*func_mono)(real *,unsigned char *,int *);
	typedef void (*func_dct36)(real *,real *,real *,real *,real *);
	int ds = fr->down_sample;
	int p8=0;
#ifdef USE_3DNOW
	static func funcs[3][4] = {
#else
	static func funcs[2][4] = { 
#endif
		{ synth_1to1,
		  synth_2to1,
		  synth_4to1,
		  synth_ntom } ,
		{ synth_1to1_8bit,
		  synth_2to1_8bit,
		  synth_4to1_8bit,
		  synth_ntom_8bit } 
#ifdef USE_3DNOW
  	       ,{ synth_1to1_3dnow,
  		  synth_2to1,
 		  synth_4to1,
  		  synth_ntom }
#endif
	};

	static func_mono funcs_mono[2][2][4] = {    
		{ { synth_1to1_mono2stereo ,
		    synth_2to1_mono2stereo ,
		    synth_4to1_mono2stereo ,
		    synth_ntom_mono2stereo } ,
		  { synth_1to1_8bit_mono2stereo ,
		    synth_2to1_8bit_mono2stereo ,
		    synth_4to1_8bit_mono2stereo ,
		    synth_ntom_8bit_mono2stereo } } ,
		{ { synth_1to1_mono ,
		    synth_2to1_mono ,
		    synth_4to1_mono ,
		    synth_ntom_mono } ,
		  { synth_1to1_8bit_mono ,
		    synth_2to1_8bit_mono ,
		    synth_4to1_8bit_mono ,
		    synth_ntom_8bit_mono } }
	};

#ifdef USE_3DNOW	
	static func_dct36 funcs_dct36[2] = {dct36 , dct36_3dnow};
#endif

	if((ai->format & AUDIO_FORMAT_MASK) == AUDIO_FORMAT_8)
		p8 = 1;
	fr->synth = funcs[p8][ds];
	fr->synth_mono = funcs_mono[param.force_stereo?0:1][p8][ds];

/* TODO: make autodetection for _all_ x86 optimizations (maybe just for i586+ and keep separate 486 build?) */
#ifdef USE_3DNOW
	/* check cpuflags bit 31 (3DNow!) and 23 (MMX) */
	if((param.stat_3dnow < 2) && 
	   ((param.stat_3dnow == 1) ||
	    (getcpuflags() & 0x80800000) == 0x80800000))
      	{
	  fr->synth = funcs[2][ds]; /* 3DNow! optimized synth_1to1() */
	  fr->dct36 = funcs_dct36[1]; /* 3DNow! optimized dct36() */
	}
	else
	{
	       	  fr->dct36 = funcs_dct36[0];
      	}
#endif

	if(p8) {
		if(make_conv16to8_table(ai->format) != 0)
		{
			/* it's a bit more work to get proper error propagation up */
			return 1;
		}
	}
	return 0;
}
