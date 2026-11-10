# Lichii-tgm-v1.0.2

Forest Lantern brings pine-green dark mode and pale-green light mode to the
library, menus, playback toolbar, global settings and per-game properties.
The small lantern header remains. The fox footer and generic forest landscape
are no longer displayed or bundled.

The selected-game panel shows one large cover from the game's existing metadata,
including user-provided NUP icons, with its proportions preserved. It uses original
encoded artwork rather than an enlarged list thumbnail. Decoding runs away from
the UI thread, rejects malformed/oversized images, caches decoded covers within
16 MiB, and ignores stale results when the selection changes. Missing artwork has
a neutral placeholder. Tree, grid and carousel views remain available. Narrow
library layouts provide a full-width details view with Back and visible actions.

Comfortable spacing is the default, with larger toolbar controls, dropdown fields
and choices, checkboxes, tabs, scrollbars and numeric step buttons. Native keyboard
entry and existing numeric ranges/precision are retained. Appearance and Library
offers Compact spacing and an Open settings full-screen preference. Existing
themes and all original settings remain available.

Settings opens full-screen on the owner's monitor, with Back, a Windowed option
and a fixed footer. Save & close retains the original OK action. Apply is available
while stopped as well as during emulation. Back uses each dialog's original
Cancel/Close action, preserving validation and in-progress work guards. Dialogs
whose controls apply immediately retain their existing Close behaviour.

The six navigation categories are Application, Emulation, Video, Devices, Services
and Diagnostics. All 16 original global pages remain searchable, including inside
collapsed groups and nested tabs. A Sections drawer replaces the sidebar when
width is limited. Per-game settings retain Game Info, Add-Ons and all ten original
configuration pages. Controls retains P1–P8, Advanced, Microphone, shared controls,
profiles and every binding. Dense controller sections reflow rather than losing
controls. Settings that cannot change during emulation stay disabled.

The playback toolbar shares existing menu actions. Comfortable mode exposes the
complete menus through Menu; hiding the toolbar restores the traditional menu bar.
Game launch still uses existing entry validation. Touch selection uses explicit
Play; normal mouse/keyboard activation remains. Fullscreen and photo hotsampling
hide their UI chrome and restore it afterwards.

Existing configuration keys, data paths, profiles, saves and cache formats are
retained. New UI preferences do not change emulation values. This build inherits
the reviewed v1.0.1 OOT integration and does not claim new gameplay, controller
detection, damaged-game-data or driver fixes.

## Validation

Build Windows x86_64 and Linux x86_64 AppImage from the same source commit.
Run all nine CTest groups, including 199 navigation checks and shared UI tests
for retained edits/buttons, real tap targets, popup choice heights, full-screen
switching, numeric ranges, asynchronous covers, guarded Cancel and immediate
settings. Original forms and ApplyConfiguration paths are checked separately.

Standalone Qt previews inspect both palettes and settings forms without starting
the emulator or loading games. Package checks cover versions, checksums, runtime
dependencies and the relocatable AppImage launcher. Test executables are excluded
from release archives.

Actual touch hardware, on-screen keyboard behaviour and SteamOS Game Mode require
a check on the owner's handheld. Automated/WSL tests do not establish those results,
and gameplay was not run during this UI validation.
