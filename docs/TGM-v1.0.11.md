# Lichii-tgm-v1.0.11

I've brought across the ASTC scratch batching fix from Zortos. Big texture
arrays start with a batch sized for the existing scratch pool, so they can avoid
asking for one giant allocation before trying smaller batches. [Lichii #2393](https://github.com/zortos293/Lichii/pull/2393)

I've also brought across Zortos's texture-budget correction. When a driver
reports two slightly different budgets in the same frame, the texture cache
only adds the usage from other allocations if the integrated GPU is using a
capped share of system memory. [Lichii #2407](https://github.com/zortos293/Lichii/pull/2407)

I've fixed the kernel's main and idle thread cleanup on shutdown, using
danieljesus's change. Each core releases its threads before the schedulers are
torn down. [Lichii #2257](https://github.com/zortos293/Lichii/pull/2257)

I've brought across Zortos and capy-ai's fix for reusing driver pipeline binaries
within the same session on drivers where Lichii-tgm manages those binaries.
Shader cache removal now leaves an entry in the log. [Lichii #2349](https://github.com/zortos293/Lichii/pull/2349)

I've included the shader identifier in compute pipeline binding logs when
GPU logging is enabled, using Zortos's change. [Lichii #2347](https://github.com/zortos293/Lichii/pull/2347)

I've corrected how Maxwell shaders handle a zero factor with FMZ. Fused
multiply-add instructions keep their addend before applying saturation, and
half-precision comparisons now use matching types. [Lichii #811](https://github.com/zortos293/Lichii/pull/811)

I've brought across the file and storage request bounds checks. A request for
more bytes than its mapped buffer now returns an invalid size error before
calling the filesystem. [Lichii #973](https://github.com/zortos293/Lichii/pull/973)

I've brought across luimu64's documentation corrections and added the Markdown
link checker. [Fritchii #68](https://github.com/Frizti/Fritchii/pull/68), [#69](https://github.com/Frizti/Fritchii/pull/69), [#71](https://github.com/Frizti/Fritchii/pull/71)

I've fixed pitch-linear texture uploads so a buffer that is too short doesn't
produce a partial upload. This builds on azeselh's bounds fix, brought into
Lichii by Zortos. [Lichii #2414](https://github.com/zortos293/Lichii/pull/2414), [Eglantine #925](https://github.com/Eglantine-Community/Eglantine/pull/925)

Frame generation is off by default. Forest Lantern, handheld and Couch Mode settings,
saves and OOT aspect-ratio options are retained.

## Renderer and shader fixes

I've made Vulkan retry adapter discovery if the list changes while it's being
read. If the list shrinks, we use just the devices Vulkan returned. [Lichii #2223](https://github.com/zortos293/Lichii/pull/2223)

I've fixed shader lookups for OOT's unassigned texture descriptors, including
the same lookup from a shader worker's saved copy of the descriptor pool. [Lichii #2220](https://github.com/zortos293/Lichii/pull/2220)

I've brought across the command-stream fix that lets a split register write
resume after an interleaved GPU command list. I've also skipped empty texture
work on cache hits and fixed the SMAA edge pass so a moving edge clears properly.
Large texture uploads now initialise the whole image before sending it in chunks.
[Lichii #2415](https://github.com/zortos293/Lichii/pull/2415),
[#910](https://github.com/zortos293/Lichii/pull/910),
[#1945](https://github.com/zortos293/Lichii/pull/1945),
[#2321](https://github.com/zortos293/Lichii/pull/2321)

I've brought across Rhys Denno's shader work for Hyrule Warriors and kept the
existing depth and gather paths. I've also corrected full-width bit extraction,
deep address expressions, and missing-value messages when a shader cache record
is incomplete.
[Lichii #2416](https://github.com/zortos293/Lichii/pull/2416),
[Eglantine #928](https://github.com/Eglantine-Community/Eglantine/pull/928),
[#1825](https://github.com/zortos293/Lichii/pull/1825),
[#2301](https://github.com/zortos293/Lichii/pull/2301),
[#2401](https://github.com/zortos293/Lichii/pull/2401)

I've fixed NCA verification hanging when a read stops returning data, and made
NUP reject raw entries whose stored and declared sizes differ. [Lichii #2372](https://github.com/zortos293/Lichii/pull/2372), [#1922](https://github.com/zortos293/Lichii/pull/1922)

I've reduced the work the CPU does when decoding ASTC textures by expanding
colour endpoints once per partition. The decoder outputs match the previous
decoder on the block regression fixtures.
[Lichii #1859](https://github.com/zortos293/Lichii/pull/1859)

## Loading and storage

I've fixed Windows file replacement when the emulator still holds a cached
handle. Open files retain their identity across rename, and an external lock
still returns a lock error to the game. I've also kept registered updates when
a game-list rescan is cancelled.
[Lichii #2420](https://github.com/zortos293/Lichii/pull/2420),
[#2298](https://github.com/zortos293/Lichii/pull/2298)

I've cut out a copy when loading NSO segments and added a size check before
allocating for LZ4 data. I've kept empty IPSwitch records from writing bytes,
reused AES-CTR contexts for repeated reads, and given concurrent SD archive
parsers independent HMAC contexts. The core also starts with an empty content
provider for tools that run without a frontend.
[Lichii #2403](https://github.com/zortos293/Lichii/pull/2403),
[#2371](https://github.com/zortos293/Lichii/pull/2371),
[#1855](https://github.com/zortos293/Lichii/pull/1855),
[#1968](https://github.com/zortos293/Lichii/pull/1968),
[#2284](https://github.com/zortos293/Lichii/pull/2284)

## Audio, multiplayer and desktop

I've kept the UI volume live after boot, enabled SDL3 5.1 for supported Switch 2
playback when Surround is selected, and corrected how Switch 2 games clear
queued output audio. I've also fixed the room subnet mask, restored saved
multiplayer IP bans on startup, and corrected the SSL read-ahead and notification
service responses.
[Lichii #1847](https://github.com/zortos293/Lichii/pull/1847),
[#2417](https://github.com/zortos293/Lichii/pull/2417),
[#2299](https://github.com/zortos293/Lichii/pull/2299),
[#2418](https://github.com/zortos293/Lichii/pull/2418),
[#1788](https://github.com/zortos293/Lichii/pull/1788),
[#2016](https://github.com/zortos293/Lichii/pull/2016),
[#1948](https://github.com/zortos293/Lichii/pull/1948),
[#2224](https://github.com/zortos293/Lichii/pull/2224),
[#1851](https://github.com/zortos293/Lichii/pull/1851)

I've updated Windows HDR state detection, improved Qt and GPU diagnostics, and
kept the existing Chinese settings translations with readable GPU fence help.
The pipeline builder trace can also show how many SPIR-V words it sent to Vulkan.
I've also brought across build and test fixes for shader prebuilding, audio
queues, storage pointer tracking and Apple power queries.
[Lichii #2176](https://github.com/zortos293/Lichii/pull/2176),
[#1268](https://github.com/zortos293/Lichii/pull/1268),
[#1826](https://github.com/zortos293/Lichii/pull/1826),
[#2293](https://github.com/zortos293/Lichii/pull/2293),
[#2155](https://github.com/zortos293/Lichii/pull/2155),
[#1759](https://github.com/zortos293/Lichii/pull/1759),
[#1762](https://github.com/zortos293/Lichii/pull/1762),
[#1852](https://github.com/zortos293/Lichii/pull/1852),
[#2398](https://github.com/zortos293/Lichii/pull/2398),
[Fritchii #46](https://github.com/Frizti/Fritchii/pull/46)

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
