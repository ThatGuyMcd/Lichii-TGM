# The NVIDIA SM86 (Maxwell) GPU - Instruction set

<!-- TOC -->
[AL2P](#al2p)
[ALD](#ald)
[AST](#ast)
[ATOM](#atom)
[ATOMS](#atoms)
[B2R](#b2r)
[BAR](#bar)
[BFE](#bfe)
[BFI](#bfi)
[BPT](#bpt)
[BRA](#bra)
[BRK](#brk)
[BRX](#brx)
[CAL](#cal)
[CCTL](#cctl)
[CCTLL](#cctll)
[CONT](#cont)
[CS2R](#cs2r)
[CSET](#cset)
[CSETP](#csetp)
[DADD](#dadd)
[DEPBAR](#depbar)
[DFMA](#dfma)
[DMNMX](#dmnmx)
[DMUL](#dmul)
[DSET](#dset)
[DSETP](#dsetp)
[EXIT](#exit)
[F2F](#f2f)
[F2I](#f2i)
[FADD](#fadd)
[FCHK](#fchk)
[FCMP](#fcmp)
[FFMA](#ffma)
[FLO](#flo)
[FMNMX](#fmnmx)
[FMUL](#fmul)
[FSET](#fset)
[FSETP](#fsetp)
[FSWZADD](#fswzadd)
[GETCRSPTR](#getcrsptr)
[GETLMEMBASE](#getlmembase)
[HADD2](#hadd2)
[HFMA2](#hfma2)
[HMUL2](#hmul2)
[HSET2](#hset2)
[HSETP2](#hsetp2)
[I2F](#i2f)
[I2I](#i2i)
[IADD](#iadd)
[IADD3](#iadd3)
[ICMP](#icmp)
[IDE](#ide)
[IDP](#idp)
[IMAD](#imad)
[IMADSP](#imadsp)
[IMNMX](#imnmx)
[IMUL](#imul)
[IPA](#ipa)
[ISBERD](#isberd)
[ISCADD](#iscadd)
[ISET](#iset)
[ISETP](#isetp)
[JCAL](#jcal)
[JMP](#jmp)
[JMX](#jmx)
[KIL](#kil)
[LD](#ld)
[LDC](#ldc)
[LDG](#ldg)
[LDL](#ldl)
[LDS](#lds)
[LEA](#lea)
[LEPC](#lepc)
[LONGJMP](#longjmp)
[LOP](#lop)
[LOP3](#lop3)
[MEMBAR](#membar)
[MOV](#mov)
[MUFU](#mufu)
[NOP](#nop)
[OUT](#out)
[P2R](#p2r)
[PBK](#pbk)
[PCNT](#pcnt)
[PEXIT](#pexit)
[PIXLD](#pixld)
[PLONGJMP](#plongjmp)
[POPC](#popc)
[PRET](#pret)
[PRMT](#prmt)
[PSET](#pset)
[PSETP](#psetp)
[R2B](#r2b)
[R2P](#r2p)
[RAM](#ram)
[RED](#red)
[RET](#ret)
[RRO](#rro)
[RTT](#rtt)
[S2R](#s2r)
[SAM](#sam)
[SEL](#sel)
[SETCRSPTR](#setcrsptr)
[SETLMEMBASE](#setlmembase)
[SHF](#shf)
[SHFL](#shfl)
[SHL](#shl)
[SHR](#shr)
[SSY](#ssy)
[ST](#st)
[STG](#stg)
[STL](#stl)
[STP](#stp)
[STS](#sts)
[SUATOM](#suatom)
[SULD](#suld)
[SURED](#sured)
[SUST](#sust)
[SYNC](#sync)
[TEX](#tex)
[TLD](#tld)
[TLD4](#tld4)
[TMML](#tmml)
[TXA](#txa)
[TXD](#txd)
[TXQ](#txq)
[VABSDIFF](#vabsdiff)
[VABSDIFF4](#vabsdiff4)
[VADD](#vadd)
[VMAD](#vmad)
[VMNMX](#vmnmx)
[VOTE](#vote)
[VSET](#vset)
[VSETP](#vsetp)
[VSHL](#vshl)
[VSHR](#vshr)
[XMAD](#xmad)
<!-- /TOC -->

NOTE: Regenerate TOC with `grep '^# ' docs/NvidiaGpu.md | tail -n +2 | sed 's/^# //' | tr 'A-Z' 'a-z' | awk '{print "["toupper($0)"](#"$0")"}'`. GitHub lowercases heading anchors, so the TOC must link lowercase or nothing scrolls.

The numbers (in binary) represent the opcodes; `-` signifies "don't care".

# AL2P
`1110 1111 1010 0---`

# ALD
`1110 1111 1101 1---`

# AST
`1110 1111 1111 0---`

# ATOM
- **ATOM_cas**: `1110 1110 1111 ----`
- **ATOM**: `1110 1101 ---- ----`

Atomic operation.

- INC, DEC for U32/S32/U64 does nothing.
- ADD, INC, DEC for S64 does nothing.
- Only ADD does something for F32.
- Only ADD, MIN and MAX does something for F16x2.

# ATOMS
- **ATOMS_cas**: `1110 1110 ---- ----`
- **ATOMS**: `1110 1100 ---- ----`

# B2R
`1111 0000 1011 1---`

# BAR
`1111 0000 1010 1---`

# BFE
- **BFE_reg**: `0101 1100 0000 0---`
- **BFE_cbuf**: `0100 1100 0000 0---`
- **BFE_imm**: `0011 100- 0000 0---`

Bit Field Extract.

# BFI
- **BFI_reg**: `0101 1011 1111 0---`
- **BFI_rc**: `0101 0011 1111 0---`
- **BFI_cr**: `0100 1011 1111 0---`
- **BFI_imm**: `0011 011- 1111 0---`

Bit Field Insert.

# BPT
`1110 0011 1010 ----`

Breakpoint trap.

# BRA
`1110 0010 0100 ----`

Relative branch.

# BRK
`1110 0011 0100 ----`

Break.

# BRX
`1110 0010 0101 ----`

# CAL
`1110 0010 0110 ----`

# CCTL
`1110 1111 011- ----`

Cache Control.

# CCTLL
`1110 1111 100- ----`

Texture Cache Control.

# CONT
`1110 0011 0101 ----`

Continue.

# CS2R
`0101 0000 1100 1---`

Move Special Register to Register.

# CSET
`0101 0000 1001 1---`

Test Condition Code And Set.

# CSETP
`0101 0000 1010 0---`

Test Condition Code and Set Predicate.

# DADD
- **DADD_reg**: `0101 1100 0111 0---`
- **DADD_cbuf**: `0100 1100 0111 0---`
- **DADD_imm**: `0011 100- 0111 0---`

# DEPBAR
`1111 0000 1111 0---`

# DFMA
- **DFMA_reg**: `0101 1011 0111 ----`
- **DFMA_rc**: `0101 0011 0111 ----`
- **DFMA_cr**: `0100 1011 0111 ----`
- **DFMA_imm**: `0011 011- 0111 ----`

FP64 Fused Multiply Add.

# DMNMX
- **DMNMX_reg**: `0101 1100 0101 0---`
- **DMNMX_cbuf**: `0100 1100 0101 0---`
- **DMNMX_imm**: `0011 100- 0101 0---`

FP64 Minimum/Maximum.

# DMUL
- **DMUL_reg**: `0101 1100 1000 0---`
- **DMUL_cbuf**: `0100 1100 1000 0---`
- **DMUL_imm**: `0011 100- 1000 0---`

FP64 Multiply.

# DSET
- **DSET_reg**: `0101 1001 0--- ----`
- **DSET_cbuf**: `0100 1001 0--- ----`
- **DSET_imm**: `0011 001- 0--- ----`

FP64 Compare And Set.

# DSETP
- **DSETP_reg**: `0101 1011 1000 ----`
- **DSETP_cbuf**: `0100 1011 1000 ----`
- **DSETP_imm**: `0011 011- 1000 ----`

FP64 Compare And Set Predicate.

# EXIT
`1110 0011 0000 ----`

# F2F
- **F2F_reg**: `0101 1100 1010 1---`
- **F2F_cbuf**: `0100 1100 1010 1---`
- **F2F_imm**: `0011 100- 1010 1---`

# F2I
- **F2I_reg**: `0101 1100 1011 0---`
- **F2I_cbuf**: `0100 1100 1011 0---`
- **F2I_imm**: `0011 100- 1011 0---`

# FADD
- **FADD_reg**: `0101 1100 0101 1---`
- **FADD_cbuf**: `0100 1100 0101 1---`
- **FADD_imm**: `0011 100- 0101 1---`
- **FADD32I**: `0000 10-- ---- ----`

FP32 Add.

# FCHK
- **FCHK_reg**: `0101 1100 1000 1---`
- **FCHK_cbuf**: `0100 1100 1000 1---`
- **FCHK_imm**: `0011 100- 1000 1---`

Single Precision FP Divide Range Check.

# FCMP
- **FCMP_reg**: `0101 1011 1010 ----`
- **FCMP_rc**: `0101 0011 1010 ----`
- **FCMP_cr**: `0100 1011 1010 ----`
- **FCMP_imm**: `0011 011- 1010 ----`

FP32 Compare to Zero and Select Source.

# FFMA
- **FFMA_reg**: `0101 1001 1--- ----`
- **FFMA_rc**: `0101 0001 1--- ----`
- **FFMA_cr**: `0100 1001 1--- ----`
- **FFMA_imm**: `0011 001- 1--- ----`
- **FFMA32I**: `0000 11-- ---- ----`

FP32 Fused Multiply and Add.

# FLO
- **FLO_reg**: `0101 1100 0011 0---`
- **FLO_cbuf**: `0100 1100 0011 0---`
- **FLO_imm**: `0011 100- 0011 0---`

# FMNMX
- **FMNMX_reg**: `0101 1100 0110 0---`
- **FMNMX_cbuf**: `0100 1100 0110 0---`
- **FMNMX_imm**: `0011 100- 0110 0---`

FP32 Minimum/Maximum.

# FMUL
- **FMUL_reg**: `0101 1100 0110 1---`
- **FMUL_cbuf**: `0100 1100 0110 1---`
- **FMUL_imm**: `0011 100- 0110 1---`
- **FMUL32I**: `0001 1110 ---- ----`

FP32 Multiply.

# FSET
- **FSET_reg**: `0101 1000 ---- ----`
- **FSET_cbuf**: `0100 1000 ---- ----`
- **FSET_imm**: `0011 000- ---- ----`

FP32 Compare And Set.

# FSETP
- **FSETP_reg**: `0101 1011 1011 ----`
- **FSETP_cbuf**: `0100 1011 1011 ----`
- **FSETP_imm**: `0011 011- 1011 ----`

FP32 Compare And Set Predicate.

# FSWZADD
`0101 0000 1111 1---`

FP32 Add used for FSWZ emulation.

# GETCRSPTR
`1110 0010 1100 ----`

# GETLMEMBASE
`1110 0010 1101 ----`

# HADD2
- **HADD2_reg**: `0101 1101 0001 0---`
- **HADD2_cbuf**: `0111 101- 1--- ----`
- **HADD2_imm**: `0111 101- 0--- ----`
- **HADD2_32I**: `0010 110- ---- ----`

FP16 Add.

# HFMA2
- **HFMA2_reg**: `0101 1101 0000 0---`
- **HFMA2_rc**: `0110 0--- 1--- ----`
- **HFMA2_cr**: `0111 0--- 1--- ----`
- **HFMA2_imm**: `0111 0--- 0--- ----`
- **HFMA2_32I**: `0010 100- ---- ----`

FP16 Fused Multiply Add.

# HMUL2
- **HMUL2_reg**: `0101 1101 0000 1---`
- **HMUL2_cbuf**: `0111 100- 1--- ----`
- **HMUL2_imm**: `0111 100- 0--- ----`
- **HMUL2_32I**: `0010 101- ---- ----`

FP16 Multiply.

# HSET2
- **HSET2_reg**: `0101 1101 0001 1---`
- **HSET2_cbuf**: `0111 110- 1--- ----`
- **HSET2_imm**: `0111 110- 0--- ----`

FP16 Compare And Set.

# HSETP2
- **HSETP2_reg**: `0101 1101 0010 0---`
- **HSETP2_cbuf**: `0111 111- 1--- ----`
- **HSETP2_imm**: `0111 111- 0--- ----`

FP16 Compare And Set Predicate.

# I2F
- **I2F_reg**: `0101 1100 1011 1---`
- **I2F_cbuf**: `0100 1100 1011 1---`
- **I2F_imm**: `0011 100- 1011 1---`

# I2I
- **I2I_reg**: `0101 1100 1110 0---`
- **I2I_cbuf**: `0100 1100 1110 0---`
- **I2I_imm**: `0011 100- 1110 0---`

# IADD
- **IADD_reg**: `0101 1100 0001 0---`
- **IADD_cbuf**: `0100 1100 0001 0---`
- **IADD_imm**: `0011 100- 0001 0---`

Integer Addition.

# IADD3
- **IADD3_reg**: `0101 1100 1100 ----`
- **IADD3_cbuf**: `0100 1100 1100 ----`
- **IADD3_imm**: `0011 100- 1100 ----`
- **IADD32I**: `0001 110- ---- ----`

3-input Integer Addition.

# ICMP
- **ICMP_reg**: `0101 1011 0100 ----`
- **ICMP_rc**: `0101 0011 0100 ----`
- **ICMP_cr**: `0100 1011 0100 ----`
- **ICMP_imm**: `0011 011- 0100 ----`

Integer Compare to Zero and Select Source.

# IDE
`1110 0011 1001 ----`

# IDP
- **IDP_reg**: `0101 0011 1111 1---`
- **IDP_imm**: `0101 0011 1101 1---`

# IMAD
- **IMAD_reg**: `0101 1010 0--- ----`
- **IMAD_rc**: `0101 0010 0--- ----`
- **IMAD_cr**: `0100 1010 0--- ----`
- **IMAD_imm**: `0011 010- 0--- ----`
- **IMAD32I**: `1000 00-- ---- ----`

Integer Multiply And Add.

# IMADSP
- **IMADSP_reg**: `0101 1010 1--- ----`
- **IMADSP_rc**: `0101 0010 1--- ----`
- **IMADSP_cr**: `0100 1010 1--- ----`
- **IMADSP_imm**: `0011 010- 1--- ----`

Extracted Integer Multiply And Add..

# IMNMX
- **IMNMX_reg**: `0101 1100 0010 0---`
- **IMNMX_cbuf**: `0100 1100 0010 0---`
- **IMNMX_imm**: `0011 100- 0010 0---`

Integer Minimum/Maximum.

# IMUL
- **IMUL_reg**: `0101 1100 0011 1---`
- **IMUL_cbuf**: `0100 1100 0011 1---`
- **IMUL_imm**: `0011 100- 0011 1---`
- **IMUL32I**: `0001 1111 ---- ----`

Integer Multiply.

# IPA
`1110 0000 ---- ----`

# ISBERD
`1110 1111 1101 0---`

In-Stage-Buffer Entry Read.

# ISCADD
- **ISCADD_reg**: `0101 1100 0001 1---`
- **ISCADD_cbuf**: `0100 1100 0001 1---`
- **ISCADD_imm**: `0011 100- 0001 1---`
- **ISCADD32I**: `0001 01-- ---- ----`

Scaled Integer Addition.

# ISET
- **ISET_reg**: `0101 1011 0101 ----`
- **ISET_cbuf**: `0100 1011 0101 ----`
- **ISET_imm**: `0011 011- 0101 ----`

Integer Compare And Set.

# ISETP
- **ISETP_reg**: `0101 1011 0110 ----`
- **ISETP_cbuf**: `0100 1011 0110 ----`
- **ISETP_imm**: `0011 011- 0110 ----`

Integer Compare And Set Predicate.

# JCAL
`1110 0010 0010 ----`

Absolute Call.

# JMP
`1110 0010 0001 ----`

Absolute Jump.

# JMX
`1110 0010 0000 ----`

Absolute Jump Indirect.

# KIL
`1110 0011 0011 ----`

# LD
`100- ---- ---- ----`

Load from generic Memory.

# LDC
`1110 1111 1001 0---`

Load Constant.

# LDG
`1110 1110 1101 0---`

Load from Global Memory.

# LDL
`1110 1111 0100 0---`

Load within Local Memory Window.

# LDS
`1110 1111 0100 1---`

Load within Shared Memory Window.

# LEA
- **LEA_hi_reg**: `0101 1011 1101 1---`
- **LEA_hi_cbuf**: `0001 10-- ---- ----`
- **LEA_lo_reg**: `0101 1011 1101 0---`
- **LEA_lo_cbuf**: `0100 1011 1101 ----`
- **LEA_lo_imm**: `0011 011- 1101 0---`

# LEPC
`0101 0000 1101 0---`

# LONGJMP
`1110 0011 0001 ----`

# LOP
- **LOP_reg**: `0101 1100 0100 0---`
- **LOP_cbuf**: `0100 1100 0100 0---`
- **LOP_imm**: `0011 100- 0100 0---`

# LOP3
- **LOP3_reg**: `0101 1011 1110 0---`
- **LOP3_cbuf**: `0000 001- ---- ----`
- **LOP3_imm**: `0011 11-- ---- ----`
- **LOP32I**: `0000 01-- ---- ----`

# MEMBAR
`1110 1111 1001 1---`

Memory Barrier.

# MOV
- **MOV_reg**: `0101 1100 1001 1---`
- **MOV_cbuf**: `0100 1100 1001 1---`
- **MOV_imm**: `0011 100- 1001 1---`
- **MOV32I**: `0000 0001 0000 ----`

# MUFU
`0101 0000 1000 0---`

Multi Function Operation.

# NOP
`0101 0000 1011 0---`

No operation.

# OUT
- **OUT_reg**: `1111 1011 1110 0---`
- **OUT_cbuf**: `1110 1011 1110 0---`
- **OUT_imm**: `1111 011- 1110 0---`

# P2R
- **P2R_reg**: `0101 1100 1110 1---`
- **P2R_cbuf**: `0100 1100 1110 1---`
- **P2R_imm**: `0011 1000 1110 1---`

Move Predicate Register To Register.

# PBK
`1110 0010 1010 ----`

Pre-break.

# PCNT
`1110 0010 1011 ----`

Pre-continue.

# PEXIT
`1110 0010 0011 ----`

Pre-exit.

# PIXLD
`1110 1111 1110 1---`

# PLONGJMP
`1110 0010 1000 ----`

Pre-long jump.

# POPC
- **POPC_reg**: `0101 1100 0000 1---`
- **POPC_cbuf**: `0100 1100 0000 1---`
- **POPC_imm**: `0011 100- 0000 1---`

Population/Bit count.

# PRET
`1110 0010 0111 ----`

Pre-return from subroutine. Pushes the return address to the CRS stack.

# PRMT
- **PRMT_reg**: `0101 1011 1100 ----`
- **PRMT_rc**: `0101 0011 1100 ----`
- **PRMT_cr**: `0100 1011 1100 ----`
- **PRMT_imm**: `0011 011- 1100 ----`

# PSET
`0101 0000 1000 1---`

Combine Predicates and Set.

# PSETP
`0101 0000 1001 0---`

Combine Predicates and Set Predicate.

# R2B
`1111 0000 1100 0---`

Move Register to Barrier.

# R2P
- **R2P_reg**: `0101 1100 1111 0---`
- **R2P_cbuf**: `0100 1100 1111 0---`
- **R2P_imm**: `0011 100- 1111 0---`

Move Register To Predicate/CC Register.

# RAM
`1110 0011 1000 ----`

# RED
`1110 1011 1111 1---`

Reduction Operation on Generic Memory.

# RET
`1110 0011 0010 ----`

Return.

# RRO
- **RRO_reg**: `0101 1100 1001 0---`
- **RRO_cbuf**: `0100 1100 1001 0---`
- **RRO_imm**: `0011 100- 1001 0---`

# RTT
`1110 0011 0110 ----`

# S2R
`1111 0000 1100 1---`

# SAM
`1110 0011 0111 ----`

# SEL
- **SEL_reg**: `0101 1100 1010 0---`
- **SEL_cbuf**: `0100 1100 1010 0---`
- **SEL_imm**: `0011 100- 1010 0---`

# SETCRSPTR
`1110 0010 1110 ----`

# SETLMEMBASE
`1110 0010 1111 ----`

# SHF
- **SHF_l_reg**: `0101 1011 1111 1---`
- **SHF_l_imm**: `0011 011- 1111 1---`
- **SHF_r_reg**: `0101 1100 1111 1---`
- **SHF_r_imm**: `0011 100- 1111 1---`

# SHFL
`1110 1111 0001 0---`

# SHL
- **SHL_reg**: `0101 1100 0100 1---`
- **SHL_cbuf**: `0100 1100 0100 1---`
- **SHL_imm**: `0011 100- 0100 1---`

# SHR
- **SHR_reg**: `0101 1100 0010 1---`
- **SHR_cbuf**: `0100 1100 0010 1---`
- **SHR_imm**: `0011 100- 0010 1---`

# SSY
`1110 0010 1001 ----`

Set Synchronization Point.

# ST
`101- ---- ---- ----`

Store to generic Memory.

# STG
`1110 1110 1101 1---`

Store to global Memory.

# STL
`1110 1111 0101 0---`

Store within Local or Shared Window.

# STP
`1110 1110 1010 0---`

Store to generic Memory and Predicate.

# STS
`1110 1111 0101 1---`

Store within Local or Shared Window.

# SUATOM
- **SUATOM**: `1110 1010 0--- ----`
- **SUATOM_cas**: `1110 1010 1--- ----`

Atomic Op on Surface Memory.

# SULD
`1110 1011 000- ----`

Surface Load.

# SURED
`1110 1011 010- ----`

Reduction Op on Surface Memory.

# SUST
`1110 1011 001- ----`

Surface Store.

# SYNC
`1111 0000 1111 1---`

# TEX
- **TEX**: `1100 0--- ---- ----`
- **TEX_b**: `1101 1110 10-- ----`
- **TEXS**: `1101 -00- ---- ----`

Texture Fetch with scalar/non-vec4 source/destinations.

# TLD
- **TLD**: `1101 1100 ---- ----`
- **TLD_b**: `1101 1101 ---- ----`
- **TLDS**: `1101 -01- ---- ----`

Texture Load with scalar/non-vec4 source/destinations.

# TLD4
- **TLD4**: `1100 10-- ---- ----`
- **TLD4_b**: `1101 1110 11-- ----`
- **TLD4S**: `1101 1111 -0-- ----`

Texture Load 4 with scalar/non-vec4 source/destinations.

# TMML
- **TMML**: `1101 1111 0101 1---`
- **TMML_b**: `1101 1111 0110 0---`

Texture MipMap Level.

# TXA
`1101 1111 0100 0---`

# TXD
- **TXD**: `1101 1110 00-- ----`
- **TXD_b**: `1101 1110 01-- ----`

Texture Fetch With Derivatives.

# TXQ
- **TXQ**: `1101 1111 0100 1---`
- **TXQ_b**: `1101 1111 0101 0---`

Texture Query.

# VABSDIFF
`0101 0100 ---- ----`

# VABSDIFF4
`0101 0000 0--- ----`

# VADD
`0010 00-- ---- ----`

# VMAD
`0101 1111 ---- ----`

# VMNMX
`0011 101- ---- ----`

# VOTE
- **VOTE**: `0101 0000 1101 1---`
- **VOTE_vtg**: `0101 0000 1110 0---`

Vote Across SIMD Thread Group

`VOTE_vtg` is a kepler leftover.

# VSET
`0100 000- ---- ----`

# VSETP
`0101 0000 1111 0---`

# VSHL
`0101 0111 ---- ----`

# VSHR
`0101 0110 ---- ----`

# XMAD
- **XMAD_reg**: `0101 1011 00-- ----`
- **XMAD_rc**: `0101 0001 0--- ----`
- **XMAD_cr**: `0100 111- ---- ----`
- **XMAD_imm**: `0011 011- 00-- ----`

Integer Short Multiply Add.
