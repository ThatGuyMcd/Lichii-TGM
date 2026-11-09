# Ocarina of Time PR audit

Audit snapshot: 2026-11-09. Reviewed 527 PRs, including open, merged and closed PRs. The Ocarina/OoT/game-ID search, including conversations, matched 305 PRs: 115 open, 130 already merged and 60 closed without merging.

This is an experimental Windows x64 integration. A successful compilation and unit-test run does not establish gameplay correctness or performance. No game files were supplied and no emulator/gameplay session was started.

Original PR heads, source state and per-PR decisions are also saved in [audit.json](audit.json). A reconciled PR can contribute unique behavior without importing an obsolete or competing implementation verbatim. See [GPU/CPU decisions](../OOT_EXPERIMENTAL_INTEGRATION.md).

| PR | State at audit | Disposition | Title |
|---|---|---|---|
| [#1032](https://github.com/zortos293/Lichii/pull/1032) | open | reconciled | maxwell_3d: skip the back drain on split macro DMA reads of report slots (Ocarina of Time Kokiri 42 → 47 fps) |
| [#1028](https://github.com/zortos293/Lichii/pull/1028) | open | merged | video_core: async read-back: the reading thread waits for its downloads (port of #805's read-back) |
| [#1023](https://github.com/zortos293/Lichii/pull/1023) | open | merged | texture_cache: delete unused fresh render targets early while over budget (Ocarina Market device loss on 8 GB) |
| [#1021](https://github.com/zortos293/Lichii/pull/1021) | open | merged | video_core: prepare native shaders before gameplay from a recipe (builds on #895, #896) |
| [#1016](https://github.com/zortos293/Lichii/pull/1016) | merged | upstream-merged | core_timing: sleep the HostTiming thread in the OS instead of a hardware wait |
| [#1008](https://github.com/zortos293/Lichii/pull/1008) | open | merged | shader: prove Ocarina's per-tap gathers and call links through IMAD, USHF and uniform LEA |
| [#1005](https://github.com/zortos293/Lichii/pull/1005) | open | merged | common: keep a faulting stack walk from taking the sampler's process down |
| [#1002](https://github.com/zortos293/Lichii/pull/1002) | open | merged | nx2: Windows host-timing sleep, background pinning and pacing override |
| [#999](https://github.com/zortos293/Lichii/pull/999) | open | merged | vulkan: skip precache builds a driver crash ended the last precache on |
| [#998](https://github.com/zortos293/Lichii/pull/998) | open | merged | vulkan: build native task/mesh pipelines a few at a time |
| [#995](https://github.com/zortos293/Lichii/pull/995) | open | merged | vulkan: compile pointer-slot compute rebuilds on a worker instead of waiting in the dispatch |
| [#994](https://github.com/zortos293/Lichii/pull/994) | open | merged | vulkan: keep unused graphics pipelines until the disk cache size is exceeded (desktop) |
| [#991](https://github.com/zortos293/Lichii/pull/991) | closed | merged | nx2: pace movie playback at display refresh and show FPS in titles |
| [#989](https://github.com/zortos293/Lichii/pull/989) | closed | merged | yuzu_cmd: show the game name and FPS in the SDL window title |
| [#980](https://github.com/zortos293/Lichii/pull/980) | open | reconciled | video_core: draw GPU-written mesh task counts without reading them on the front |
| [#979](https://github.com/zortos293/Lichii/pull/979) | merged | upstream-merged | video_core: restore the Switch 2 semaphore mirror, shadowing words with direct releases |
| [#978](https://github.com/zortos293/Lichii/pull/978) | closed | already-present | video_core: restore the Switch 2 semaphore mirror lost in f6a8f51324 |
| [#976](https://github.com/zortos293/Lichii/pull/976) | open | merged | Ocarina of Time: fix the Castle Town guard house freeze and its device-memory blowup (#247, #449) |
| [#975](https://github.com/zortos293/Lichii/pull/975) | merged | upstream-merged | video_core: EDEN_NX2_STUTTER_TRACE and the shader cache bench harness |
| [#974](https://github.com/zortos293/Lichii/pull/974) | open | reconciled | video_core: keep per-draw writes that change no resolve from breaking fast draws |
| [#971](https://github.com/zortos293/Lichii/pull/971) | open | merged | vulkan: skip native draws without a depth target only while many pipelines are pending |
| [#970](https://github.com/zortos293/Lichii/pull/970) | open | merged | video_core: read texture descriptors without flushing during shader translation |
| [#969](https://github.com/zortos293/Lichii/pull/969) | open | reconciled | vulkan: keep dispatching while a native compute pipeline is rebuilt (Ocarina of Time) |
| [#967](https://github.com/zortos293/Lichii/pull/967) | open | merged | build: allow linking a local clang_rt.builtins with clang-cl |
| [#965](https://github.com/zortos293/Lichii/pull/965) | merged | upstream-merged | nx2_profile: GPU waits by zone, present and submit zones, lock contention; opt-in live-read check |
| [#961](https://github.com/zortos293/Lichii/pull/961) | merged | upstream-merged | common: trim all-zero guest pages unconditionally (builds on #431) |
| [#960](https://github.com/zortos293/Lichii/pull/960) | open | merged | android: show the Ocarina of Time anti-aliasing option |
| [#959](https://github.com/zortos293/Lichii/pull/959) | open | merged | vulkan: allocate a buffer over the memory budget instead of throwing |
| [#956](https://github.com/zortos293/Lichii/pull/956) | open | reconciled | puller: decide met semaphore acquires without draining the split's back (stacked on #243) |
| [#955](https://github.com/zortos293/Lichii/pull/955) | open | reconciled | vk_buffer_cache: skip vertex buffer binds still bound in the command buffer |
| [#954](https://github.com/zortos293/Lichii/pull/954) | open | reconciled | buffer_cache: keep the resolved index buffer while its range, mappings and buffers are |
| [#953](https://github.com/zortos293/Lichii/pull/953) | open | reconciled | nx2 profile: measure the fast method path, one method zone per batch |
| [#951](https://github.com/zortos293/Lichii/pull/951) | merged | upstream-merged | video_core: EDEN_NX2_MEM_TRACE memory trace; log texture cache thresholds only when they change |
| [#950](https://github.com/zortos293/Lichii/pull/950) | open | merged | vulkan: create colour images without DCC on RADV before GFX10 (Ocarina of Time on RX 580) |
| [#948](https://github.com/zortos293/Lichii/pull/948) | open | merged | vulkan: keep uneven-mip ASTC in BC on integrated GPUs and cards under 6 GiB |
| [#947](https://github.com/zortos293/Lichii/pull/947) | merged | upstream-merged | file_sys: harden PFS0, RomFS, NCA and XCI parsers against malformed files |
| [#946](https://github.com/zortos293/Lichii/pull/946) | open | merged | docs: note Bluetooth HFP input can silence output on Linux |
| [#941](https://github.com/zortos293/Lichii/pull/941) | open | merged | vulkan: keep NVIDIA DLSS on when Streamline warns that the VRAM budget is used up |
| [#933](https://github.com/zortos293/Lichii/pull/933) | open | reconciled | vk_rasterizer: poll a suspended list copy without a kernel call each time |
| [#929](https://github.com/zortos293/Lichii/pull/929) | open | merged | nx2 video: cap dav1d threads by a fixed reservation, not a halving |
| [#927](https://github.com/zortos293/Lichii/pull/927) | open | merged | qt: Adventure Notes, a progress panel that reads the save and follows the running game |
| [#922](https://github.com/zortos293/Lichii/pull/922) | open | merged | vulkan: place 8-64 MiB images in a pool of 256 MiB blocks |
| [#919](https://github.com/zortos293/Lichii/pull/919) | open | merged | BoxArt Switch 2 |
| [#918](https://github.com/zortos293/Lichii/pull/918) | merged | upstream-merged | common: make Log2Ceil(0) the smallest bucket instead of wrapping |
| [#917](https://github.com/zortos293/Lichii/pull/917) | closed | reconciled | Ocarina of Time performance work (#717 by @stephenjayakar) on current nx2, plus a blend-state fix for AMD |
| [#916](https://github.com/zortos293/Lichii/pull/916) | open | merged | vk_scheduler: time sampled GPU frames by the source line that recorded each command |
| [#915](https://github.com/zortos293/Lichii/pull/915) | open | merged | Ocarina: AMD FSR 4 through an FFX API upscaler library (builds on #695) |
| [#914](https://github.com/zortos293/Lichii/pull/914) | open | merged | Ocarina FSR: camera jitter and texture LOD bias (builds on #695) |
| [#913](https://github.com/zortos293/Lichii/pull/913) | open | merged | common: EDEN_GPU_PIN_EXCLUSIVE=1 keeps other threads off the pinned GPU threads' cores (Linux) |
| [#912](https://github.com/zortos293/Lichii/pull/912) | merged | upstream-merged | nx2 video: leave the dav1d thread count at its default |
| [#911](https://github.com/zortos293/Lichii/pull/911) | open | reconciled | video_core: let fence-delayed semaphore releases meet acquires before their write (OoT channel stalls since #337) |
| [#910](https://github.com/zortos293/Lichii/pull/910) | merged | upstream-merged | texture_cache: don't prepare the null image on a texture memo hit (Ocarina of Time) |
| [#909](https://github.com/zortos293/Lichii/pull/909) | open | reconciled | buffer_cache: skip the once-a-frame native bank upload for Ocarina of Time |
| [#903](https://github.com/zortos293/Lichii/pull/903) | open | merged | Ocarina: NVIDIA DLSS mode setting (Balanced / Performance / Ultra Performance) |
| [#900](https://github.com/zortos293/Lichii/pull/900) | open | reconciled | [Proof of concept] Shader precompile from game files: per-stage translation cache checked on every use (see #899) |
| [#896](https://github.com/zortos293/Lichii/pull/896) | open | reconciled | Draw precompiled pipelines without translating them on the GPU thread (Ocarina of Time) |
| [#895](https://github.com/zortos293/Lichii/pull/895) | open | reconciled | Precompile a game’s shader pipelines before play, from shareable recipes (Switch 2) |
| [#893](https://github.com/zortos293/Lichii/pull/893) | open | merged | dist: add the missing nx2 translation contexts to es.ts |
| [#883](https://github.com/zortos293/Lichii/pull/883) | open | merged | pipeline_cache: merge compatible older records into an existing newer cache |
| [#881](https://github.com/zortos293/Lichii/pull/881) | merged | upstream-merged | audio: capture silence when the host microphone cannot open or start |
| [#880](https://github.com/zortos293/Lichii/pull/880) | merged | upstream-merged | vulkan: fail a backable texture cache image that does not fit instead of leaving it unbacked |
| [#879](https://github.com/zortos293/Lichii/pull/879) | merged | upstream-merged | nx2 video: pick libdav1d for AV1 explicitly, cap its threads, log the decoder choice |
| [#878](https://github.com/zortos293/Lichii/pull/878) | merged | upstream-merged | vulkan: free compute IR before a synchronous driver compile (precache memory) |
| [#876](https://github.com/zortos293/Lichii/pull/876) | open | merged | Fix Intel Vulkan shader routing and portable-cache reuse |
| [#875](https://github.com/zortos293/Lichii/pull/875) | open | merged | vk_pipeline_cache_prebuild: build cached pipelines newest first (from Fritchii) |
| [#874](https://github.com/zortos293/Lichii/pull/874) | open | reconciled | query_cache: keep fence-flagged payload releases synchronized at low accuracy (#337 perf regression) |
| [#870](https://github.com/zortos293/Lichii/pull/870) | open | merged | common, dynarmic: name JIT blocks by guest PC in the Windows sampler |
| [#869](https://github.com/zortos293/Lichii/pull/869) | open | reconciled | core: decompress Ocarina of Time's zstd on the host, streamed blocks … |
| [#865](https://github.com/zortos293/Lichii/pull/865) | open | merged | vulkan, shader: draw points and lines through the mesh compute fallback (Ocarina of Time foliage shadows) |
| [#861](https://github.com/zortos293/Lichii/pull/861) | open | reconciled | buffer_cache: preflush native titles' GPU readbacks, waiting for downloads in flight |
| [#854](https://github.com/zortos293/Lichii/pull/854) | open | merged | shader: turn off Adreno LRZ on the Qualcomm driver (Ocarina of Time 8x8 holes) |
| [#841](https://github.com/zortos293/Lichii/pull/841) | merged | upstream-merged | vulkan: build cached task/mesh pipelines at boot without VK_EXT_mesh_shader |
| [#840](https://github.com/zortos293/Lichii/pull/840) | open | merged | video_core: Add native Linux DLSS, DLAA quality, and neural rendering |
| [#836](https://github.com/zortos293/Lichii/pull/836) | open | merged | vulkan: recycle images instead of creating and freeing them every frame (Ocarina of Time) |
| [#831](https://github.com/zortos293/Lichii/pull/831) | open | reconciled | Ocarina of Time on RDNA 4 and Zen 5: 224 fps VRR cap, AMD FSR 4 and frame generation, pacing, loads, HDR, 21:9 (draft, tracking) |
| [#823](https://github.com/zortos293/Lichii/pull/823) | open | merged | texture_cache: let an integrated GPU's memory share bound the textures only (#739) |
| [#820](https://github.com/zortos293/Lichii/pull/820) | merged | upstream-merged | shader: interpolate noperspective varyings perspective-correct (OoT tomb red line, port of #722) |
| [#818](https://github.com/zortos293/Lichii/pull/818) | merged | upstream-merged | texture_cache: don't upload stale guest memory into new images (Switch 2 titles) |
| [#817](https://github.com/zortos293/Lichii/pull/817) | merged | upstream-merged | texture_cache: skip uploads a join copy overwrites whole |
| [#816](https://github.com/zortos293/Lichii/pull/816) | merged | upstream-merged | video_core: mark native compute storage bindings only as far as the shader's stores reach |
| [#815](https://github.com/zortos293/Lichii/pull/815) | merged | upstream-merged | video_core, vulkan: drop only damaged, stale or duplicate pipeline cache records |
| [#814](https://github.com/zortos293/Lichii/pull/814) | open | merged | vulkan: draw task/mesh through the compute fallback on AMD drivers, with a Mesh Shaders setting (GPU hangs, #316, #871) |
| [#812](https://github.com/zortos293/Lichii/pull/812) | merged | upstream-merged | shader precache: discover every container the archive walker reads |
| [#810](https://github.com/zortos293/Lichii/pull/810) | open | merged | common: let HostMemory::Protect share the placeholder lock (Ocarina of Time) |
| [#809](https://github.com/zortos293/Lichii/pull/809) | merged | upstream-merged | nx2: keep refused decoder packets and stop the movie clock running ahead (#659 follow-up) |
| [#807](https://github.com/zortos293/Lichii/pull/807) | open | merged | vulkan: explain out-of-host-memory pipeline failures (Windows commit limit, #462) |
| [#806](https://github.com/zortos293/Lichii/pull/806) | open | merged | shader_environment: read NVN's draw parameters from the draw (Ocarina of Time vegetation on AMD) |
| [#805](https://github.com/zortos293/Lichii/pull/805) | open | merged | video_core: decouple the split GPU threads — Ocarina of Time holds 60 fps on AMD, plus fixes for three older races |
| [#804](https://github.com/zortos293/Lichii/pull/804) | open | reconciled | vk_pipeline_cache: migrate version 53 and 54 shader caches to version 55's graphics keys |
| [#802](https://github.com/zortos293/Lichii/pull/802) | open | reconciled | buffer_cache: upload each byte of native uniform banks once a frame |
| [#800](https://github.com/zortos293/Lichii/pull/800) | open | reconciled | video_core: NVIDIA DLSS on Linux through NGX |
| [#797](https://github.com/zortos293/Lichii/pull/797) | open | merged | vulkan: frame generation from NVIDIA's optical flow hardware |
| [#790](https://github.com/zortos293/Lichii/pull/790) | open | merged | graphics: clarify DLSS settings and improve the Graphics layout |
| [#785](https://github.com/zortos293/Lichii/pull/785) | open | merged | core_timing: don't scale the guest counter for CPU Boost/Overclock in Switch 2 titles (fixes #317) |
| [#784](https://github.com/zortos293/Lichii/pull/784) | open | merged | textures: swizzle and unswizzle block linear in 16-byte units from a per-GOB offset table |
| [#783](https://github.com/zortos293/Lichii/pull/783) | open | merged | vulkan: fix frame generation on high refresh rate monitors |
| [#781](https://github.com/zortos293/Lichii/pull/781) | merged | upstream-merged | Precache shaders before game start (compute, from #579/#741) |
| [#779](https://github.com/zortos293/Lichii/pull/779) | open | merged | vulkan: run guest HMMA on Intel Xe2 matrix units (Arc B580) |
| [#778](https://github.com/zortos293/Lichii/pull/778) | merged | upstream-merged | Import from Frizti's Fritchii v4: OoT water flicker, staging buffer reuse, per-method switch reads |
| [#769](https://github.com/zortos293/Lichii/pull/769) | merged | upstream-merged | common: pin the split GPU threads to their own cores by default |
| [#768](https://github.com/zortos293/Lichii/pull/768) | open | merged | macOS: fix device loss on Ocarina save load and shader-compile stutter on MoltenVK |
| [#761](https://github.com/zortos293/Lichii/pull/761) | open | reconciled | vulkan: prewarm predicted mesh pipelines and reuse task/mesh libraries |
| [#756](https://github.com/zortos293/Lichii/pull/756) | merged | upstream-merged | common: mark cold guest memory with MADV_COLD instead of MADV_PAGEOUT (Linux 3-15 s freezes) |
| [#752](https://github.com/zortos293/Lichii/pull/752) | merged | upstream-merged | video_core: let a channel wait for a GPU-written command list without blocking the others |
| [#727](https://github.com/zortos293/Lichii/pull/727) | merged | upstream-merged | nx2: stub vi2 command 201 and nifm command 101 (#653) |
| [#726](https://github.com/zortos293/Lichii/pull/726) | closed | merged | docs(linux): document experimental DLSS 5 Vulkan-layer setup |
| [#722](https://github.com/zortos293/Lichii/pull/722) | closed | already-present | shader: interpolate noperspective varyings perspective-correct (Ocarina of Time tomb red line, #441) |
| [#721](https://github.com/zortos293/Lichii/pull/721) | merged | upstream-merged | video_core: replace Ocarina's movie network by an input copy (fixes #676) |
| [#718](https://github.com/zortos293/Lichii/pull/718) | merged | upstream-merged | vk_query_cache: raise bank-bound occlusion query reset cap to 4 banks (Ocarina of Time) |
| [#717](https://github.com/zortos293/Lichii/pull/717) | open | reconciled | nx2: Ocarina of Time performance work (split GPU thread, caches, JIT, kernel), squashed on build 116 |
| [#715](https://github.com/zortos293/Lichii/pull/715) | open | merged | [Bugfix] nx2:vi2: copy Ocarina's UI storage at each present while it runs a frame ahead (#286) |
| [#714](https://github.com/zortos293/Lichii/pull/714) | merged | upstream-merged | [Bugfix] video_core: read diagnostic switches after nx2_switches.txt loads |
| [#700](https://github.com/zortos293/Lichii/pull/700) | merged | upstream-merged | video_core: invalidate every block a cached GPU write touches, stop query walks early (all games) |
| [#699](https://github.com/zortos293/Lichii/pull/699) | merged | upstream-merged | vk_scheduler: keep drawing in the render pass a deferred clear began (Ocarina of Time) |
| [#698](https://github.com/zortos293/Lichii/pull/698) | merged | upstream-merged | common: leave threads to Windows on hybrid CPUs with few performance cores (Ocarina of Time) |
| [#695](https://github.com/zortos293/Lichii/pull/695) | open | merged | Ocarina of Time: AMD FSR 3.1 in place of the game's anti-aliasing network |
| [#693](https://github.com/zortos293/Lichii/pull/693) | open | merged | common: use Intel's P-core list for GPU thread pinning on Linux hybrid CPUs |
| [#692](https://github.com/zortos293/Lichii/pull/692) | merged | upstream-merged | texture_cache: cap the EDEN_NX2_TC_*_MIB overrides at the device budget (Ocarina of Time) |
| [#686](https://github.com/zortos293/Lichii/pull/686) | merged | upstream-merged | file_sys: guard LayeredFS and ExeFS patch dirs against missing mod roots (ports upstream #728) |
| [#679](https://github.com/zortos293/Lichii/pull/679) | merged | upstream-merged | vulkan: import the easy AMD fixes from the CheezerWheezer fork survey (EGLA-67) |
| [#678](https://github.com/zortos293/Lichii/pull/678) | open | merged | Ocarina of Time: NVIDIA DLSS and DLAA in place of the game's anti-aliasing network |
| [#664](https://github.com/zortos293/Lichii/pull/664) | merged | upstream-merged | video_core: make GPU Clocks Boost and Overclock shrink Switch 2 GPU durations again (Mario Kart World 30 fps lock) |
| [#659](https://github.com/zortos293/Lichii/pull/659) | merged | upstream-merged | nx2: keep movie video in sync with its audio (imports #571, ports upstream 1f52ac7e2e) |
| [#657](https://github.com/zortos293/Lichii/pull/657) | merged | upstream-merged | vulkan: keep HMMA off AMD's emulated cooperative matrices (RDNA2 crash since #583) |
| [#656](https://github.com/zortos293/Lichii/pull/656) | open | reconciled | macOS performance and MoltenVK fixes: JIT metadata, query copies, buffer tails, shader reuse, async translation |
| [#655](https://github.com/zortos293/Lichii/pull/655) | merged | upstream-merged | Ocarina: FPS Unlock built-in and Switch 2 frame rate limit slider |
| [#652](https://github.com/zortos293/Lichii/pull/652) | open | reconciled | vulkan: retain stale Switch 2 graphics pipelines within a RAM budget |
| [#649](https://github.com/zortos293/Lichii/pull/649) | merged | upstream-merged | engines: don't shadow KeplerCompute::state in BypassOotNetwork |
| [#645](https://github.com/zortos293/Lichii/pull/645) | merged | upstream-merged | common: retry the Windows fastmem backing allocation instead of losing fastmem |
| [#644](https://github.com/zortos293/Lichii/pull/644) | closed | reconciled | OoT DLSS: hand the effects bias mask to Streamline as R16F; presets E/F |
| [#643](https://github.com/zortos293/Lichii/pull/643) | open | merged | video_core: emulate tensor programs only where cooperative matrices run them; stand in otherwise (imports #143 by @felipecsl) |
| [#641](https://github.com/zortos293/Lichii/pull/641) | closed | reconciled | OoT (Switch 2): GPU split and back-thread work, 31 → 46.5 FPS on a Ryzen 3700X |
| [#640](https://github.com/zortos293/Lichii/pull/640) | open | already-present | video_core, vulkan: drop only damaged, stale or duplicate pipeline cache records |
| [#635](https://github.com/zortos293/Lichii/pull/635) | open | reconciled | vulkan: keep Ocarina's glows and particles out of DLSS history (night smear and flicker) |
| [#634](https://github.com/zortos293/Lichii/pull/634) | closed | already-present | vulkan: EDEN_NX2_NO_COOP_MATRIX=1 keeps HMMA on the software path (amdvlk64 crash on RDNA2) |
| [#629](https://github.com/zortos293/Lichii/pull/629) | closed | already-present | vulkan: no cooperative-matrix HMMA on AMD GPUs without matrix units (RDNA2 boot crash since #583) |
| [#628](https://github.com/zortos293/Lichii/pull/628) | open | merged | macos: build for Intel Macs, and decode ASTC on the CPU when the GPU decoder can't be built |
| [#621](https://github.com/zortos293/Lichii/pull/621) | merged | upstream-merged | gpu_thread: reset pending-channel poll timer on channel progress and use 1-tick sleeps on Windows |
| [#618](https://github.com/zortos293/Lichii/pull/618) | open | merged | OoT: photo mode (free camera, picture ratios, hotsampling, IGCS camera tools) |
| [#616](https://github.com/zortos293/Lichii/pull/616) | merged | upstream-merged | video_core: Ocarina of Time option to bypass the game's sharpening anti-aliasing network (imports #319 by @ProphetWild) |
| [#615](https://github.com/zortos293/Lichii/pull/615) | open | reconciled | vulkan: port Eglantine #754 channel-aware state tracking |
| [#612](https://github.com/zortos293/Lichii/pull/612) | open | merged | qt: share settings as short codes in Configure and game Properties |
| [#606](https://github.com/zortos293/Lichii/pull/606) | merged | upstream-merged | vulkan: NVIDIA DLSS Super Resolution for Ocarina of Time (imports #456 by @Rengundom) |
| [#589](https://github.com/zortos293/Lichii/pull/589) | open | reconciled | engines: replay a split-front macro the compiled program rejects on the interpreter |
| [#584](https://github.com/zortos293/Lichii/pull/584) | merged | upstream-merged | file_sys: keep Ocarina's Dynamic FPS real down to 10 fps (imports #299 by @ctgl1987) |
| [#583](https://github.com/zortos293/Lichii/pull/583) | merged | upstream-merged | shader: run HMMA on VK_KHR_cooperative_matrix 16x16x16 (RDNA4: Ocarina of Time 41 → 60 fps) (imports #521 by @estevanbs) |
| [#582](https://github.com/zortos293/Lichii/pull/582) | merged | upstream-merged | shader: run HMMA.16816.F16 on cooperative matrices (Ocarina of Time +12% standing, +24% walking) (imports #475 by @mstampfli) |
| [#580](https://github.com/zortos293/Lichii/pull/580) | merged | upstream-merged | buffer_cache: reuse a contiguous range's page table walk for its shorter sizes (imports #478 by @mstampfli) |
| [#579](https://github.com/zortos293/Lichii/pull/579) | open | reconciled | vulkan: pipeline libraries + shader warm-up from the game files (first-visit shader stutter) |
| [#573](https://github.com/zortos293/Lichii/pull/573) | open | reconciled | video_core, vulkan: AMD and split GPU thread fixes for Ocarina of Time |
| [#571](https://github.com/zortos293/Lichii/pull/571) | open | already-present | nx2: keep AV1 movies in time with their audio (Ocarina of Time title movie 10 s -> 0.03 s behind) |
| [#570](https://github.com/zortos293/Lichii/pull/570) | closed | already-present | fix: vk_query_cache CloseCounter occlusion reset threshold 256->8192 (OoT water/window flicker #340) |
| [#569](https://github.com/zortos293/Lichii/pull/569) | merged | upstream-merged | vulkan, shader: draw task/mesh pipelines through compute without VK_EXT_mesh_shader |
| [#566](https://github.com/zortos293/Lichii/pull/566) | closed | already-present | fix: vk_query_cache CloseCounter occlusion reset threshold 256->8192 (OoT water/window flicker #340) |
| [#564](https://github.com/zortos293/Lichii/pull/564) | closed | already-present | fix: vk_query_cache CloseCounter occlusion reset threshold 256->4096 (OoT water/window flicker #340) |
| [#559](https://github.com/zortos293/Lichii/pull/559) | open | reconciled | video_core: wait only for a macro DMA read's own query reports |
| [#558](https://github.com/zortos293/Lichii/pull/558) | closed | already-present | fix: vk_query_cache CloseCounter occlusion reset threshold 256->16384 (OoT water/window flicker #340) |
| [#557](https://github.com/zortos293/Lichii/pull/557) | closed | already-present | vk_pipeline_cache: rebuild compute pipelines once a null pointer slot holds a buffer (port of Eglantine 688c6fa7d8, OoT near hedges) |
| [#556](https://github.com/zortos293/Lichii/pull/556) | closed | merged | nx2:vi2: EDEN_NX2_FRAMES_AHEAD=2 runs one more frame ahead (Ocarina ~5% in Kokiri village) |
| [#555](https://github.com/zortos293/Lichii/pull/555) | closed | reconciled | vk_pipeline_cache: don't evict unused pipelines with EDEN_NX2_PRELOAD_ALL_PIPELINES |
| [#553](https://github.com/zortos293/Lichii/pull/553) | closed | partial-platform | Android Reworked + Performance  |
| [#551](https://github.com/zortos293/Lichii/pull/551) | merged | upstream-merged | vulkan, shader: rebuild native compute translations whose pointer slots were null, bind up to 64 storage buffers (OoT Hyrule Field grass; imports #395, #421, #434) |
| [#550](https://github.com/zortos293/Lichii/pull/550) | merged | upstream-merged | vulkan: don't evict disk-cache pipelines that were never used |
| [#543](https://github.com/zortos293/Lichii/pull/543) | open | merged | input: EDEN_GC_RUMBLE_BOOST so HD-rumble titles can be felt on GameCube controllers (Ocarina of Time) |
| [#530](https://github.com/zortos293/Lichii/pull/530) | merged | upstream-merged | common: pin the split GPU threads on Linux too |
| [#527](https://github.com/zortos293/Lichii/pull/527) | closed | already-present | vulkan, shader: prototype task/mesh emulation without VK_EXT_mesh_shader (#349) |
| [#526](https://github.com/zortos293/Lichii/pull/526) | merged | upstream-merged | ci: build the Windows PGO release with clang-cl, ThinLTO and an OoT profile |
| [#524](https://github.com/zortos293/Lichii/pull/524) | merged | upstream-merged | cmake: build with -fno-unswitch-loops on GCC 13+ (Linux black screen, #500) |
| [#521](https://github.com/zortos293/Lichii/pull/521) | closed | already-present | shader: run HMMA on VK_KHR_cooperative_matrix 16x16x16 (RDNA4: Ocarina of Time 41 → 60 fps) |
| [#518](https://github.com/zortos293/Lichii/pull/518) | merged | upstream-merged | ci: build the standard Windows release with clang-cl and ThinLTO |
| [#515](https://github.com/zortos293/Lichii/pull/515) | merged | upstream-merged | shader: mesh programs that output points or lines (port of upstream 743d5bbaed) |
| [#512](https://github.com/zortos293/Lichii/pull/512) | merged | upstream-merged | nx2: diagnostics for device loss: VK_EXT_device_fault, unregister-page detail, unmapped read callers |
| [#499](https://github.com/zortos293/Lichii/pull/499) | merged | upstream-merged | NUP: allow icon/title edits when metadata sits before the blob; take base-package icons |
| [#497](https://github.com/zortos293/Lichii/pull/497) | merged | upstream-merged | Switch 2 microphone capture and Controls configuration (#255, merged with nx2) |
| [#494](https://github.com/zortos293/Lichii/pull/494) | open | out-of-scope | vulkan: improve Xenoblade Chronicles 2 cloud rendering on AMD |
| [#491](https://github.com/zortos293/Lichii/pull/491) | closed | already-present | video_core: keep FrontTrackInstanceLoop out of line for GCC 13 (black screen at boot since #178) |
| [#488](https://github.com/zortos293/Lichii/pull/488) | merged | upstream-merged | video_core: keep same-value instance stores while the back's sink holds state writes |
| [#484](https://github.com/zortos293/Lichii/pull/484) | merged | upstream-merged | qt: don't resize a maximized window when a game boots |
| [#481](https://github.com/zortos293/Lichii/pull/481) | merged | upstream-merged | common: back RangeSet with a sorted array (buffer cache lookups without tree nodes) |
| [#478](https://github.com/zortos293/Lichii/pull/478) | closed | already-present | buffer_cache: reuse a contiguous range's page table walk for its shorter sizes |
| [#475](https://github.com/zortos293/Lichii/pull/475) | closed | already-present | shader: run HMMA.16816.F16 on cooperative matrices (Ocarina of Time +12% standing, +24% walking) |
| [#467](https://github.com/zortos293/Lichii/pull/467) | closed | merged | game_list: Import Shader Pack action that installs pre-compiled pipeline packs (Ocarina of Time 22,769-pipeline pack) |
| [#465](https://github.com/zortos293/Lichii/pull/465) | merged | upstream-merged | video_core: bounded wait for unbuilt Switch 2 pipelines (Ocarina of Time: draws held 14.7 s to 0.2 s per 10 s) |
| [#463](https://github.com/zortos293/Lichii/pull/463) | closed | already-present | texture_cache: don't upload stale guest memory into new images (Ocarina of Time +20% in Castle Town) |
| [#457](https://github.com/zortos293/Lichii/pull/457) | closed | already-present | file_sys: edit NUP metadata that sits between the TOC and the blob ("fix: bad meta start") |
| [#456](https://github.com/zortos293/Lichii/pull/456) | closed | reconciled | vulkan: add NVIDIA DLSS Super Resolution for Ocarina of Time |
| [#448](https://github.com/zortos293/Lichii/pull/448) | closed | already-present | vulkan: keep ASTC with uneven BC block mips as RGBA8 on AMD (OoT dark terrain lines) |
| [#446](https://github.com/zortos293/Lichii/pull/446) | merged | upstream-merged | gpu_thread: poll suspended channels every 50 us instead of every 1 ms (Ocarina ~11% faster in Kokiri village) |
| [#445](https://github.com/zortos293/Lichii/pull/445) | closed | merged | debugger: frame time statistics in status (median, p90, p99, max, over 100 ms, fence wait) |
| [#440](https://github.com/zortos293/Lichii/pull/440) | merged | upstream-merged | vulkan, texture_cache: fail an image that does not fit instead of throwing |
| [#439](https://github.com/zortos293/Lichii/pull/439) | merged | upstream-merged | macro: enlarge the Turing macro RAM so entries past 0x1000 execute |
| [#438](https://github.com/zortos293/Lichii/pull/438) | merged | upstream-merged | engines: hand the macros back to the engines when the GPU split stops |
| [#437](https://github.com/zortos293/Lichii/pull/437) | closed | reconciled | vulkan: keep version 53 pipeline caches |
| [#434](https://github.com/zortos293/Lichii/pull/434) | closed | already-present | vulkan: rebuild compute translations without dropping pointers, with 64 storage buffers (OoT near foliage) |
| [#433](https://github.com/zortos293/Lichii/pull/433) | merged | upstream-merged | shader: gather each tap at its own offset when per-tap offsets are dynamic |
| [#432](https://github.com/zortos293/Lichii/pull/432) | merged | upstream-merged | shader: NaN-defined float min, max, clamp and atomic min/max |
| [#431](https://github.com/zortos293/Lichii/pull/431) | open | already-present | common: trim all-zero guest pages from the working set on Windows (EDEN_NX2_TRIM_ZERO) |
| [#430](https://github.com/zortos293/Lichii/pull/430) | merged | upstream-merged | vulkan: log once when task/mesh pipelines are skipped without VK_EXT_mesh_shader |
| [#429](https://github.com/zortos293/Lichii/pull/429) | merged | upstream-merged | shader: route the remaining 32-bit global atomics to their storage buffer |
| [#427](https://github.com/zortos293/Lichii/pull/427) | merged | upstream-merged | vulkan: keep framebuffers within the device limits, pad the stream buffer |
| [#425](https://github.com/zortos293/Lichii/pull/425) | merged | upstream-merged | buffer_cache: track every 64-byte block a usage range touches |
| [#423](https://github.com/zortos293/Lichii/pull/423) | closed | already-present | vulkan, shader: raise the native storage binding budget to 64 behind EDEN_NX2_SSBO64 (OoT field grass) |
| [#421](https://github.com/zortos293/Lichii/pull/421) | closed | already-present | shader: charge pointer table entries only for bindings the first pass left free (OoT foliage) |
| [#418](https://github.com/zortos293/Lichii/pull/418) | merged | upstream-merged | common, video_core: experimental setting to pin the split GPU threads to their own cores |
| [#415](https://github.com/zortos293/Lichii/pull/415) | open | already-present | video_core: mark native compute storage bindings only as far as the shader's stores reach |
| [#414](https://github.com/zortos293/Lichii/pull/414) | merged | upstream-merged | vulkan: don't wait for the GPU to grow the quad index buffer |
| [#413](https://github.com/zortos293/Lichii/pull/413) | merged | upstream-merged | vulkan: answer MustFlushRegion without the buffer cache lock for never-GPU-written pages |
| [#412](https://github.com/zortos293/Lichii/pull/412) | merged | upstream-merged | video_core: drop redundant per-draw and per-frame bookkeeping on the GPU back thread |
| [#411](https://github.com/zortos293/Lichii/pull/411) | closed | reconciled | vulkan: pipeline libraries + shader warm-up from the game files (first-visit shader stutter) |
| [#410](https://github.com/zortos293/Lichii/pull/410) | merged | upstream-merged | shader: bind loaded pointers that compete with a paired cbuf pointer (OoT near foliage) |
| [#408](https://github.com/zortos293/Lichii/pull/408) | open | reconciled | Ocarina of Time: fix stutter from pipeline rebuilds and slow compute builds |
| [#398](https://github.com/zortos293/Lichii/pull/398) | merged | upstream-merged | shader: gather from a second descriptor buffer as TLD4.B |
| [#397](https://github.com/zortos293/Lichii/pull/397) | merged | upstream-merged | buffer_cache: don't download GPU data over newer CPU writes |
| [#395](https://github.com/zortos293/Lichii/pull/395) | closed | already-present | vulkan: rebuild native compute pipelines whose pointer slot was null at translation (OoT near hedges) |
| [#394](https://github.com/zortos293/Lichii/pull/394) | merged | upstream-merged | hid: write the Joy-Con pair attribute in one store |
| [#387](https://github.com/zortos293/Lichii/pull/387) | closed | reconciled | video_core, qt: NVIDIA DLSS toggle hotkey, sharpness and DLAA above 1x for Ocarina |
| [#378](https://github.com/zortos293/Lichii/pull/378) | open | merged | vulkan_device: opt-in EDEN_AMD_FP16=1 to keep shaderFloat16 on the AMD driver |
| [#377](https://github.com/zortos293/Lichii/pull/377) | merged | upstream-merged | nx2_sampler: name every report and stop walks that leave the stack |
| [#376](https://github.com/zortos293/Lichii/pull/376) | open | merged | texture_cache: download the collector's evicted images with one GPU wait |
| [#354](https://github.com/zortos293/Lichii/pull/354) | open | merged | Fix Ocarina of Time's green tint on AMD: bind light-table shadow maps through descriptor arrays |
| [#353](https://github.com/zortos293/Lichii/pull/353) | merged | upstream-merged | vulkan: don't write render target components the fragment shader does… |
| [#351](https://github.com/zortos293/Lichii/pull/351) | merged | upstream-merged | buffer_cache: bind table-pointer pools as far as a storage buffer rea… |
| [#345](https://github.com/zortos293/Lichii/pull/345) | open | reconciled | video_core: precise split-front DMA reads, per-bind native bank checks, query flush before macro reads (Ocarina perf and white flashes) |
| [#337](https://github.com/zortos293/Lichii/pull/337) | merged | upstream-merged | query_cache: keep fence-flagged reports pending until their fence runs (Ocarina white flashes) |
| [#329](https://github.com/zortos293/Lichii/pull/329) | merged | upstream-merged | vk_graphics_pipeline: run graphics warp ops at the guest's warp size (Ocarina white leaves on Intel) |
| [#321](https://github.com/zortos293/Lichii/pull/321) | open | merged | vk_graphics_pipeline: build a held draw's pipeline inline again for worker-only builds |
| [#319](https://github.com/zortos293/Lichii/pull/319) | closed | already-present | Ocarina of Time: option to bypass the game's sharpening anti-aliasing network |
| [#318](https://github.com/zortos293/Lichii/pull/318) | closed | already-present | Ocarina of Time: option to bypass the game's sharpening anti-aliasing network |
| [#313](https://github.com/zortos293/Lichii/pull/313) | open | reconciled | vk_pipeline_cache: keep Switch 2 pipelines up to a memory budget instead of evicting them after 9000 frames |
| [#311](https://github.com/zortos293/Lichii/pull/311) | merged | upstream-merged | texture_cache: match the clear-skip check to the clear's real coverage |
| [#310](https://github.com/zortos293/Lichii/pull/310) | merged | upstream-merged | dynarmic: wait for the code space commit instead of emitting past it |
| [#309](https://github.com/zortos293/Lichii/pull/309) | merged | upstream-merged | vk_query_cache: log when the bank-bound reset drops a samples count |
| [#301](https://github.com/zortos293/Lichii/pull/301) | merged | upstream-merged | vulkan: add EDEN_NX2_PIPELINE_BUILD_TRACE to name shaders a driver crashes on |
| [#300](https://github.com/zortos293/Lichii/pull/300) | open | reconciled | vulkan: build software HMMA compute pipelines off the wait path on AMD drivers |
| [#299](https://github.com/zortos293/Lichii/pull/299) | closed | reconciled | file_sys: keep Ocarina's Dynamic FPS real down to 10 fps |
| [#295](https://github.com/zortos293/Lichii/pull/295) | merged | upstream-merged | shader, vulkan: split misaligned 64/128-bit SSBO accesses and order compute writes |
| [#292](https://github.com/zortos293/Lichii/pull/292) | closed | already-present | buffer_cache: bind compute table pointers up to 256 MiB for Ocarina's… |
| [#288](https://github.com/zortos293/Lichii/pull/288) | closed | already-present | texture_cache: skip uploads a join copy overwrites whole |
| [#284](https://github.com/zortos293/Lichii/pull/284) | merged | upstream-merged | externals: include AV1 decoding in MinGW builds |
| [#282](https://github.com/zortos293/Lichii/pull/282) | closed | already-present | Ocarina of Time: option to bypass the game's sharpening anti-aliasing network |
| [#279](https://github.com/zortos293/Lichii/pull/279) | merged | upstream-merged | shader: shadow high registers of IMAD with a uniform multiplier |
| [#278](https://github.com/zortos293/Lichii/pull/278) | open | merged | fsp: make the IStorage read throttle configurable (OoT opening video 3-6 s -> 1.2 s behind) |
| [#277](https://github.com/zortos293/Lichii/pull/277) | closed | reconciled | Merge upstream Eglantine nx2 (e736dfde..0ca2032bff) |
| [#270](https://github.com/zortos293/Lichii/pull/270) | merged | upstream-merged | nx2: add an FPS limit for the game's present rate |
| [#267](https://github.com/zortos293/Lichii/pull/267) | open | reconciled | How I got 60 FPS on my config: Ryzen 9 9950X3D + RTX 4080 (Ocarina of Time) |
| [#264](https://github.com/zortos293/Lichii/pull/264) | merged | upstream-merged | buffer_cache: arm read traps only on the pages not armed yet |
| [#259](https://github.com/zortos293/Lichii/pull/259) | open | reconciled | buffer_cache: opt-in EDEN_NX2_PREFLUSH=1 for Switch 2 titles (OoT Kokiri ~26 -> ~40 fps, some flicker) |
| [#257](https://github.com/zortos293/Lichii/pull/257) | closed | reconciled | shader: never mark a depth-compare texture as integer |
| [#255](https://github.com/zortos293/Lichii/pull/255) | closed | already-present | Implement Switch 2 microphone capture and Controls configuration |
| [#253](https://github.com/zortos293/Lichii/pull/253) | closed | reconciled | video_core: offer NVIDIA DLSS in place of Ocarina of Time's anti-aliasing |
| [#251](https://github.com/zortos293/Lichii/pull/251) | open | already-present | nx2:vdec: keep Ocarina's movies in time with their sound below 60 fps |
| [#250](https://github.com/zortos293/Lichii/pull/250) | merged | upstream-merged | video_core: skip GPU-write invalidations of caches with nothing on those pages |
| [#246](https://github.com/zortos293/Lichii/pull/246) | merged | upstream-merged | tests: compare unsigned results with unsigned literals |
| [#245](https://github.com/zortos293/Lichii/pull/245) | merged | upstream-merged | video_core: read bindless handle words from the host-aligned storage base |
| [#243](https://github.com/zortos293/Lichii/pull/243) | open | reconciled | video_core: macro DMA reads on the split front without draining the back |
| [#242](https://github.com/zortos293/Lichii/pull/242) | closed | reconciled | shader: don't take rect or buffer texture types from a per-draw handle's fallback |
| [#241](https://github.com/zortos293/Lichii/pull/241) | merged | upstream-merged | shader: retry native lowering with an aligned high uniform base before rejecting |
| [#240](https://github.com/zortos293/Lichii/pull/240) | merged | upstream-merged | file_sys: bundle Dark's Dynamic FPS mod for Ocarina |
| [#235](https://github.com/zortos293/Lichii/pull/235) | open | reconciled | video_core: queue a safe read's flush on the split's back thread (EDEN_NX2_SPLIT_SAFE_READS) |
| [#232](https://github.com/zortos293/Lichii/pull/232) | merged | upstream-merged | common: keep time-critical threads on the performance cores of hybrid CPUs on Windows |
| [#231](https://github.com/zortos293/Lichii/pull/231) | closed | reconciled | video_core: resolve waiting semaphore acquires without draining the GPU split's back |
| [#230](https://github.com/zortos293/Lichii/pull/230) | closed | reconciled | vulkan: enable float16 on the device for host shaders so LSFG frame generation loads |
| [#228](https://github.com/zortos293/Lichii/pull/228) | merged | upstream-merged | dist: describe EDEN_NX2_FRAMES_AHEAD as the code reads it |
| [#223](https://github.com/zortos293/Lichii/pull/223) | open | reconciled | AMD Mobile GPU successful in-game launch |
| [#216](https://github.com/zortos293/Lichii/pull/216) | closed | reconciled | core: keep Ocarina of Time at real speed below 60 fps |
| [#215](https://github.com/zortos293/Lichii/pull/215) | merged | upstream-merged | video_core: wait for pending query results before macro DMA reads (Ocarina white flashes) |
| [#213](https://github.com/zortos293/Lichii/pull/213) | open | merged | input: HD Rumble and DualSense haptics through RenHDrum |
| [#210](https://github.com/zortos293/Lichii/pull/210) | closed | reconciled | vulkan: build three Ocarina of Time shaders unoptimized on AMD's proprietary driver |
| [#206](https://github.com/zortos293/Lichii/pull/206) | closed | reconciled | shader: retry compare-exchange loops with the value the exchange returned |
| [#204](https://github.com/zortos293/Lichii/pull/204) | closed | already-present | video_core: flush the query cache before macro DMA reads under the GPU split |
| [#203](https://github.com/zortos293/Lichii/pull/203) | merged | upstream-merged | file_sys, config: apply LayeredFS mods to NUP titles; assert nested array index against the innermost array |
| [#199](https://github.com/zortos293/Lichii/pull/199) | merged | upstream-merged | common: write each console log line in one call on Windows |
| [#197](https://github.com/zortos293/Lichii/pull/197) | merged | upstream-merged | shader_cache: skip the cached rejection path for compute dispatches |
| [#195](https://github.com/zortos293/Lichii/pull/195) | closed | superseded-build | ci: decode AV1 in the MinGW build with MSYS2's FFmpeg |
| [#194](https://github.com/zortos293/Lichii/pull/194) | merged | upstream-merged | video_core: port scoped Frizti performance and FSR3 fixes |
| [#192](https://github.com/zortos293/Lichii/pull/192) | merged | upstream-merged | video_core: store guarded and mesh pipelines in the Vulkan disk cache |
| [#186](https://github.com/zortos293/Lichii/pull/186) | merged | upstream-merged | common: name threads on MinGW builds too |
| [#185](https://github.com/zortos293/Lichii/pull/185) | merged | upstream-merged | texture_cache: land deferred clears before non-pass image writes |
| [#184](https://github.com/zortos293/Lichii/pull/184) | merged | upstream-merged | video_core: promote the 17 enabled perf9 paths |
| [#182](https://github.com/zortos293/Lichii/pull/182) | merged | upstream-merged | debug server: frame_times for guest present intervals |
| [#179](https://github.com/zortos293/Lichii/pull/179) | closed | reconciled | perf/oot-gpu-thread: narrower split waits and live experiment switches (~34 -> ~49 fps) |
| [#178](https://github.com/zortos293/Lichii/pull/178) | merged | upstream-merged | video_core: skip the instance loop writes the split back ignores |
| [#174](https://github.com/zortos293/Lichii/pull/174) | merged | upstream-merged | perf(shader): unroll each K8 group of the software HMMA |
| [#173](https://github.com/zortos293/Lichii/pull/173) | closed | reconciled | fix(shader): keep depth-compare images float when read int |
| [#166](https://github.com/zortos293/Lichii/pull/166) | open | reconciled | video_core: opt-in split-pipeline switches and fixes from the Ocarina of Time 30 fps work |
| [#161](https://github.com/zortos293/Lichii/pull/161) | open | reconciled | perf: reduce Ocarina of Time timing and GPU submission overhead |
| [#149](https://github.com/zortos293/Lichii/pull/149) | merged | upstream-merged | Fix OoT split-thread deadlock, FSR3 frame-gen interpolation, and RED/SURED register shadowing |
| [#147](https://github.com/zortos293/Lichii/pull/147) | closed | reconciled | buffer_cache: batch CPU-readback ranges into one GPU wait |
| [#146](https://github.com/zortos293/Lichii/pull/146) | merged | upstream-merged | video_core: cache continuous GPU ranges until the page table changes |
| [#144](https://github.com/zortos293/Lichii/pull/144) | merged | upstream-merged | shader: lower the remaining Ocarina compute forms |
| [#143](https://github.com/zortos293/Lichii/pull/143) | open | reconciled | video_core: don't emulate tensor programs on Mesa's Intel driver; stand in for Ocarina's upscaler |
| [#142](https://github.com/zortos293/Lichii/pull/142) | merged | upstream-merged | vulkan: drop draws recorded after a graphics pipeline that failed to build |
| [#141](https://github.com/zortos293/Lichii/pull/141) | merged | upstream-merged | texture_cache: skip the upload of a new render target a clear overwrites whole |
| [#137](https://github.com/zortos293/Lichii/pull/137) | merged | upstream-merged | common: retry early UMWAIT wakes until the Event deadline |
| [#127](https://github.com/zortos293/Lichii/pull/127) | merged | upstream-merged | shader: count nested returns before moving demotes to the end (AMD black scene) |
| [#125](https://github.com/zortos293/Lichii/pull/125) | closed | reconciled | shader: accept any UP for the carry of uniform 64-bit adds |
| [#124](https://github.com/zortos293/Lichii/pull/124) | merged | upstream-merged | file_sys: built-in exefs patches, starting with full-resolution 3D for 0400486024DEC000 |
| [#121](https://github.com/zortos293/Lichii/pull/121) | merged | upstream-merged | vk_scheduler: pause, don't reset, the occlusion counter at each submission (Ocarina white flashes) |
| [#112](https://github.com/zortos293/Lichii/pull/112) | merged | upstream-merged | shader: lower the green-hat kid adventure's 1080p upscaler gathers |
| [#103](https://github.com/zortos293/Lichii/pull/103) | merged | upstream-merged | buffer_cache: upload CPU writes onto GPU-written pages instead of downloading them |
| [#102](https://github.com/zortos293/Lichii/pull/102) | closed | reconciled | shader: lower Ocarina of Time depth-compare and per-tap gathers, LEA.HI -Rb and SURED OR |
| [#100](https://github.com/zortos293/Lichii/pull/100) | merged | upstream-merged | shader: shadow ISBEWR data registers (Ocarina door NaN) |
| [#90](https://github.com/zortos293/Lichii/pull/90) | closed | superseded-build | Oot windows build |
| [#88](https://github.com/zortos293/Lichii/pull/88) | merged | upstream-merged | fix(vulkan): wait for the device before recreating the swapchain |
| [#87](https://github.com/zortos293/Lichii/pull/87) | merged | upstream-merged | kernel, shader: boot and render the green-hat kid adventure |
