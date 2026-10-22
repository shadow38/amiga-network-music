#!/bin/sh
# make-test-audio.sh - makes the test sounds of amisqueeze (docs/development/testing.md, "Test sounds"): 440 Hz
# in every format the player handles, left and right apart, a sweep, a glide cut into three gapless MP3s, two
# tones apart in an MP3, and the counter pattern for byte-by-byte comparisons. Put them in the server's music
# folder and rescan it.
#
# Usage: sh tools/make-test-audio.sh [DIR]      (default: out/test-audio)
# Needs sox, flac, lame and python3, from the PATH or from the SOX, FLAC, LAME and PYTHON variables.
set -e

cd "$(dirname "$0")/.."
dir=${1:-out/test-audio}
SOX=${SOX:-sox}
FLAC=${FLAC:-flac}
LAME=${LAME:-lame}
PYTHON=${PYTHON:-python3}
for tool in "$SOX" "$FLAC" "$LAME" "$PYTHON"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "make-test-audio: $tool not found" >&2; exit 1; }
done
mkdir -p "$dir"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# synth FILE RATE BITS CHANNELS SECONDS SYNTH-ARGS...: a sound made by sox (its format from the file name; raw:
# signed little-endian), at half of full scale (-6 dB: no clipping once resampled), without dither (a silent side
# stays at 0)
synth() {
    out=$1 rate=$2 bits=$3 channels=$4 secs=$5
    shift 5
    case $out in
    *.raw) order=-L ;;
    *) order= ;;
    esac
    "$SOX" -D -n -r "$rate" -b "$bits" -c "$channels" -e signed $order "$out" synth "$secs" "$@" vol 0.5
}

# synth_flac NAME RATE BITS CHANNELS SECONDS SYNTH-ARGS...: the same in FLAC, through raw PCM (flac warns about
# the extra chunks sox writes in its WAV and AIFF files)
synth_flac() {
    name=$1
    shift
    synth "$tmp/t.raw" "$@"
    "$FLAC" -s -f --force-raw-format --endian=little --sign=signed --sample-rate="$1" --bps="$2" --channels="$3" \
        -o "$dir/$name" "$tmp/t.raw"
}

# mp3_of WAV NAME: 128 kbit/s CBR, without an ID3v1 tag (LMS strips it before converting and sox then drops the
# last frame); lame writes its LAME header, which gives the delay and padding to drop (gapless)
mp3_of() {
    "$LAME" --quiet -b 128 --cbr --id3v2-only "$1" "$dir/$2"
}

# 440 Hz for 10 s, in each format and size
synth_flac tone-440.flac 44100 16 2 10 sine 440
synth_flac tone-440-48k.flac 48000 16 2 10 sine 440
synth_flac tone-440-24bit.flac 44100 24 2 10 sine 440
synth_flac tone-440-96k.flac 96000 24 2 10 sine 440
synth_flac tone-440-mono.flac 44100 16 1 10 sine 440
synth "$dir/tone-440.wav" 44100 16 2 10 sine 440
synth "$dir/tone-440-24bit.wav" 44100 24 2 10 sine 440
synth "$dir/tone-440.aiff" 44100 16 2 10 sine 440
"$SOX" -D -n -r 44100 -b 8 -c 2 -e unsigned "$dir/tone-440-8bit.wav" synth 10 sine 440 vol 0.5

# 440 Hz on one side only, 5 s each (remix: 0 is a silent channel)
synth_flac tone-left.flac 44100 16 2 5 sine 440 remix 1 0
synth_flac tone-right.flac 44100 16 2 5 sine 440 remix 0 1

# A sweep from 20 Hz to 20 kHz in 20 s (exponential)
synth_flac sweep.flac 44100 16 2 20 sine 20:20000

# 440 Hz on the left, 660 Hz on the right, 20 s, as an MP3
synth "$tmp/t.wav" 44100 16 2 20 sine 440 sine 660
mp3_of "$tmp/t.wav" test-20s.mp3

# A 12 s glide from 220 to 880 Hz cut into three MP3s at 4.1 s and 8.3 s, inside MP3 frames: played gapless, the
# glide goes on without a click or a gap at the boundaries
synth "$tmp/glide.wav" 44100 16 2 12 sine 220:880
"$SOX" "$tmp/glide.wav" "$tmp/g1.wav" trim 0 4.1
"$SOX" "$tmp/glide.wav" "$tmp/g2.wav" trim 4.1 4.2
"$SOX" "$tmp/glide.wav" "$tmp/g3.wav" trim 8.3
mp3_of "$tmp/g1.wav" gapless-1.mp3
mp3_of "$tmp/g2.wav" gapless-2.mp3
mp3_of "$tmp/g3.wav" gapless-3.mp3

# The counter pattern (tools/pcm-compare.py), 20 s, in FLAC: decoded losslessly by the server, so that the
# OUTPUT file of amisqueeze can be compared with it byte by byte (pcm-compare.py --counter)
"$PYTHON" tools/pcm-compare.py --write-counter 20 "$tmp/counter.raw"
"$FLAC" -s -f --force-raw-format --endian=little --sign=signed --sample-rate=44100 --bps=16 --channels=2 \
    -o "$dir/test-pattern.flac" "$tmp/counter.raw"

echo "make-test-audio: test sounds in $dir"
ls "$dir"
