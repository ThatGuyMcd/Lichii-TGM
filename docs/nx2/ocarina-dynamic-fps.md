# Ocarina Dynamic FPS add-on

`Dynamic FPS by Dark (built-in)` is a game Add-On, enabled by default for Ocarina.
It targets title `0400486024DEC000`, module `main`, build ID
`E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348` only. Max Resolution remains a
separate, default-on Add-On. Either can be disabled independently in Add-Ons;
the existing disabled-add-on configuration persists that choice.

## Credit and source

Dark, an Eglantine developer, wrote the routine. This port preserves the call and
120-byte routine from Frizti's `oot-gather-fix` commit
`765659af4ccb7201d1145c57f3b45ce84653cea0`. That commit explicitly states that
Dark gave permission to bundle it. Frizti supplied the built-in integration.
CTGL's #299 changed one instruction: the delta cap is `CNTFRQ / 10` instead of
`CNTFRQ / 20`.

The routine's SHA-256 is
`0afb4af10a78bb49372e6807fc5b71d9fa99d828b7625f0fab7429fc66a4b1e6`
(`3c15dfb57f748328ad7d1c1675941dc2ace32a5e7b10907368ceac6a9a8c5ee1` before the
cap change).
The call's SHA-256 is
`e4d3f65ad1b568b557f02b9a087a9c304240ac70d6ef474dac83a1f97737515e`.
This port adds original-byte and memory-layout guards, and
transactional validation before writing either record.

## Verified original layout

An independently extracted original `main.text` has SHA-256
`f71eb7b75b11dcc8d955f3f6e987a098573288d9ce41aa22a96272541e858c5f`, matching
the text hash in its NSO header. Its instruction at module offset `0x6E6CC4`
is `28 18 20 1E`, or `fdiv s8, s1, s0`. The patch replaces it with
`bl 0x3C46200` and executes the original division first in the new routine.
The enclosing function saves and restores its return address. The continuation
does not consume the routine's clobbered scratch registers or flags before
intervening calls or redefinition.

The original text ends at `0x3C461D0`; read-only data starts at `0x3C47000`.
The routine occupies `0x3C46200` through `0x3C46277`, in executable alignment
padding, not original text. `AppLoader_NSO::LoadModule` zero-initializes the
module image before copying segments and rounds the text mapping to a page.
The loader checks that this space is still zero-filled executable padding
before installing the routine.

Data ends at `0x4D46B80` and BSS ends at `0x4EA0000`. The routine stores its
previous timestamp and smoothed delta at `0x4E9FE00` and `0x4E9FE08`. The
loader requires both words to fit BSS and remain zero-initialized. These
checks establish addressability and initialization, not exclusive ownership
of that storage throughout gameplay.

Any changed user-mod bytes in either patch record or the timer state take
precedence over the entire built-in patch. An incompatible build, original
instruction, layout, or truncated module leaves both patch records untouched.

## Water ripple passes

The ripple simulation (compute shaders `fb4203a7d928ed42` then `e2ce39e630ce65f3`,
256x256 ping-pong textures) sets its pass count at `main+0x475F8` as
`iterations * (int)step`, with `iterations` 6. Truncation is harmless at the
original step of exactly 1.0, but any step below 1.0 runs no passes: at 120 fps
the ripples stop, and the grid origin that follows the player keeps moving with
no pass to apply the shift. Upstream PR #1180 measured this at a save in Zora's
Domain, frames that ran the simulation out of 10: no patch 10, FPS Unlock capped
at 60 fps 4 (step 0.998 to 1.003), FPS Unlock at about 99 fps 0.

Both built-ins replace that `fcvtzs` with `bl 0x3C46300` and drop the `mul` that
followed it. The routine adds `iterations * step` to a remainder kept per water
object, runs the rounded whole number of passes and keeps the rest, with at least
one pass whenever the step is positive so the origin shift is always applied. A
paused game (step 0) still runs none. The final pass `6a7f45ab293a93b1` was never
affected.

The remainders live in 8 slots of 16 bytes from `0x4EA0000` (the water object
from `x21` at the patch site, then the remainder as a float), with the next slot
to reuse at `0x4EA0080`. An object without a slot takes the next one in turn, so
two water objects drawn in the same frame don't pull from one shared fraction.
The original timer state remains 16 bytes at `0x4E9FE00`. The routine is 148 bytes at
`0x3C46300-0x3C46393`, clear of the Dynamic FPS routine at `0x3C46200` and of the
ultrawide caves at `0x3C46400` and up. It uses w8, x9-x12 and s0-s2, none read
by the game again before it writes them.

## Combined patch storage

TGM keeps Photo Mode's state at `0x4E9FE40..0x4E9FED0` and its executable cave
at `0x3C46420`. The aspect code occupies `0x3C46800..0x3C46A10`. Importing the
water, sound and step-counter PRs at their original addresses would overlap those
features. They are relocated together through the patch pipeline:

| Component | Executable storage | Mutable state |
|---|---|---|
| Water (#1180) | `0x3C46300`, 148 bytes | `0x4EA0000..0x4EA0088` |
| Sound (#1289) | `0x3C46280`, 16 bytes; `0x3C46A20`, 124 bytes | `0x4EA0090..0x4EA0098` |
| Step counters (#1587) | `0x3C46B00..0x3C46C3C` | `0x4EA00A0..0x4EA01B0` |

The loader reserves one new zero-filled 4 KiB data page after the original BSS,
only for the supported OOT main build and exact original segment layout. Both
layout calculation and loading reserve the same page. Other titles, modules and
layouts receive no allocation. Patch application requires this reservation, clear
state, every original instruction and unused executable padding; a conflicting
user mod skips the whole affected built-in. Original Photo Mode storage is unchanged.

FPS Unlock applies all three additions. Dynamic FPS uses the water correction;
the sound and step-counter fixes apply to FPS Unlock only. The sound job updates
at 60 Hz above 60 FPS and receives a step of at least one. Four step-driven counters
use sixteen per-address fractional accumulators to avoid integer truncation at
high frame rates. This does not establish a native 60 FPS performance gain.

## Timing limits

The routine reads `CNTPCT_EL0` and `CNTFRQ_EL0`. After its first call, it caps
the timestamp difference at `CNTFRQ / 10`, or 100 ms, then smooths it by adding
one eighth of the signed difference from the previous smoothed value.
It calculates `s2 = (smoothed_ticks / CNTFRQ) * s1`. It replaces the original
`s8 = s1 / s0` only when `s2 > 1.03 * s8`.
The original code selects `s1` as 30 or 60 and converts the integer field at
`x21 + 0x2C8` into `s0`, so the result is a frame-step scale, not seconds.

This is not a promise of correct game speed at every frame rate. It retains
the original step as a floor, caps long frames, and smooths changes. Actual
gameplay speed and the runtime value of that frame-rate field still require
live validation.

For Switch 1 titles, `CoreTiming::GetClockTicks` scales the counter for CPU
Boost (1.7x) and Overclock (2x). It no longer does so for Switch 2 titles,
whose SDK converts ticks with a fixed 31.25 MHz: the scaled counter made this
routine, and every other tick-based time, run 1.7x fast on Boost (#317).
Enabling core-speed synchronization still divides the counter by the
configured speed limit, which alters the elapsed seconds this routine
calculates. Correct gameplay speed with the Add-On is unverified.

## FPS Unlock ground velocity

The character controller compares a frame-rate value with `100.0` in three places; above it
(`b.gt` at `0x13A070`, `0x13D710` and `0x389AC2C`) it scales its ground's velocity by `0.1`
(`0x3E40FB4`). Over 100 fps a moving platform then slides away under the player.
`FPS Unlock (built-in)` replaces the three branches with `nop`. `Dynamic FPS by Dark (built-in)`
never exceeds 60 fps and leaves them alone. Measured on the Deku Tree B1 raft: the applied
velocity was 0.100 of the ground's at steps below 0.6 and 1.000 at steps of 0.627 and above.

## Tests

The `[patch_manager]` tests call production `PatchManager::PatchNSO` with
synthetic module images and real IPS/IPS32 user mods. They cover default-on and
disable behavior, title/module/build gates, original-byte and layout
guards, truncated inputs, all-record user-mod precedence, and Max Resolution.
The `[addon_config]` tests round-trip the production configuration reader and
writer, including legacy configurations and repeated enable/disable cycles.

Run them from a clang/Ninja test build with:

```sh
cmake --build build --target tests --parallel 4
build/bin/tests '[patch_manager],[addon_config]'
```
