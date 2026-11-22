# Lichii-tgm-v1.0.10

I've brought across four fixes from Lichii and kept our own cache safeguards.

I've corrected how texture collection handles memory spilling out of video memory,
using [Zortos's Lichii #2167](https://github.com/zortos293/Lichii/pull/2167)
([upstream commit be37029a](https://github.com/zortos293/Lichii/commit/be37029a1e6bcc1c466f2af250f678ab08b9ad92)).
A spill now requests collection without pretending that an image allocation has
failed. If the first passes free memory, collection can back off rather than
unnecessarily dropping costly textures that would need loading again. Genuine
allocation failures still take the stronger recovery path. I've kept our batched
readbacks and the safeguard that gives a newer CPU write priority over a readback.

I've fixed ownership of callable objects passed to queued work, following
[danieljesus's Lichii #2346](https://github.com/zortos293/Lichii/pull/2346)
([upstream commit 249d889b](https://github.com/zortos293/Lichii/commit/249d889b1cc7892a5b6ead91ef8d249a6e7a643d)).
The wrapper now owns a copy of an ordinary lvalue callable instead of retaining
a reference to an object that could go out of scope. Move-only work and explicitly
shared references keep their intended behaviour.

I've also brought across the sparse-texture translation guard from
[Zortos's Lichii #2360](https://github.com/zortos293/Lichii/pull/2360)
([upstream commit 4a558454](https://github.com/zortos293/Lichii/commit/4a558454622c4d99193cd727e1a2a5657b5641b1)),
originally from
[mastorok's Eglantine correction](https://github.com/Eglantine-Community/Eglantine/commit/19dfa543ecbd3e97e779ef295f079b020d81e64e).
A segment whose device address is no longer valid is skipped safely, while the
remaining valid segments keep being processed.

I've fixed DMA clears across separately mapped GPU pages, following
[RomainROCH's Lichii #1939](https://github.com/zortos293/Lichii/pull/1939)
([reviewed PR head feada72b](https://github.com/zortos293/Lichii/commit/feada72b36072485bfd9533722fff9d13f734402)).
Each mapped device-memory run is now cleared separately, so gaps between those
runs stay untouched and a later valid buffer is still reached when an earlier
run is unmapped or uncached. I've kept our readback ordering and uniform-cache
invalidation for each run.

I've kept our Forest Lantern, handheld and Couch Mode UI, every setting, saves,
data paths, OOT patches and aspect-ratio options intact. Frame generation remains
off. Both Windows and Linux AppImage downloads are included.

Thanks to the original contributors and to Lichii, Fritchii, Eglantine, Eden and
yuzu. Their copyright notices and licences remain. Shader precompile system by
Frizti (Fritchii). Shader precompile recipe by Frizti (Fritchii); the existing
OOT profile is included with redistribution permission and its original notice.
