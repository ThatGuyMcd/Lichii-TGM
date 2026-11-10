# Lichii-tgm-v1.0.2

Forest Lantern brings pine-green dark mode and a pale-green light mode to the
library, menus, playback toolbar, settings, per-game properties and child dialogs.
Original lantern, woodland and sleeping-fox illustrations frame the content.
Controls and text use standard Qt widgets with keyboard focus and scalable type.

The settings sidebar groups all 16 existing pages and searches their names, labels,
help text, combo choices and nested tabs. Appearance and Library contains the theme
selector. Controls retains Player 1–8, Advanced and Microphone, plus shared controls.
Per-game properties uses the same navigation, with Game Info, all ten original
settings pages and NUP metadata controls. Long pages scroll; dialog buttons remain
outside the scrolling content. No emulation settings, choices or overrides were removed.

The playback toolbar shares the existing menu actions. Library Play uses the existing
entry validation. Tree, grid and carousel views and their context menus remain available.
The toolbar hides during single-window fullscreen and photo hotsampling, then restores
its previous visibility. View → Playback controls its visibility.

On first upgrade, an existing light theme becomes Forest Lantern Light and an existing
dark theme becomes Forest Lantern Dark. Only the theme selection changes. The original
six themes remain selectable; this migration runs once. Existing data locations,
controller profiles, hotkeys, saves, caches and configuration keys are retained.

This release builds on the v1.0.1 OOT integration. It changes the desktop interface;
it does not claim new gameplay fixes, controller detection fixes, or resolution of the
previously reported kernel blue screen or damaged game assets.

## Validation

Build both Windows x86_64 and Linux x86_64 AppImage from the same source commit.
Run the existing test suites plus standalone Qt navigation/theme tests, and inspect
both themes using the standalone preview. The preview loads settings forms without
starting the emulator or accessing games. Test programs are excluded from packages.
Actual gameplay validation remains a separate user check.
