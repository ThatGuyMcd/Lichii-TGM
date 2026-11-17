# Couch Mode

I've added Couch Mode as an optional full-screen presentation of our existing library and settings. Desktop remains the default. Open **View → Couch Mode**, or press **Ctrl+Shift+C**. The GUI also accepts **--couch** for a launch-specific opt-in, suitable for a Steam shortcut; this keeps your startup preference. The Desktop dock button restores the desktop window. In Settings → Application → Couch Mode, **Start in Couch Mode** controls future startup.

The first desktop launch with Couch Mode available offers **Switch now**, **Later** and a **Never ask again** checkbox. Later leaves Desktop open and offers the invitation on the next desktop launch. Switching to Couch Mode or choosing Never ask again remembers the decision. You can restore the invitation in Settings → Application → Couch Mode. A launch that opens a game or starts directly in Couch Mode keeps its existing flow.

The Horizon interface uses a 1920×1080 design canvas. It scales uniformly to fit the display, preserving proportions and navigation order. A 1280×800 Steam Deck displays a centered 1280×720 interface with 40 pixels above and below. Settings text scale is separately adjustable from 75% to 125%.

## Controller navigation

| Input | Action |
| --- | --- |
| D-pad or left stick | Move focus; scroll the focused list into view |
| A / Nintendo A / Cross | Select or edit |
| B / Nintendo B / Circle | Back or cancel |
| LB / RB (L / R) | Previous / next main tab |
| LT / RT (ZL / ZR) | Previous / next settings group |
| X / Square | Search; delete a character on the keyboard |
| Y / Triangle | Native context options for a setting; collection management |
| Start / Options | Session menu: resume, stop, add a game folder, Desktop, exit |
| Back + Start during gameplay | Pause and open Couch Mode |
| Hold Back for one second in input testing | Finish the input test |

Keyboard navigation is also available: arrows, Enter, Escape, Q/E for main tabs, Page Up/Down for groups, F3 for search and F10 for the session menu. Physical keyboards and mice remain usable.

Settings groups appear horizontally below the main tabs. Sections and settings have independent vertical scrolling. Search occupies one line and searches all available settings. The bottom actions are **Help → Review → Save**. Leaving Settings, changing scope, launching software, switching to Desktop or closing with a draft offers **Save and leave**, **Discard and leave**, and **Keep editing**.

## Settings and input

The shell reads the real native controls, including their current values, ranges, choice lists, validators, runtime locks, tooltips and per-game inheritance. It invokes the same native apply and persistence paths as Desktop. Native subdialogs retain their own confirmation and validation.

Numeric controls provide adjustment, a slider where applicable, and exact entry. Date/time fields use wheels for the fields exposed by the native editor, including seconds where supported. Text fields open a QWERTY keyboard with Shift, numbers, punctuation, symbols, caret movement, backspace, paste, clear, OK and Cancel. Password fields remain masked. Backspace moves by Unicode grapheme rather than splitting an emoji.

Controller binding uses the original input poller and the original controller preview widget. Couch renders that widget into a texture; it does not maintain another drawing or mapping implementation. Navigation pauses while a binding is captured. Release the selecting button before providing the new binding. Controller testing displays live input and suspends normal navigation.

Native lists and trees use a scrolling browser. Left/right selects the column; A opens a branch, toggles a checkbox or edits the selected field. Back returns to the parent. Hotkey conflicts are checked by the existing hotkey editor. File selection supports folders, drives, filters, paths, save names and multiple selection.

Library art, titles, paths, favorites and launch behavior come from the desktop model. Collections are stored independently in `couch-library.json` beside the normal configuration. Deleting a collection keeps its games.

## Theme packs

Themes are directories containing `theme.json` and optional images/fonts. Import the manifest using the Themes tab, or Settings → Application → Couch Mode. Imported packs are copied into `<configuration directory>/couch-themes/<id>/`. Select a theme to preview it; Save persists the preference, and Discard restores the saved theme.

See [the example pack](../dist/qt_themes/couch/example/theme.json). The format is declarative JSON; executable QML and scripts are not part of a theme.

The version-1 manifest supports:

- `id`, `name`, `author`, `base` (`dark` or `light`) and `fontFamily`.
- `colours`: background, surface, text, muted, line, focus, accent, onPrimary and scrim.
- `metrics`: rowHeight, mappingRowHeight, sectionHeight, sidebarWidth, columnGap, dockHeight, radius, focusWidth, fontSize, valueSize, smallSize and titleSize.
- `assets`: local image/font paths. `background` or `background.<view>` supplies a background. `font` registers a bundled font; set its family using `fontFamily`. `icon.search`, `icon.profile`, `icon.collections`, `icon.controllers`, `icon.themes`, `icon.settings` and `icon.desktop` replace dock/header icons.
- `layers`: up to 64 decorative image, text or rectangle layers. Each supplies x, y, width and height in 1920×1080 coordinates, optional view, opacity, colour and radius. Image layers select an asset and `fill: "crop"` or fit. Text layers accept fontSize and the tokens `{game.name}`, `{game.id}` and `{view}`.
- `views`: home, library, collections, themes and settings. A background slot positions the background image. Home supports hero and games slots; library supports a games slot. Each non-settings view supports a dock slot. Slots use absolute design-canvas coordinates.

Control semantics and controller focus order remain stable across skins. Theme layouts scale with the same canvas as Horizon. Rows and text have minimum sizes to keep controller navigation readable. Theme assets must remain inside their pack. Absolute/network paths, parent traversal, escaping symlinks, unsupported file types and excessive file sizes are rejected. Missing or invalid selected packs fall back to Horizon dark.

## Development and validation

Generic adapters live in `src/qt_common/couch`, the emulator integration in `src/yuzu/couch`, and the production scene in `dist/qt_themes/couch`. The catalogue supplies labels and grouping only; it contains no replacement setting values.

`src/tests/qt_couch` includes a standalone Qt test target for widgets, validation, native dialogs, the production keyboard, date/number editors, scrolling and 1080p/800p geometry. When built inside Lichii, `couch_shell_tests` additionally tests the real shell's draft/save/discard guards, failed saves and Unicode editing. These test executables do not launch an emulation session.

Windows deployment must scan `dist/qt_themes/couch` with `windeployqt --qmldir`. Linux AppImage deployment must include the Qt Quick, Quick Widgets, Basic Controls, layouts and QML worker modules and their dependencies. The theme and controller tests should be run against the deployed modules as well as the development Qt installation.

I've included automated checks for 1920×1080 and 1280×800, both scroll areas, native adapters, numeric/date/text editors, theme validation, draft transactions, modal input ownership, configuration restoration and the invitation choices. The release includes matching Windows and Linux AppImage packages with the required Qt Quick modules.
