# OoT startup JIT crash on the experimental build

Reported build: `0f3c7b6cc6`, Windows x64, Ryzen 9 3900X, RTX 2080 Super.
The target log ends after cached shader loading with:

```
emitting the A64 block at 0x808e4994 (location 0x808e4994) failed:
Xbyak error 18 (bad mem size)
```

This is a host CPU code-generation failure. Shader loading finishing immediately
before it does not identify the shader cache as the cause.

## Reproduction and correction

The indirect-branch cache imported from Lichii PR #831 emits a RIP-relative
indirect jump through `qword[rip + slot]`. In bundled Xbyak 7.40.1, strict memory
size checking rejects this form: `opJmpOp` suppresses REX.W and `opRext` uses a
synthetic 32-bit register operand, which fails the memory/register width check.
A standalone test reproduces error 18 with strict checking enabled. The equivalent
`ptr[rip + slot]` form emits the expected six-byte `FF 25` indirect jump. With
strict checking disabled, both forms emit identical bytes.

Dynarmic previously set `XBYAK_STRICT_CHECK_MEM_REG_SIZE=0` and
`XBYAK_OLD_DISP_CHECK=1` privately. Other users of the header-only Xbyak library
compiled different inline definitions. Object inspection found both strict and
relaxed definitions of the same `opMR` symbol; the shipped GUI contains the strict
version. Linker selection could therefore expose this error inside Dynarmic.

The correction uses the unsized jump operand and places the existing Dynarmic
compatibility definitions on the shared Xbyak interface so its consumers agree.
This preserves the jump's machine code and the experimental branch cache.

Upstream Eglantine `nx2` at `78743f30c2` does not contain this branch-cache
implementation. The related open PRs and issues were checked before this change;
no matching correction was found. The original branch-cache commit is
`55d42ca12e` (ported from `451bdf6faa`, Lichii PR #831).

The mechanism reproduces the reported error, but the exact guest block and a
successful game boot still need confirmation on the reporting PC. This change
does not establish an FPS improvement.

## Performance settings confirmed by the target log

The same report enables NVIDIA DLSS with OoT Quality mode, which changes output
to 3840x2160, scene rendering to 2560x1440 and resolution scale to 1.5x. It also
enables FSR3 frame generation. Real game FPS should be measured with frame
generation off. Once startup works, compare the same warmed scene with DLSS off
and OoT anti-aliasing set to No sharpening, then test resolution separately.

The report already uses Low GPU accuracy and Unsafe DMA accuracy; recommending a
lower accuracy setting is therefore not an explanation for its 15-28 FPS.
Cooperative tensor matrices are active. Shader objects and descriptor heaps
fall back on this GPU, while native storage and uniforms are active.
