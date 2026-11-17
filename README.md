<p align="center"><img src="dist/branding/lichii-tgm-v1.0-banner.png" alt="Lichii-tgm" width="900"></p>

# Lichii-tgm

This is my fork of Lichii, built on the work of Lichii, Eglantine, Eden, yuzu and
Sudachi. I'm focusing on OOT Remake, with improvements to rendering, stability,
performance and gaming handhelds.

I've kept our Forest Lantern dark and light themes, selected-game artwork, larger
touch controls and full-screen settings. All of the settings and per-game overrides
are still here. Compatibility and performance depend on your game, hardware and driver.

I've added an optional [Couch Mode](docs/CouchMode.md) with a full-screen library,
controller navigation, theme packs and the same settings and save paths as Desktop.
The first-launch invitation offers **Switch now**, **Later** and **Never ask again**.
You can also open it from **View → Couch Mode** or **Ctrl+Shift+C**, and choose
**Start in Couch Mode** in settings.

## Downloads

You can get the Windows ZIP and Linux AppImage from
[our releases page](https://github.com/ThatGuyMcd/Lichii-TGM/releases).
The [latest release](https://github.com/ThatGuyMcd/Lichii-TGM/releases/latest)
has the current version and the full list of changes.

- Windows x86_64: extract the entire ZIP and run `lichii-tgm.exe`.
- Linux x86_64: make the AppImage executable with `chmod +x`, then run it.
- The command-line executable is `lichii-tgm-cli.exe` / `lichii-tgm-cli`.
- Releases include SHA-256 checksums and a build manifest.

I don't bundle games, keys or firmware. You'll need your own legally obtained game
and system files. This is an independent fork, with no affiliation to Nintendo.

## Existing saves and settings

I've kept the existing Eden/Eglantine user-data directories and configuration keys.
Your settings, saves, firmware, keys and caches stay accessible. Portable
installations continue to use the `user` directory.

## Ocarina of Time Remake integration

I'm bringing useful OOT work across from Lichii and Fritchii while keeping our UI,
settings, save paths and aspect-ratio add-ons intact. Each release lists the commits
and PR components I've brought in, with credit to the original contributors.

Upscaler availability depends on your GPU and platform. The graphics settings show
the available options and their requirements.

## Building

See [the build guide](docs/Build.md) and [the fork release guide](docs/TGM-Release.md).
The internal build targets remain `yuzu` and `yuzu-cmd`; their output names are
`lichii-tgm` and `lichii-tgm-cli`.

## Reports and support

[Report issues on this fork](https://github.com/ThatGuyMcd/Lichii-TGM/issues).
Include the release version, game version, hardware, driver, settings and steps to
reproduce the issue. Review logs for personal information before sharing them.

The optional compatibility reporter connects to the
[upstream Lichii community service](https://lichii.zortos.me), which is operated
separately from this fork. Submission requires your explicit consent.

## Credits and license

I've kept the original copyright notices and third-party licenses. Thanks to
Lichii, Eglantine (including Camille LaVey), Eden, yuzu, Sudachi and their contributors.
The original fruit artwork is by YOB (yobslab). The fork's release wordmark is an
adaptation of that artwork; its provenance is in
[the artwork record](dist/branding/lichii-tgm-artwork.json).

See [LICENSE.txt](LICENSE.txt) and individual source files for license terms.
