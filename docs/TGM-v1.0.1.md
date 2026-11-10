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
The kernel dump could not be read with the current process permissions, so the
full kernel stack and original driver trigger have not been established.

The two reports do not prove an out-of-memory condition or a DLSS fault, and this
release does not claim to fix the system blue screen. A driver-level investigation
and a controlled gameplay retest are still needed. No Windows, driver, game-data
or controller settings were changed as part of this release.

Automated tests and package checks are not gameplay or hardware validation. The
release manifest records completed checks; private logs, dumps and game assets are
not included in the release.
