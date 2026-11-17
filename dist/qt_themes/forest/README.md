# Forest Lantern

I've kept Forest Lantern's dark and light themes together, with the same lantern
header and touch spacing. `style.qss` uses palette tokens replaced by
`QtCommon::ForestTheme::Apply`. I've bundled the theme as a Qt resource, so it needs
no network, font download or external theme files at runtime.
Only `header-bough.png` is bundled. The fox and forest-glade images remain archived
in source for provenance; the UI no longer displays or packages them. Library
covers use the selected game's existing metadata, decoded asynchronously and fitted
without stretching. Touch spacing is independent of the selected palette.

The three PNG assets are original images generated for Lichii-tgm on 10 November
2026 using OpenAI image generation, guided by the selected Forest Lantern concept.
They depict generic woodland scenery, a lantern and a sleeping fox. They contain
no game screenshots, game logos or extracted game textures. They are distributed
with this project's GPL-3.0-or-later licensed UI.

- `header-bough.png`: transparent leafy bough and a small amber lantern; clear left area.
- `footer-fox.png`: transparent sleeping fox, meadow flowers and a small lantern.
- `forest-glade.png`: original forest path, stream and warm lantern landscape.

I've retained the full generation prompts and selected concept reference locally
under `build-local` for design provenance. The runtime keeps the header artwork
away from settings controls. Menu and toolbar icons are drawn with Qt vector paths.
