# Shader preparation profiles

`oot/oot-v131072-cache54.nx2recipe` contains pipeline state and shader fingerprints
for Ocarina of Time Remake, title `0400486024DEC000`, game version `131072`, pipeline
cache version `54`. Shader code is assembled locally from the user's game files.

Source: [Frizti/Fritchii v5.7](https://github.com/Frizti/Fritchii/releases/tag/fritchii-v5.7),
`recipes/OoT v131072 (cache v54).nx2recipe` inside `Fritchii-v5.7-Windows-x64.zip`.

- Release archive SHA-256: `245489bd70075e9913d670683dd28a92b0bab286b03a190fce07dc1dc0233843`
- Recipe SHA-256: `85d944c8557fbeffe02af566c9c4277251ed0ab1a0f2a3ab7a626af6a877c3a1`
- Recipe bytes: `1821351`
- Supported main build: `E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348`

The profile is versioned data, not a promise of every possible runtime pipeline.
It is used by the explicit Prepare game shaders action. Keeping it in a subfolder
preserves the existing quick precache's nonrecursive recipe discovery behaviour.
The UI reports unreadable content, missing programs, failed compilations and GPU
incompatibilities. Original recipe tooling/provenance: Lichii PRs #895/#896/#1021;
see `docs/nx2/native-shader-preparation.md` and `tools/nx2-precompile/README.md`.
