# Lichii-tgm-v1.0.5

I've brought Couch Mode into this update, along with another round of shared fixes from upstream. I've kept our Forest Lantern UI, handheld layout, settings, saves and OOT add-ons intact.

## Couch Mode

I've added a full-screen library and settings layout for playing from the sofa, using a controller or a touchscreen. It includes game collections, search, settings navigation and theme packs. The settings still use the emulator's original controls and apply paths.

On first launch, you can choose **Switch now** or **Later**, with **Never ask again** if you don't want the invitation to return. You can also open it from **View → Couch Mode** or **Ctrl+Shift+C**. Starting in Couch Mode is a separate preference, so trying it once won't change how the emulator starts next time.

There's more information in the [Couch Mode guide](CouchMode.md).

## Shared fixes

I've adapted these upstream components to our fork, keeping our existing cache ownership and settings:

- Texture-handle interpretation and cache migration: [Lichii #2051](https://github.com/zortos293/Lichii/pull/2051), based on [Eglantine #900](https://github.com/Eglantine-Community/Eglantine/pull/900).
- Texture cleanup in the image's owning GPU address space: [#2062](https://github.com/zortos293/Lichii/pull/2062).
- Bounded reads for large shader-cache logs: [#2110](https://github.com/zortos293/Lichii/pull/2110).
- Complete, atomic Vulkan pipeline-cache snapshots and fallback when a driver rejects a saved cache: [#2100](https://github.com/zortos293/Lichii/pull/2100).
- Shader-job waiters can pick up their own ready driver work: [#2098](https://github.com/zortos293/Lichii/pull/2098).
- Syncpoint polling keeps its notification history separate from the immediate value mirror: [#2052](https://github.com/zortos293/Lichii/pull/2052).
- VP9 range-encoder carry handling: [#2070](https://github.com/zortos293/Lichii/pull/2070).
- Native texture tiling in uploads and software blits: [#2085](https://github.com/zortos293/Lichii/pull/2085) and [#2121](https://github.com/zortos293/Lichii/pull/2121).
- Safer package metadata parsing, preserving useful missing-key and program errors: [#2061](https://github.com/zortos293/Lichii/pull/2061), with isolated synthetic key-generation coverage informed by [#2068](https://github.com/zortos293/Lichii/pull/2068).
- Register bounds for the existing LZ texture-load lowering: [#2132](https://github.com/zortos293/Lichii/pull/2132).
- Correct handling of the default IP protocol on Windows, avoiding an incorrect unsupported-protocol error: [#2058](https://github.com/zortos293/Lichii/pull/2058).
- Game-list patch and DLC labels refresh on each scan rather than reusing stale text from disk: [#2095](https://github.com/zortos293/Lichii/pull/2095), addressing [#2088](https://github.com/zortos293/Lichii/issues/2088).

These are specific components from those changes, with credit to their upstream authors. I've kept frame generation off and retained our independent shader regressions.

## Downloads and nightly updates

I've included matching Windows x86-64 and Linux AppImage builds. Both carry the same source version, with checksums and complete automated test receipts alongside them.

I've also expanded the nightly routine to review commits, PRs and issues across both upstream forks, including shared fixes that don't mention OOT. Each successful nightly release advances the patch version; the next one after this is v1.0.6.

[All source changes since v1.0.4](https://github.com/ThatGuyMcd/Lichii-TGM/compare/Lichii-tgm-v1.0.4...Lichii-tgm-v1.0.5).

Thanks to the Lichii, Fritchii, Eglantine, Eden and yuzu contributors. Their original copyright notices and licences remain. Games, keys and firmware are not included.
