# Lichii-tgm releases

I publish matching Windows ZIP and Linux AppImage downloads from this fork. Each
release lists the changes I've brought across, with credit and links to the
original PRs and commits.

`CMakeModules/Branding.cmake` is the source of product/version metadata. A patch
release advances the highest published fork version once: `1.0.3` becomes `1.0.4`,
and `1.0.9` becomes `1.0.10`. A local candidate, a draft or an upstream `build-N`
tag doesn't consume another version. `tools/release/plan_nightly_release.py`
plans this from the fork's release records and the local release journal. It also
checks published timestamps, so an interrupted scheduled run can't publish twice in one
London calendar day after losing its local journal. An additional release I've
explicitly requested uses `--manual`; it still requires the next numeric patch,
an unused tag, matching packages and every publication gate. The publisher's
`manual_release` input must match the manifest. Nightly runs keep their normal
date guard. The next successful scheduled release advances the latest published
fork patch, including any separately requested manual release.
Windows Python needs the
`tzdata` package; Linux can use the system IANA timezone data.

Configure with `-DLICHII_BUILD_TAG=Lichii-tgm-v<version>` and
`-DLICHII_BUILD_PLATFORM=windows-x86_64` or `linux-x86_64`. Builds without a release
tag retain their development revision. Internal CMake targets remain unchanged.

Build the GUI, CLI and tests, then run CTest on both platforms. Windows also ships
`lichii-tgm-updater.exe`. Qt tests require the matching Qt Test library and offscreen
plugin. Test executables are excluded from release archives.

For Couch Mode, Windows deployment scans `dist/qt_themes/couch` using
`windeployqt --qmldir`. Both packages include Qt Quick, Quick Widgets, Basic
Controls, layouts and QML worker modules. I check the Couch shell and invitation
with the deployed modules as well as the development Qt installation.

On the configured local machine, `build-local/Build-All.ps1 -Tests` builds and
validates both platforms and creates matching ZIP/AppImage files. Machine-specific
toolchain setup is in the workspace `BUILD-HERE.md`; local helpers/builds are ignored.

## Release assets

- `Lichii-tgm-v<version>-windows-x86_64.zip`
- `Lichii-tgm-v<version>-linux-x86_64.AppImage`
- `SHA256SUMS` and a JSON build manifest
- `windows-ctest.xml` and `linux-ctest.xml`

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

## Before publishing

I keep games, Nintendo-owned code/artwork, firmware, keys, account credentials,
shader captures, crash dumps and personal user data out of releases. I retain the
licenses and credits for the independent code and artwork we use.

`tools/release/publication_guard.py` checks the tracked source and extracted
packages for restricted file types, executable signatures, ownership markers,
captured shader instructions and credentials. New or changed artwork needs an
exact hash and a documented origin/license in `publication-provenance.json`;
opaque profiles and recipes need a documented data classification. These checks
support provenance review; a scan alone cannot establish authorship or rights.
The source check reads protected dependencies and never changes them.

The publisher verifies a clean checkout, the next fork tag, matching package
source/version identities, checksum coverage and both CTest reports. The reports
must include all configured groups, including Couch and cache-replay groups, with
no failed or skipped groups. It checks the contents of both packages before
making the draft public. Detailed diagnostic reports stay outside the checkout
and aren't uploaded as release assets.

I write the README and release notes in my own voice: what I've changed, why it
helps and how to use it. Performance figures need measurements. I publish after
the required checks and provenance review pass.
Existing releases and tags are kept intact.

The nightly review covers every new or updated commit, PR, issue and discussion
from Lichii and Fritchii, with the historical review backlog retained. I assess
shared components and production changes even when no game is named. Actionable
bug reports get an existing correction or a source-backed fix with independent
regression coverage, followed by the same paired-build and release gates.

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
