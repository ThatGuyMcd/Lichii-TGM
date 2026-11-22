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

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
