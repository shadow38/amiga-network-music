FLAC test files (plan step 1.8b), made once from the signals of tests/flac_data.py with flac 1.3.4:

  flac --force-raw-format --endian=little --sign=signed --channels=C --bps=B --sample-rate=R -<level> -o F S.raw

  file              signal    C  B   R      level  extra
  s16-l0.flac       s16       2  16  44100  0
  s16-l5.flac       s16       2  16  44100  5
  s16-l8.flac       s16       2  16  44100  8
  m16-l5.flac       m16       1  16  44100  5
  s24-l5.flac       s24       2  24  44100  5
  s16-48k-l5.flac   s16-48k   2  16  48000  5
  s16-l5-pad.flac   s16       2  16  44100  5      --padding=1258291 (metadata bigger than the stream buffer)
  bench-l8.flac     bench     2  16  44100  8      --no-padding: built into amisqueeze for the speed measurement
                                                     (tools/bin2c.py tests/data/bench-l8.flac bench_flac
                                                     src/amisqueeze/bench_flac.c)

FLAC is lossless: decoded, each file gives flac_data.expected(signal) (24 bits: their 16 most significant bits).

MP3 test files (plan step 1.8c), made once from the same signals with lame 3.100:

  lame -r -s R --bitwidth 16 --signed --little-endian -m M <options> S.raw F

  file              signal    R      M  options
  s16-128.mp3       s16       44.1   j  -b 128 --cbr --id3v2-only --tt Test     (LAME header frame, ID3v2 tag)
  s16-v2.mp3        s16       44.1   j  -V 2 --id3v2-only --tt Test             (VBR)
  s16-320-raw.mp3   s16       44.1   s  -b 320 --cbr -t                         (frames only, no header, no tag)
  m16-64.mp3        m16       44.1   m  -b 64 --cbr --id3v2-only --tt Test      (mono)
  s16-48k-192.mp3   s16-48k   48     j  -b 192 --cbr --id3v2-only --tt Test
  s16-22k-64.mp3    s16-22k   22.05  j  -b 64 --cbr --id3v2-only --tt Test      (MPEG-2)

MP3 is lossy: the tests compare the decoded audio with the signal, after its delay (encoder and decoder, plus the
LAME header frame, decoded as silence), by its signal-to-noise ratio (lame's own decoder gives the same values).
