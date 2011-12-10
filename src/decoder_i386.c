/* The all-in-one decoder module:

This shall encompass the layer bitstream decoding work, with focus on including the synth stuff that's called in decoding loops.

The idea is that putting stuff together here, as static functions even, helps the optimizer.
*/

#define DECODE_STATIC

#include "dct64_i386.c"
#include "decode_i386.c"
#include "decode_ntom.c"
#include "decode_2to1.c"
#include "decode_4to1.c"

#include "layer1.c"
#include "layer2.c"
#include "layer3.c"

#include "synthfunc.c"
