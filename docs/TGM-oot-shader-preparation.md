# Prepare game shaders

Select a game, open **More actions**, then choose **Prepare game shaders…**.
The existing Build/Rebuild Shader Precache and manual recipe actions remain available.
The new action scans game files, assembles the supported pipeline records and compiles
them on the selected Vulkan GPU without launching a guest game or kernel.

Ocarina of Time Remake initially supports the attested v131072 main build
`E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348` in a NUP or recognized loose dump.
The title is checked from metadata rather than the game filename. Other titles can
be scanned, but native GPU preparation needs a validated profile and verifiable
game identity. A mismatched version, executable, recipe hash or cache format is
reported instead of silently substituting another profile.

The package includes a pinned metadata-only OOT profile from Fritchii v5.7. Shader
code is read locally from the player's game. The profile is under `recipes/oot/`,
so merely installing it does not change the existing quick precache's recipe search.
The source and checksums are recorded in `recipes/README.md`.

## What is counted

Unique shader programs, program combinations and pipeline records are different
counts. The same program can be compiled with several rendering/resource states.
A profile containing roughly 120,000 pipeline records is not 120,000 distinct shader
programs, and it does not prove every future runtime state is covered.

The result lists scanned files, unique programs, combinations, records prepared
now, previously prepared records, failed builds, unsupported GPU configurations,
quarantined records, unreached work, scan limits and missing recipe programs.
Unreadable archives and unrepresented programs make the coverage partial. Runtime
state validation remains enabled; unexpected configurations use normal compilation.

## Resource use and interruption

Scanning uses one archive worker, a 512 MiB per-blob limit and 1 GiB retained-program
budget. OOT's verified model shader archive inflates to 320,672,184 bytes. Assemblies
can exceed 1 GiB, so the initial operation needs free disk space and can take minutes.
GPU compilation uses at most four workers from the UI, with bounded work in flight.
Large descriptor/cache objects are allocated on the heap.

Cancel stops scheduling new work and waits for current driver calls to finish.
Completed translated records are checkpointed. A saved driver-cache fingerprint is
required before an offline job counts those records as previously prepared. Missing
driver data causes rebuilding. Driver eviction and new runtime states can still
require compilation later.

Jobs and reports are kept below the title's shader folder in `preparation-jobs/`.
Settings are copied into the worker, including per-game overrides and external
content folders; the worker does not rewrite emulator configuration. OOT's automatic
DLSS resolution policy is applied in the child so its cache identity matches launch.
Preparation locks coordinate current emulator instances; ordinary cache writes also
use the existing cache lock. Close a running game before starting preparation.

## Validation scope

Automated tests cover archive discovery/limits/cancellation, profile identity and
provenance, safe GPU key normalization, read-only setting snapshots, Unicode job
paths and worker failures. A standalone synthetic compute fixture checks first
compilation, checkpoint reuse and rebuilding after only its scratch driver cache
is deleted. It contains no game code and runs no guest process.

Real-game extraction/assembly, Windows/Linux builds and package checks are recorded
with the local release manifest. No reduction in gameplay stutter or universal
pipeline coverage is claimed without measurements from real play.

The local v131072 NUP validation scanned 186,587 files and recovered 42,848 unique
programs and 85,635 combinations. It assembled 120,124 pipeline records without read
errors or scan limits. The pinned profile had 47 unmatched programs, omitting 337
records; these gaps are reported as partial coverage. This extraction check deferred
bulk GPU compilation. A CPU replay check found no mismatched record keys, but 22
of 62,658 environments lacked storage-pointer facts. Such failures remain visible
as failed GPU preparation work and retain normal runtime compilation. The separate
synthetic GPU check does not establish that all OOT records compile on every card.
