# Lichii-tgm-v1.0.8

I've stopped recovery launches from rewriting an unchanged Vulkan driver cache before the game
starts. Normal launches now save that snapshot on the existing background worker, keeping the
driver safety checks and later cache saves intact. This adapts the startup-save component of
[Frizti's Fritchii change](https://github.com/Frizti/Fritchii/commit/8b44122bb2fc54c21cc1b5595f77a812cf27ae74).

I've reduced repeated work for disabled vertex streams using the relevant part of
[Lichii #2215](https://github.com/zortos293/Lichii/pull/2215). Disabled streams leave
the dirty list and binding mask straight away; enabling them resolves and binds
their buffers through the existing path.

I've brought across the Windows memory-query component of
[#2173](https://github.com/zortos293/Lichii/pull/2173), using the process's available
commit value, and the bounded zero-page cleanup from
[#2233](https://github.com/zortos293/Lichii/pull/2233). Cleanup requests stop at
1 MiB and keep the existing page-by-page fallback.

I've fixed an expired-event loop using
[#2252](https://github.com/zortos293/Lichii/pull/2252), so a timer whose owner has
gone away cannot prevent the next live timer from running. I've also isolated
regression-test logging before tests change paths, preserving gameplay logs.

I've kept our Forest Lantern and Couch Mode UI, every setting, saves, data paths,
OOT patches and aspect-ratio options intact. Frame generation remains off.

I've tightened the orphaned-profile scan using
[danieljesus's Lichii #2265](https://github.com/zortos293/Lichii/pull/2265). It leaves
folders without a complete user ID alone and keeps unreadable profile data intact.
An inaccessible folder is treated as containing data, and a failed cleanup is logged.

I've added the two zero-length copy guards from
[Lichii #2282](https://github.com/zortos293/Lichii/pull/2282), covering scratch-buffer
growth and empty NUP metadata. Empty copies now leave their buffers alone without
passing null pointers to `memcpy`; normal copying keeps its existing behaviour.

Thanks to the Lichii contributors for the original fixes and to Fritchii,
Eglantine, Eden and yuzu. Their original copyright notices and licences remain.
Shader precompile recipe by Frizti (Fritchii); the existing OOT profile is included
with redistribution permission and its original notice.
