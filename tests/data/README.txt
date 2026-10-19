FLAC test files (plan step 1.8b), made once from the signals of tests/flac_data.py with flac 1.3.4:

  flac --force-raw-format --endian=little --sign=signed --channels=C --bps=B --sample-rate=R -L -o F S.raw

  file              signal    C  B   R      level  extra
  s16-l0.flac       s16       2  16  44100  0
  s16-l5.flac       s16       2  16  44100  5
  s16-l8.flac       s16       2  16  44100  8
  m16-l5.flac       m16       1  16  44100  5
  s24-l5.flac       s24       2  24  44100  5
  s16-48k-l5.flac   s16-48k   2  16  48000  5
  s16-l5-pad.flac   s16       2  16  44100  5      --padding=1258291 (metadata bigger than the stream buffer)

FLAC is lossless: decoded, each file gives flac_data.expected(signal) (24 bits: their 16 most significant bits).
