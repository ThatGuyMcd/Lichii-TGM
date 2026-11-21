# Lichii-tgm-v1.0.9

I've brought across the log ownership fix from
[danieljesus's Lichii #2256](https://github.com/zortos293/Lichii/pull/2256).
On Linux, each running session now owns its log until it closes, so another
session cannot move that live log into the previous-session copy. Further
sessions take a free log slot, up to eight. If a slot cannot be locked, I've
kept it out of use; when none can be owned, console and diagnostic ring logging
continue without writing to somebody else's file. Windows keeps its existing
two-slot behaviour. Missing parent folders are also created before logging starts.

I've separated the expensive per-command GPU timestamps from ordinary frame
tracing using [Lichii #1811](https://github.com/zortos293/Lichii/pull/1811), by
pabCMX. Frame tracing keeps its submission and work timings, and the performance
overlay keeps its GPU timestamps. The detailed site markers and their query pool
now require both `EDEN_DKB_FRAME_TRACE=1` and `EDEN_DKB_GPU_SITES=1` before launch.
This reduces the work added by a diagnostic trace; it doesn't change ordinary
gameplay when tracing is off.

I've corrected the SPIR-V capability declared when a shader helper selects
pointers between different storage buffers, following
[danieljesus's Lichii #2333](https://github.com/zortos293/Lichii/pull/2333).
The helper now declares full variable-pointer support for that operation, while
the branch-based path keeps its existing declarations. The existing renderer
revision checks also apply this correction when prepared shaders are rebuilt.

I've kept our Forest Lantern, handheld and Couch Mode UI, every setting, saves,
data paths, OOT patches and aspect-ratio options intact. Frame generation remains
off. Both Windows and Linux AppImage downloads are included.

Thanks to the original contributors and to Lichii, Fritchii, Eglantine, Eden and
yuzu. Their copyright notices and licences remain. Shader precompile system by
Frizti (Fritchii). Shader precompile recipe by Frizti (Fritchii); the existing
OOT profile is included with redistribution permission and its original notice.
