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

I've brought across the fragment-depth fix for Vulkan devices that have to
use floating-point depth in place of 24-bit depth. I've also restored geometry
bindings when a wait submits the command buffer during draw setup.
[Lichii #2421](https://github.com/zortos293/Lichii/pull/2421),
[#2125](https://github.com/zortos293/Lichii/pull/2125)

I've made depth feedback checks use the views bound for the current draw
and made the shader cache retain write tracking for routed native stores
and atomics. The Ampere shader translator now handles half comparisons
with a constant-buffer operand. I've also kept overlapping query reports
in their original traversal order and stopped recording Vulkan indirect
dispatches with a missing buffer.
[Lichii #2357](https://github.com/zortos293/Lichii/pull/2357),
[#1832](https://github.com/zortos293/Lichii/pull/1832),
[#1511](https://github.com/zortos293/Lichii/pull/1511),
[Fritchii #72](https://github.com/Frizti/Fritchii/pull/72),
[Frizti's indirect-dispatch fix](https://github.com/Frizti/Fritchii/commit/763871b779c567cfb569747296c2989ec9dafce6)

I've matched the descriptor used by the Linux FSR 4 library we ship and
corrected a type mismatch in the vertical-blank diagnostics.
[Lichii #1595](https://github.com/zortos293/Lichii/pull/1595),
[#958](https://github.com/zortos293/Lichii/pull/958)

I've accepted page-aligned native window buffers for the second supported
block format while keeping their existing pitch and size.
[Lichii #2304](https://github.com/zortos293/Lichii/pull/2304)

I've brought across tighter checks for NRO and KIP layouts so the loaders
can reject invalid images before reading or mapping their segments.
NUP verification now streams archive members and verifies the backing data,
including when a decoded copy is already in memory.
[Lichii #2379](https://github.com/zortos293/Lichii/pull/2379),
[#2375](https://github.com/zortos293/Lichii/pull/2375),
[#2364](https://github.com/zortos293/Lichii/pull/2364)

I've fixed a short-input read in fixed-width hex parsing and kept the
processor-count query available on non-x86 hosts.
[Hex parsing fix](https://github.com/zortos293/Lichii/commit/7776a7afe7de99d376948eacc4afd1efd937f31a),
[processor-count fix](https://github.com/zortos293/Lichii/commit/e0d774c2c8)

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

I've kept the previous diagnostic lines when the log formatter runs out of
memory. [Lichii #1475](https://github.com/zortos293/Lichii/pull/1475)

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

## Further upstream fixes

I've brought across more bindless image loads, stores and atomics for Switch 2
shaders. I've also reduced descriptor work for texture tables indexed by a
bit search that is guaranteed to find a set bit. Tables with an unrestricted
reader keep their existing range.
[Lichii #2093](https://github.com/zortos293/Lichii/pull/2093),
[#1914](https://github.com/zortos293/Lichii/pull/1914)

I've deferred the directory scan for content archives until the emulator needs
the files, while keeping archive validation and update overlays working as
before. Small GPU command submissions now use inline input storage and keep
the existing command snapshot behaviour. Joy-Con polling now installs its
callbacks and publishes the initial battery level before the first report.
[Lichii #1784](https://github.com/zortos293/Lichii/pull/1784),
[#2312](https://github.com/zortos293/Lichii/pull/2312),
[#2225](https://github.com/zortos293/Lichii/pull/2225),
[Fritchii GPU submission work](https://github.com/Frizti/Fritchii/commit/8799a851451c244ff5f62802416b27583ec1e705)

I've brought across the padded shadow map fix for OOT Remake. The renderer
can reuse a square D32 image when drawing stays inside it, and keeps the
wider target when the clip reaches the padding or the cache finds an
incompatible image at the same address.
[Lichii #1868](https://github.com/zortos293/Lichii/pull/1868)

I've kept Windows fastmem page permissions intact when a mapping is split.
Vulkan now checks for completion and device loss between timed waits. NSO
loading uses the same program arguments for sizing and mapping, and can
continue when a saved argument string is too long for the loader.
[Lichii #2361](https://github.com/zortos293/Lichii/pull/2361),
[#2402](https://github.com/zortos293/Lichii/pull/2402),
[#2380](https://github.com/zortos293/Lichii/pull/2380)

I've fixed a race in OpenGL compute dispatch by holding the buffer and texture
cache locks through setup and submission. The work queue drains after those
locks are released. [Lichii #2436](https://github.com/zortos293/Lichii/pull/2436)

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
