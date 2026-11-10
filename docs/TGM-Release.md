# Lichii-tgm releases

`CMakeModules/Branding.cmake` defines the product name, numeric version, release tag,
repository and Linux application ID. Release 1.0.1 is titled/tagged `Lichii-tgm-v1.0.1`;
numeric metadata uses `1.0.1` (Windows `1.0.1.0`).

Configure with `-DLICHII_BUILD_TAG=Lichii-tgm-v1.0.1` and
`-DLICHII_BUILD_PLATFORM=windows-x86_64` or `linux-x86_64`. Builds without a release
tag retain their development revision. Internal CMake targets remain unchanged.

Build the GUI, CLI and tests, then run CTest on both platforms. Windows also ships
`lichii-tgm-updater.exe`. Qt tests require the matching Qt Test library and offscreen
plugin. Test executables are excluded from release archives.

On the configured local machine, `build-local/Build-All.ps1 -Tests` builds and
validates both platforms and creates matching ZIP/AppImage files. Machine-specific
toolchain setup is in the workspace `BUILD-HERE.md`; local helpers/builds are ignored.

## Release assets

- `Lichii-tgm-v1.0.1-windows-x86_64.zip`
- `Lichii-tgm-v1.0.1-linux-x86_64.AppImage`
- `SHA256SUMS` and a JSON build manifest
- Reviewed integration and branding audits

Package runtime libraries and `LICENSE.txt`; verify archive integrity, dependencies
and Linux executable build IDs. Build both platforms from the same tested commit.
The published tag must point to that commit.

Pass `--custom-apprun tools/release/AppRun` to linuxdeploy when building AppImages.
The launcher sets Qt plugin/QML and library paths relative to the mounted AppImage.
For Qt 6, deploy the Wayland client module and discover the available platform
plugin filenames from the Qt installation.

Upload verified assets to a draft release, verify asset digests, then publish it as
a non-prerelease. Fork tags use `Lichii-tgm-vMAJOR.MINOR[.PATCH]`, ordered numerically;
they never select upstream `build-N` releases. Updates target `ThatGuyMcd/Lichii-TGM`.

## Branding boundaries

Visible names, titles, executable outputs, version metadata, desktop identity,
release artwork and fork support/download links use the fork identity.

Existing user-data paths, environment/configuration keys, cache formats, internal
CMake targets and upstream compatibility-service API/header names remain compatible.
Copyright/attribution, upstream issue/PR references, historical documentation, PGO
profile filenames, fixtures, icon resource keys and original source artwork retain
their provenance. Source icon filenames may still contain the upstream name; the
visible fruit icon has no text. Previous release wordmarks remain archived in source.

Protected dependencies, submodules, RecompiledFuncs and RecompiledPatches are not
edited. Source edits use the patch pipeline. The legacy upstream release workflow
is disabled on forks; the TGM publisher verifies draft assets/tag before publishing.
