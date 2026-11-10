<p align="center"><img src="dist/branding/lichii_icon.png" alt="Lichii-tgm" width="160"></p>

# Lichii-tgm-v1.0.2

Lichii-tgm is ThatGuyMcd's independent Nintendo Switch and Switch 2 emulator fork,
based on Lichii, Eglantine, Eden, yuzu and Sudachi. Version 1.0.2 introduces Forest
Lantern dark and light themes, woodland artwork, a playback toolbar and searchable,
organised settings. Every existing setting and per-game override is retained. See the
[release notes](docs/TGM-v1.0.2.md) for validation and known limitations.

This is an experimental emulator. Compatibility and performance depend on the game,
hardware and driver. Automated build and unit-test results are not gameplay validation.

## Downloads

Download the Windows ZIP or Linux AppImage from
[this fork's releases](https://github.com/ThatGuyMcd/Lichii-TGM/releases).

- Windows x86_64: extract the entire ZIP and run `lichii-tgm.exe`.
- Linux x86_64: make the AppImage executable with `chmod +x`, then run it.
- The command-line executable is `lichii-tgm-cli.exe` / `lichii-tgm-cli`.
- Releases include SHA-256 checksums and a build manifest.

Games, keys and firmware are not included. Use your own legally obtained game and
system files. Lichii-tgm is not affiliated with Nintendo or the upstream projects.

## Existing saves and settings

The fork continues to use the existing Eden/Eglantine user-data directories and
configuration keys. Existing settings, saves, firmware, keys and caches remain
accessible. Portable installations continue to use the `user` directory.

## Ocarina of Time Remake integration

The release retains the OOT integration at `4dafb0b5c1`, incorporating
upstream snapshot `d2eaae4f3abca636b030b726f107bace06f61079` and reviewed OOT
and supporting pull requests. Conflicting, superseded and experimental changes
were assessed separately. The release manifest and integration audit describe the
selected changes and validation limits.

Optional NVIDIA/AMD upscaler runtimes remain separate, user-supplied components.
See the in-application settings for their requirements and availability.

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

Original code, copyright notices and third-party licenses are retained. Thanks to
Lichii, Eglantine (including Camille LaVey), Eden, yuzu, Sudachi and their contributors.
The original fruit artwork is by YOB (yobslab). The fork's release wordmark is an
adaptation of that artwork; its provenance is in
[the artwork record](dist/branding/lichii-tgm-artwork.json).

See [LICENSE.txt](LICENSE.txt) and individual source files for license terms.
