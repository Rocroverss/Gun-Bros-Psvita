# Gun Bros WavStream EOF fix

These files are based on SoLoud commit
[`7b6cb7185d12b0d3283a9bf30e6cc3295e57a77c`](https://github.com/jarikomppa/soloud/tree/7b6cb7185d12b0d3283a9bf30e6cc3295e57a77c),
the revision packaged by VitaSDK. Original license notices are retained.
`stb_vorbis.h` contains only the declaration section of that revision's
`src/audiosource/wav/stb_vorbis.c`; the decoder implementation still comes from
the SDK archive.

The original OGG callback loops forever when the decoder returns zero frames
before `mSampleCount`. No samples are copied, neither offset advances, and the
same read repeats while SoLoud holds its mixer mutex. A game thread calling
`isValidVoiceHandle`, stop, pause, or another mixer operation then blocks too.
This matches the reported music-dependent freezes, but does not establish that
every reported crash has the same cause.

Local modifications:

- Treat decoder EOF as authoritative rather than relying on length metadata.
- Zero-fill the output, including failed-open voices and the final partial block.
- Permit a loop restart at EOF, but terminate if the restart produces no frames.
  Every callback iteration therefore consumes samples, retries once, or exits.
- Keep explicit per-instance end state so a failed looping voice can terminate.
- Clear cached OGG packet pointers and offsets on rewind.

CMake compiles this replacement directly into the executable before linking
`libsoloud.a`. The archive's original WavStream object is consequently unused.
The local header must be used by both `audio.cpp` and the replacement because the
instance gains an end-state field. Both compile without RTTI to match VitaSDK.
Do not replace this with a current SoLoud header: newer versions have a different
audio callback ABI. An SDK upgrade needs a coordinated review of this override.

Host regression tests (g++, ffmpeg, and an unpacked copy of the pinned source):

```sh
SOLOUD_SOURCE=/path/to/pinned/soloud python3 extras/test_music_stream.py
# Optional: decode each of the seven game tracks through three loop boundaries.
SOLOUD_SOURCE=/path/to/pinned/soloud GUNBROS_MUSIC_DIR=/path/to/files \
  python3 extras/test_music_stream.py
```

The tests use the real decoder and null mixer, inject zero/negative decoder
returns and failed rewinds, exercise mono/stereo EOF and rewind behavior, and
confirm that the original SDK implementation times out on the same EOF case.
No game assets are included in the tests.

Hardware acceptance still requires leaving music enabled in menus and gameplay
for at least 20–30 minutes, crossing multiple track ends, and checking pause,
resume, and track changes on an affected Vita. Host tests cannot validate Vita
thread scheduling, storage behavior, or unrelated native-game crashes.
