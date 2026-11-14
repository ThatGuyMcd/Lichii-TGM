# Upstream sync, 14 November 2026

Baseline: tested/pushed `250a4b1da5dcaf7084cf95565a396e4427bcc1e6`.
Integration branch: `codex/upstream-sync-2026-11-14-0125`.
Rollback: `codex/pre-upstream-sync-2026-11-14-0125`.
Version remains the unpublished v1.0.4 candidate; published v1.0.3 is unchanged.

The fixed main snapshot is `57b75fec58d0373bf5f37556a484c1aded248c37`.
Fritchii nx2 is `f66ea633db6058efe31a9611718019e5bd1667eb` and
oot-gather-fix/v5.8.4 is `4f5c53149b7e17d1c24445f68d79a83a3b2a17f9`.
Both Fritchii tips and all 18 release tags are unchanged. Main's latest published
release remains build-177. PR metadata was captured through 01:26 UTC; actual
production diffs and prior adaptations, rather than titles or ancestry, determine coverage.

## Compatible adaptations

- Main #1686/#1563: retain existing host-resident device-memory handling, but space
  forced collections from the last actual collection. The sparse-gap regression
  fails with the old episode-relative predicate and passes with the new policy.
- Main #1687/#1172: speed hotkeys only affect a running session. Shutdown restores
  the previous speed-limit choice in the same setting scope, while dialog Apply,
  OK and Reset remain authoritative. Actual Qt settings/config persistence tests
  reproduce the old forced-on policy and pass with the adaptation.
- Open #1685: the load-end driver cache save obeys the existing concurrent-read
  capability check. Drivers that cannot read during builds retain the final save
  after worker shutdown. No driver or clock setting is changed.
- Open #1674: propagate short encrypted/aligned reads and failed installation
  writes, clear incomplete output and remove a failed newly-created install file.
  Synthetic short-I/O and valid AES-CTR/range/tail fixtures pass under ASan/UBSan.
- Open #1678: file handles share reference/cache state independently of the
  filesystem object's lifetime. The old source reproduces a heap-use-after-free
  in an actual-source ASan probe. Six adapted lifetime/concurrency/lazy-handle
  fixtures pass under ASan/UBSan, using exclusively-created private directories.
  Concurrent destruction during an active directory/rename call remains outside
  this fix, as stated in the upstream proposal.

## Reported shader preparation and launch failure

The reported candidate completed its shader preparation phases before the guest
SDK aborted while setting up a thread stack. Its log also records the final SDK
page as unmapped. The loader's sizing pass omitted the extra 0x1000 bytes of OOT
patch BSS that its real loading pass included, leaving the process reservation
one page short. Both passes now use the same patch manager for sizing. Executable
patching and dumping remain restricted to the real load, preserving NCE's existing
dry-pass text analysis and avoiding duplicate patch side effects.

A synthetic ExeFS/NPDM fixture runs the actual two-pass loader without executing
guest code. The old source reproduces the missing final SDK page; the corrected
source passes all 38 assertions under ASan/UBSan, including wrong-build gating,
module placement, final-page read/write and dry-pass side-effect checks. This
validates the allocation correction; the reported guest SDK abort still needs
confirmation in an owner-run game session.

The repeated shader screens represent separate CPU catalogue, native pipeline,
cached pipeline, graphics-stage and mesh preparation phases. In the reported log,
120124 native pipeline records were processed, graphics-stage warming took
869.5 seconds with 36823 entries, and mesh preparation finished before the panic.
The graphics warm-up reduced itself to three workers because available host
commit memory was about 8407 MiB. Completed-stage markers were saved. The broad
renderer source identity can legitimately invalidate native preparation between
candidate builds, even when a source change concerns another GPU. The loader fix
does not alter those identities, settings, journals or shader preparation policies.
No cache is deleted or marked complete without matching coverage.

## Covered or deferred

#1689's DLSS health-check registry excludes OOT; #1656 is optional trace tooling
whose deferred recorder/parser prerequisites still have concrete correctness gaps.
#1683's allowed Dynarmic-wrapper verification policy is already present; protected
Dynarmic source is unchanged. #1177's captured head is unchanged, and #1378's reuse
implementation/tests match the previously reviewed code. #1379 retains owned
detached jobs; a newly inherited Pokemon-specific null-descriptor change is not
silently credited as an OOT fix.

#1690's optional FSR resolutions need a complete TGM patch-selection/settings/
renderer contract adaptation. #1681's single-return memory-routing rewrite needs
its execution/structural fixtures adapted before import. #1694's Turing HMMA
fallback directly concerns the owner's GPU but the reported speckle has not been
reproduced locally; hardware defaults are unchanged. #1677's Intel barrier changes
need shared-memory execution validation. #1692's raw marker callback/ring ownership
is unsafe across scheduler teardown/device-loss reporting. #1631 still assumes a
depth border was cleared although the actual render area may be smaller.

Solid NSZ playback (#1675) is separate from its accepted short-I/O prerequisite:
its decompressed temporary file can approach the whole NCA size, and Linux/private
disk-exhaustion behavior needs validation before enabling it here. #1671 includes
protected FFmpeg configuration and unverified codec/format negotiation. Foreign
library layouts, Discord/server features, Switch Sports multiplayer and external
CI/Nix dependency changes do not replace TGM's retained UI/settings/build policy.
Exact heads, full diffs and dispositions are in build-local/upstream-watch/2026-11-14-0125.

## Owner startup investigation

The owner's checkpoint `1d6241eba9` log completed all shader warm-up stages and
then reported a guest SDK panic during thread-stack setup, before a first frame.
It does not establish a shader compiler deadlock or the exact cause of that panic.
This sync does not claim to fix it. The cache's conservative renderer-source
identity can cause preparation to run again after code changes; completed caches
must not be deleted or accepted under a mismatching identity as a shortcut.
No game was launched, no cache/settings were changed, and no driver/service was
modified during automated validation. No FPS improvement or crash cure is claimed.
