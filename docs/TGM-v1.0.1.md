# Lichii-tgm-v1.0.1

This release retains the v1.0 Ocarina of Time Remake integration and merges all
11 commits from Lichii's `nx2` through `d2eaae4f3abca636b030b726f107bace06f61079`.
Windows and Linux packages are built from the same source commit; the release
manifest records their checks, hashes and validation limits.

## Graphics and stability changes

- [Upstream #1143](https://github.com/zortos293/Lichii/pull/1143) retains known
  storage-buffer pointer slots when a graphics pipeline is evicted and translated
  again. A temporarily null pointer can otherwise permanently remove geometry
  from that pipeline. The fork also carries this information through its prepared
  pipeline paths. This does not repair previously captured bad shader records.
- [Upstream #1154](https://github.com/zortos293/Lichii/pull/1154) corrects the
  variable shadowing introduced by #1143 for Windows builds.
- [Upstream #1147](https://github.com/zortos293/Lichii/pull/1147) keeps occlusion
  windows through 2048 slots within eight query banks, covering the 1025–1058-slot
  resets seen in the reported session. This is a bounded mitigation: windows above
  that limit still reset, and gameplay validation remains outstanding.
- Device loss discovered while a guest CPU read waits for the GPU now reports
  Vulkan device-fault diagnostics before the existing fatal exception path.
  This improves evidence collection; it does not recover a lost GPU.
- The upstream update includes translations, UI language handling and documentation.
  The fork retains its existing GPU-shutdown, host-memory diagnostic and delayed
  `nxformat.dll` loading integrations.

## Fritchii OOT review

The review also covers [Fritchii v5.5](https://github.com/Frizti/Fritchii/releases/tag/fritchii-v5.5)
and its 40 pull requests at the review snapshot. The
[comparison](TGM-Fritchii-OOT-review.md) records which changes are already covered,
newly selected, specific to Fritchii, or require further validation.

- Ported [Fritchii's texture-decode bounds checks](https://github.com/Frizti/Fritchii/commit/f52631cf66b3a5014b94c58ebb4b2e2bb938f896),
  covering ASTC edge texels and rounded unswizzle dispatches. These avoid writes
  outside destination images; Fritchii reports OOT corruption and GPU faults on AMD.
- NVIDIA keeps native noperspective interpolation in both shader stages, following
  [Fritchii's NVIDIA fog regression report](https://github.com/Frizti/Fritchii/commit/9b30eec8ca).
  The existing AMD clipping correction remains. Paired-stage SPIR-V tests check both
  paths, and the prepared-code identity changes so old prepared shaders are rebuilt.
  The affected scenes still need a hardware retest.
- The inline shader-emission path keeps its verification reference when the queued-IR
  budget is exhausted, avoiding a second emission of mutated IR in diagnostic mode.
- The buffer cache's post-copy barrier also orders later transfers, closing the
  downloads/gathers path identified in [Fritchii's transfer-order fix](https://github.com/Frizti/Fritchii/commit/15f8e4c5c299032b094a48eb6787eed0437a918d).
- Host-sync requests arriving after GPU shutdown are rejected, so a late GPU tick
  cannot execute callbacks whose waiting stack frames have already returned.
- Inline QMD launches mirror their implicit compute-register update on the split's
  front, so later macros read the same state as the back. A production front/back
  macro regression fails on the old code and passes for mapped/control boundaries.
- The sysmix audio queue adapts to the host callback size, within its ring capacity,
  using the focused change from [Fritchii #22](https://github.com/Frizti/Fritchii/pull/22).
  The old three-block queue could not feed a 1,024-frame PipeWire callback. Playback,
  admission-limit and surround-capacity tests cover the change.
- The AppImage has a relocatable Qt launcher, Wayland client deployment and an explicit
  X11 preference that takes precedence over a desktop's platform environment, adapted
  from [Fritchii #37](https://github.com/Frizti/Fritchii/pull/37) and
  [#39](https://github.com/Frizti/Fritchii/pull/39).

The review does not establish a frame-rate gain on the reported PC. The NVIDIA
system blue screen and damaged game-data findings below still apply.

## Missing fairy-fountain rooms and grottos

A read-only check confirmed that the installed fairy-fountain normal-map member
`/Tex/CmnTex_Map_Soil_FairyFountain_Floor_D_Nrm.bntx.zs` starts with 12288 zero bytes
instead of a Zstandard frame. This matches the damaged member diagnosed in
[upstream issue #420](https://github.com/zortos293/Lichii/issues/420#issuecomment-6708200568).
The upstream investigation found that the game then fails to create the dependent
room model, while collision can remain.

A renderer update cannot reconstruct the missing original bytes. Re-dump or verify
the affected game/update from your own console, retaining a backup of the existing
files. This release does not replace the texture with a different material. Other
missing grotto geometry needs scene validation after the data defect is corrected;
the confirmed fairy-fountain defect alone does not establish every missing surface's
cause.

## Crash investigation and remaining limits

The application dump identifies `VK_ERROR_DEVICE_LOST` in the GPU semaphore wait
reached by a guest CPU readback. Independently, Windows Error Reporting recorded a
`0x7F`, trap `0x8` blue screen with the bucket
`nvlddmkm!CNvLAdapter::queryNodePerfData` and stack-usage classification.
The recovered kernel stack shows a hardware-monitoring service querying GPU node
performance through `DxgkQueryStatistics` / `GetNodePerfData`, followed by extensive
NVIDIA kernel-driver stack use. This is a concrete lead for a monitoring/driver
interaction; it does not establish the original driver defect or prove that every
reported application crash has the same cause.

The two reports do not prove an out-of-memory condition or a DLSS fault, and this
release does not claim to fix the system blue screen. A driver-level investigation
and a controlled gameplay retest are still needed. No Windows, driver, game-data
or controller settings were changed as part of this release.

Automated tests and package checks are not gameplay or hardware validation. The
release manifest records completed checks; private logs, dumps and game assets are
not included in the release.
