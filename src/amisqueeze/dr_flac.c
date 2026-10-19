/*
 * dr_flac.c - the implementation of dr_flac (third_party/dr_flac/dr_flac.h, public domain or MIT-0), in a file of
 * its own: built without our warnings (Makefile), used by flac.c. The options shape dr_flac's structures: flac.c
 * uses the same ones.
 */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_WCHAR
#define DR_FLAC_NO_CRC                      /* the stream comes over TCP, already checked */
#include "dr_flac.h"
