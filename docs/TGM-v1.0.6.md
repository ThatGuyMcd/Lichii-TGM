# Lichii-tgm-v1.0.6

I've brought across two more shared fixes from the nightly review, keeping our Forest Lantern, handheld and Couch Mode interfaces, every setting, saves and OOT add-ons intact. Frame generation remains off.

## Profiles and launching

I've fixed profile reloads so the selected user is opened again. Reloading an empty user directory also creates and opens the normal default profile, matching the existing startup behaviour.

The command-line launcher now resolves user names after loading the selected configuration and its NAND directory. Names beginning with a number stay names, repeated `--user` options keep the last choice, and invalid indices keep the configured choice. Existing profile files retain their user IDs and contents.

This adapts [Lichii #2064](https://github.com/zortos293/Lichii/pull/2064), landed in [d3074e62d3](https://github.com/zortos293/Lichii/commit/d3074e62d35815064687c4c9adfd3c38baaac889), with additional checks for deferred profile selection and numeric bounds.

## Repeated memory diagnostics

I've limited repeated unmapped-memory write messages to the first 1,000 for each write width, followed by a periodic sample every 100,000 attempts. The address and value remain in each message. The counters are safe across host threads, and mapped writes keep their existing path.

This adapts [Lichii #2151](https://github.com/zortos293/Lichii/pull/2151), reviewed at [5b5330a826](https://github.com/GoatTech-42/Lichii/commit/5b5330a82662b99b400c9870cfab7d9e67ef6dca). It reduces logging overhead when faults repeat; it doesn't change the underlying memory operation.

## Downloads

I've included matching Windows x86-64 and Linux AppImage builds, with checksums, the source/version manifest and complete automated test receipts. Couch Mode and its first-launch invitation are included in both.

[All source changes since v1.0.5](https://github.com/ThatGuyMcd/Lichii-TGM/compare/Lichii-tgm-v1.0.5...Lichii-tgm-v1.0.6).

Thanks to the upstream authors and the Lichii, Fritchii, Eglantine, Eden and yuzu contributors. Their original copyright notices and licences remain. Games, keys and firmware are not included.
