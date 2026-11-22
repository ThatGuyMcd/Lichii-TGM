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

Frame generation remains off. Forest Lantern, handheld and Couch Mode settings,
saves and OOT aspect-ratio options are retained.

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
