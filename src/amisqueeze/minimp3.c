/*
 * minimp3.c - the implementation of minimp3 (third_party/minimp3/minimp3.h, CC0), in a file of its own: built
 * without our warnings (Makefile), used by mp3.c. Only in the builds with AMISQUEEZE_MP3 (floating point in
 * hardware, and the host's). The options are those of mp3.c: no SIMD, so that the host tests run the same code as
 * the Amiga; layer III only (what LMS sends as "mp3").
 */
#ifdef AMISQUEEZE_MP3
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#include "minimp3.h"
#endif
