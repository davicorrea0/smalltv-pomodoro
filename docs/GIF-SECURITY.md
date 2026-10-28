# GIF decoder hardening — 1.18.1

The album previously used the machine's installed AnimatedGIF library (2.2.0 in the previous build).
It now builds its own copy of AnimatedGIF 2.2.3 with local hardening. The upstream issue is
[bitbank2/AnimatedGIF#115](https://github.com/bitbank2/AnimatedGIF/issues/115).
The unmodified 2.2.3 snapshot also produced real ASan/UBSan failures in the 32-bit mutation harness;
upgrading the dependency alone was insufficient.

## Changes

- Validate the complete GIF structure both after upload and before opening an existing album item.
  Reject truncated blocks/trailers, invalid extension structure, missing palettes, zero or oversized
  dimensions, frames outside the canvas, LZW minimum code sizes outside 2–8 and more than 2,000 frames.
  The canvas limit is 480×480. Invalid uploads are deleted and an error is returned to the panel.
- Replace the buffered header parser with sequential reads and checked seeks. Palette conversion uses
  the existing scratch buffer. Exact read lengths are checked before parsing bytes.
- Refill compressed input through checked sub-block reads. Extract LZW codes without reading past
  the available bytes. Reject invalid first codes, dictionary references and cyclic links; stop the
  dictionary at 4,096 entries and bound expansion to the existing pixel stack.
- Keep the standard RAW scanline renderer, clipping, transparency, disposal and interlacing behavior.
  Yield during file reads and validation to service the ESP8266 watchdog.
- Close the underlying file when opening a GIF fails. Read the actual remaining bytes at EOF.
- Namespace and parenthesize the decoder's integer-load macros to avoid collisions with JPEGDEC and
  incorrect results for pointer expressions. `gif.inl` is renamed `gif_inl.h` for Arduino's source copier.

No new persistent buffers or framebuffer were added. Third-party files remain Apache-2.0;
local parser changes are marked in `src/AnimatedGIF/safe_parse.h`.

## Verification on 2026-10-22

- ESP8266 Arduino core 3.1.2, generic board, 4M3M flash, 160 MHz, `ip=lm2n`, `build.float=`: compiled.
- 32-bit g++ 15.2, AddressSanitizer + UndefinedBehaviorSanitizer, MCU width limit and byte-load path:
  **100,000 mutations through the RAM reader and 100,000 through file callbacks, zero sanitizer
  failures or timeouts.** The seed corpus contained seven valid animations and five malformed samples.
  Invalid inputs also reach the decoder directly, independently of the structural validator.
- Structural validator tests cover complete and truncated files, a truncated second frame, dimensions,
  palettes, extensions and LZW minimum code size.
- The five malformed regression samples no longer crash.
- For seven valid animations, the final framebuffer, drawn line count and rolling hash of every
  rendered row matched unmodified 2.2.3, through both RAM and file callbacks:

| Input | Drawn lines | All-row hash |
|---|---:|---|
| bw_wiggler_128x64 | 12,800 | 42158dee |
| ff_240 | 1,047 | 53363a1a |
| ff_small | 160 | 6ac915c3 |
| green | 32 | 46d041c5 |
| homer_tiny | 1,856 | e441c28c |
| pattern | 1,536 | 3b47a764 |
| thisisfine_240x179 | 5,907 | 74dfa846 |

Run `bash sim/test-gif.sh [corpus-directory] [case-count]` on Linux/WSL with g++ and multilib installed.
The bundled small corpus includes two ffmpeg-generated animations and the five malformed regression
samples; the broader comparison corpus was used locally. A 5-second alarm stops individual fuzz cases.
The forked `fuzz` mode saves failures; `fuzz-fast` stops at the first sanitizer failure.

## Scope and device check

This validates the GIF path used by SmallTV: RAW scanline output, RGB565, without Turbo or an allocated
framebuffer. It is not a general security certification of all AnimatedGIF APIs or of JPEGDEC.
Mutation testing is finite and does not prove the absence of every vulnerability.

The firmware has not been flashed or measured on the physical clock in this session. After installing
1.18.1, confirm the version, play an existing valid GIF, upload another valid animation and check that
a truncated GIF is rejected. That update kept the 4M3M layout and did not intentionally format LittleFS (later versions use 4M2M; see MIGRACAO-4M2M.md).
