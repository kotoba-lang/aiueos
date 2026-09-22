# ADR-0222 — the Bonsai stage: Murakumo inference on pure AIUEOS is the K16 CPU path plus two codecs, not the B70 GPU path moved over

- Status: accepted (plan; nothing in this ADR has executed)
- Date: 2026-09-22
- Owner question: 「実際に amu native compiler, binary のみで書かれた aiueos の上で
  murakumo inference が動くようにするには? inference は ternary bonsai 2 27b ptq1
  を murakumo by amu native binary で」
- Extends ADR-0220 (the whole-kernel programme; its "Inference" paragraph is
  the cutover this ADR adds a model to) and ADR-0221 (dequant + live matvec).
  Relates to ADR-0117/0119 (model transport), ADR-0120/0145 (graph
  admission), ADR-0121 (first token), ADR-0196 (`evaluate_token` budget),
  ADR-0205 (the missing tender), ADR-0207 (cluster admission), ADR-0211
  (calibrated counting), ADR-0212 (conversion order).
- Upstream evidence read, not re-measured: kotoba-lang/inference
  `verify/native/gpu/README.md` iteration 66 and
  `verify/evidence/ternary-bonsai-ptq1-native-e2e-20260920.json` (origin/main
  2026-09-22; the local checkout was 82 commits behind and was not used).

## Two streams that have not met

Everything below is measured state as of 2026-09-22, read from the ADRs and
evidence files named. Nothing was booted for this ADR.

| | stream A — kotoba-lang/inference on B70 / Xavier | stream B — this repository on the K16 |
|---|---|---|
| model | Ternary Bonsai 2 27B `PTQ1_0`, sha256 `53107f53…`, GGUF architecture `qwen35`, 48 recurrent + 16 full-attention layers, base model Qwen3.8-27B | Qwen3.8 27B, 10,934,860,704 bytes, GGUF architecture `qwen35`, the SAME 48 + 16 layer graph (ADR-0120) |
| correctness | greedy `Hello` → `11, 353, 2688, 264, 5286, 303, 279, 3694`, 8/8 = Prism llama.cpp `prism-b10709-9a9394a`; tokenizer 18/18; template 3/3 | first token BOS `248044` → `2005`, runner-up `17`, = llama.cpp Metal, **on the physical K16** (ADR-0121); decode stopped at `T02 L04 SOFTMAX` (KV alias), decode tok/s `N/A` |
| where the arithmetic runs | GPU: Vulkan through amu's wire 42 — `tools/kexe_gpu_vulkan.c` (C, supervisor side) on Mesa ANV / RADV / nvgpu (C) | CPU: all fifteen tensor types dequantise in Kotoba and the live matvec is the Kotoba object (ADR-0221); `qwen35_infer.c` (1,844 lines: orchestration, AVX2 arm, SMP split, KV/recurrent state) is C |
| what is under it | Linux, systemd (`murakumo-b70-bonsai-native.service` :8092), Node/kbb for `serve_http.cljk` and for generating the guest | pure AIUEOS boot, RTL8125 direct TLS to Murakumo (ADR-0127), job protocol / device-worker signing / relay as Kotoba objects (ADR-0215/0219/0220) |
| serving | 125.2 ms/step GPU, 7.96 tok/s aggregate at 1–8 concurrent requests (`batch-rows = 0`); the public gateway did not route to it | one job per boot; the K16 is in `/infer/nodes` as `aiueos-micro-infer` (bigram), `admission "pending"` (ADR-0207); no tender on hardware, so fuel, code and state are all refreshed by reboot (ADR-0205) |
| the only measured CPU number | — | time to first token 46,666,864,001 ns, C scalar/AVX2, one token (ADR-0127) |

Stream A satisfies the owner's condition — "compiled by amu, native binary" —
only for the guest `.kotoba`. The loader, the GPU driver and the HTTP shell
are C and JavaScript on Linux. Moving stream A onto AIUEOS would mean a Vulkan
driver in Kotoba; AIUEOS has virtio-gpu 2D (ADR-0084/0094) and nothing else,
ANV/RADV are C in the 10⁵-line class, and a Venus/virtio-gpu passthrough puts
Linux back under the kernel. **A GPU on AIUEOS is not a stage of this plan.
Bonsai on AIUEOS is the CPU path.** That is the first decision, and it fixes
the class of the number to expect: the K16 has produced one Qwen token in
46.7 s in C; the Kotoba objects are scalar four-accumulator loops that
ADR-0220 already says will be slower than the AVX2 they replace. The number
is to be measured and printed, not predicted here.

## Why the K16 path is the road

Bonsai's GGUF declares `qwen35` and its layer split is Qwen3.8's. The three
admission objects (`qwen35-gguf-header-valid`, `-kv-scan`,
`-tensor-table-bind`, ADR-0145), the thirteen forward-pass objects
(ADR-0147/0148/0221), the tokenizer objects (ADR-0139) and the transport
(ADR-0117/0119) were all built against exactly this graph. What Bonsai adds
is **two tensor codecs and one basis change** — nothing structural.

## The stages, in the order ADR-0212 imposes

### Stage A — Bonsai enters the model plane (new work is codec only)

1. **`PTQ1_0` (GGML type 143), `PQ2_0` (142) and BF16 in
   `aiueos.lib.qwen35-dequant-core`.** The arithmetic's source of truth is
   Prism llama.cpp; stream A already holds it as `kdot_ptq1_dual_r1.comp` and
   `embed_iq4xs.comp -DPTQ1`, and its f64 oracle `dense_ref.py`. The block
   layout is `qs[24], qh[2], d` for PTQ1_0 and deliberately does NOT share
   PQ2_0's `d, qs[32]` offsets (stream A, 2026-09-20). Block size pinned
   2026-09-22 by tiling: **28 bytes per 128 elements** is the only size under
   which the 851 extents end at the file's last byte
   (`contracts/bonsai2-qwen35-runtime-v1.edn`, `bonsai2-qwen35-contract-test`).
   Contract vectors are
   cut from the real artifact and graded by Prism llama.cpp, the way
   ADR-0221's were cut and graded by the C — one row per type, one and two
   blocks, and a deliberately broken decoder shown red on the named vector.
   **Landed 2026-09-22 (PTQ1_0 and BF16; PQ2_0 is not in the file).**
   `aiueos.lib.qwen35-dequant-core` decodes type 143 (`ptq10-value`, a port
   of `dequantize_row_ptq1_0` from PrismML-Eng/llama.cpp
   `9a9394a895b96003ca842a6041cb28ac49a108f7`) and type 30 (`bits << 16`);
   `qwen35-dequant-row` and `qwen35-matvec` gain both through the shared
   core. Ten new contract vectors (48 total; 5 PTQ1_0 rows of which 3 are
   real blocks from `blk.0.attn_gate.weight` and `output.weight`, 2 BF16 rows
   of which 1 is `blk.0.ssm_alpha.weight`, 3 refusals), expected bytes from a
   `cc -O2` harness holding the Prism C verbatim: the KIR oracle passes all
   48 with 37 memory assertions and every reason observed, and goes red on
   `:ptq1-0-128-real-blk0-attn-gate` with `3^1` changed to 4. Fuel: the
   256-element PTQ1_0 row passes at 4,096 and traps `:fuel-exhausted` at
   2,048 — 8–16 per element against the dequant tier's 256 and the matvec
   tier's 119, so no kotoba-native row moves. Both objects recompile with the
   pinned amu, byte-identical twice, ABI verifier green, no slot-144 host
   call; `qwen35-dequant-row.o` 39,896 → 42,512 and `qwen35-matvec.o`
   44,576 → 47,192 bytes — **5,232 bytes of the ~24 KiB low-region headroom
   ADR-0221 left**, to be re-measured by the next QEMU boot. There is no C
   twin of either type, so the in-kernel `QWEN-PARITY` self-test cannot
   grade them; until a Bonsai boot the oracle vectors are their whole
   evidence.
2. **The signed Hadamard basis change as an object.** Normalized 1024-wide
   Sylvester blocks, explicit signs from GGUF metadata, inverse after the
   embedding, and the Qwen3.8 recurrent `ssm_out` reorder from tiled
   `[hd, nk, rep]` to grouped `[hd, rep, nk]` before the transform. It runs
   before every folded matvec, so it sits in the same arena plan as
   `qwen35-matvec` (ADR-0147's 96-byte plan, offsets 64-bit).
3. **The GGUF metadata scan has to READ an `INT32` array it currently
   skips.** `prism.hadamard.sign_values` is GGUF type 5 (`INT32`) and holds
   −1. `qwen35-gguf-kv-scan.kotoba` today classes int32 arrays as "skip"
   (its own table: `7 int32 array + skip`) and reads every scalar through
   `u32-at`. Stream A's first full Bonsai run produced non-finite activations
   for exactly this reason — type 5 read unsigned turned −1 into
   4294967296 — and that is the bug to refuse here before the first boot,
   not after: a contract vector whose sign array holds −1 and whose expected
   workspace word is the two's-complement pattern. Measured 2026-09-22: the
   array is 28,672 entries (= 5120 + 6144 + 17408, the three folded input
   widths), 114,688 bytes, values {−1, 1}. The kv-scan object bounds one
   `kernel-load-u32` region at 512 bytes and its workspace at 128, so the
   array cannot be COPIED at admission; the admission records its file offset
   and count, and the Hadamard object reads signs from the read-only model
   mapping at use.
4. **A graph contract for the Bonsai artifact**,
   `contracts/bonsai2-qwen35-runtime-v1.edn`: exact byte length, sha256
   `53107f53…`, metadata count, the 64-layer schedule, the tensor table and
   the type distribution (PTQ1_0 / PQ2_0 / BF16 and whatever else the file
   holds — counted from the file, not copied from this ADR). The three
   admission objects take their constants from the contract, so the objects
   do not change; the contract does. **Landed 2026-09-22** from the public
   file's first 32 MiB: 49 metadata keys, 851 tensors in 23 roles
   (48 × 9 + 16 × 6 + 64 × 5 + 3), types `PTQ1_0` 402 / `F32` 353 / `BF16`
   96 — no `PQ2_0` in this file — data offset 11,120,992, no nextn key and no
   MTP tensors. `test/aiueos/bonsai2_qwen35_contract_test.cljk` (6 tests, 43
   assertions on the kbb engine; seen red on the tiling and sign identities
   with the numbers broken) holds the document to its own arithmetic.
5. **Transport is existing mechanism.** FAT32-safe split under `EFI/AIUEOS`
   and the contiguous 2-MiB-aligned LoaderData mapping (ADR-0117); NVMe A/B
   slots for updates (ADR-0119, K16 write still unverified). The Bonsai
   file is **5,946,648,928 bytes** (Hugging Face LFS size, 2026-09-22; the
   B70's copy was not read — the session had no production access — so the
   two copies have not been compared and the sha256 has not been recomputed
   here). Split shape: 4,000,000,000 + 1,946,648,928. It is 4,988,211,776
   bytes smaller than the Qwen3.8 file that did fit the 16 GiB K16 beside
   the kernel; whether it fits is still a boot, not this subtraction.

### Stage B — the forward-pass cutover completes, and a second token exists

6. **The remaining stages of ADR-0220's cutover, in its order:**
   norm + activation → attention → recurrent-step (gated delta net) → rope.
   Rope's C uses x87 `fsincos`, Amu emits no x87, and `f64-sin-bounded`
   exists in the language; the Kotoba rope will not be bit-identical to
   `fsincos`, so for that stage the reference becomes the object and the
   contract vectors are re-cut from Prism llama.cpp rather than from the C.
7. **`evaluate_token` becomes one object.** ADR-0196's estimate is
   5.62 × 10¹¹ fuel per token against the 2⁵³−1 ceiling — an estimate from
   shapes and a constant, not a measurement, and its three named errors all
   push the number up. Bisect the tier in the oracle on the real row range
   (ADR-0220's finding: four objects packaged at the 1,024 default `ud2`'d
   on the first real input) and give kotoba-native the row.
8. **The `T02` failure is retried on the physical K16**, with the KV alias
   fix of ADR-0121's follow-up in place, until eight greedy tokens exist.
   The floor is stream A's: `Hello` → `11, 353, 2688, 264, 5286, 303, 279,
   3694`. A different eighth token is a defect, not a rounding note, until
   the f64 oracle says which engine is right.
9. **tok/s is counted, not read from a clock that is not calibrated.**
   ADR-0211's icount, or the TSC under the `AIUEOS_QWEN38_MODEL_HANDOFF`
   profile — the only profile in which `tsc_hz` is non-zero (CLAUDE.md,
   measured 2026-09-10). `N/A` until then, never zero.
10. **Tokenizer and template.** The `qwen35` tokenizer objects (ADR-0139)
    against Qwen3.5's tokenizer and chat template, including its default
    `xhigh` system instruction. Stream A's oracles exist: 18/18 corpus lines
    equal to llama-server `/tokenize`, 3/3 message shapes byte-equal to
    `/apply-template`. Those files, not new ones.

### Stage C — the throughput lever is the compiler, not the OS

11. **Vector ISA lowering in kotoba-native (AVX2 on x86-64, NEON on
    AArch64).** `kotoba.native.vector-region` exists and emits scalar code;
    the C the objects replace is AVX2. A ternary weight is a sign and a
    magnitude, so PTQ1 matvecs are adds and subtracts over dequantised
    bytes — the shape SIMD pays for most. Without this stage "runs in
    Kotoba" is true and the rate is an order of magnitude under llama.cpp
    on the same CPU; with it the comparison is fair. This is a
    kotoba-native item, ordered after stage B so it optimises a measured
    path.
12. **The SMP split stays C until `smp.c` moves** (ADR-0220 layer 3–5).
    Each half already calls the matvec object with its own row range and
    scratch (ADR-0221); the object does not change when the split does.

### Stage D — the layer that keeps it running

13. **kototama on hardware — the tender ADR-0205 names as absent.** Fuel
    refreshed per step, code arriving as a definition CID without a reset,
    state surviving a deploy. Until it exists "Bonsai generated eight tokens
    on the K16" is a job, not a service: the board reboots every ~20 s and
    every job is bound to a boot nonce. The timer that makes preemption
    possible is already Kotoba — `native/rt-kernel.kotoba` owns the APIC
    and a periodic release — and the K16 stream kernel does not use it.
14. **Murakumo membership.** The K16 sits at `admission "pending"` with
    every other field correct (ADR-0207); `can` gains the Bonsai
    capability name beside `aiueos-micro-infer`; the gateway routes model id
    `prism-ml/Ternary-Bonsai-2-27B-PTQ1_0` to the K16 head. The wiring
    already exists in the other direction — cloud-murakumo-api's
    `xavier_hosted_model.js` routes `nex-n2.5-mini-uncensored` to a native
    head as the fallback of a vLLM primary (stream A, tick 41) — and is the
    shape to copy. The model channel is Kotobase CID blocks + IPNS head
    (ADR-0118), whose physical writer is still red.

### Stage E — "AIUEOS written only in amu-native binaries" is the other wave

15. This is ADR-0220's programme and proceeds bottom-up regardless of
    Bonsai. Position: `os/aiueos/kernel` 21,294 lines of C and assembly,
    106 objects linked, 23 of 30 C files call into Kotoba, seven files
    retired. What blocks most of the rest is not inference: the pointer
    rule (ADR-0212), indirect `ms_abi` calls that stay mechanism until
    `BOOTX64.EFI` is Kotoba, `pci.c` (5,482 lines, layer 6) and `main.c`
    (3,699, layer 5).
16. **Order between the waves: Bonsai tokens first, C deletion after.** The
    in-kernel parity self-test grades every object against the C reference
    at boot (`QWEN-PARITY dequant ok / dot ok / matvec ok`, ADR-0221).
    Retiring `qwen35_infer.c` before a Bonsai token exists removes the
    instrument that would have graded the Bonsai objects. The C leaves
    "when the K16 has measured the object's token rate" (ADR-0221) — for
    Bonsai as for Qwen3.8.

## What is refused

- **Porting Vulkan.** GPUs stay with the Linux boxes (B70, Xavier); the two
  streams share Prism llama.cpp as oracle and share nothing else at run time.
- **Reusing kotoba-lang/inference's `.kotoba` in the kernel.**
  `kernel_math_core.kotoba` and its siblings are host-target (f64 rows
  handed in by a host); ADR-0220 already names the reuse target as the
  thirteen objects here. Stream A's GLSL and Kotoba here are two copies of
  the PTQ1 arithmetic; that is acceptable exactly as long as both are graded
  by the same Prism vectors.
- **Reading "links" or "passes its contract" as "ran".** Four objects landed
  green and would have jumped to address 0 on first execution (ADR-0220,
  CLAUDE.md §5). Every stage above ends at a boot marker or a K16 receipt.
- **Predicting the token rate.** One measured CPU data point exists and it
  is 46.7 s to one token of a different quantisation in C. Bonsai's PTQ1 is
  fewer bytes per weight (memory-bound favours it) and adds a Hadamard per
  folded matvec (compute disfavours it); which wins is a measurement.

## Not measured, and named as such

Whether the artifact fits the 16 GiB K16 beside the kernel image (its byte
count is measured; the mapping is not); the B70 copy against the Hub copy;
the sha256 recomputed by this repository; the CPU-bound rate of a PTQ1 matvec on the K16's Ryzen; the
K16's NVMe slot write; sampling-distribution parity (stream A lists it as
unmeasured too); contexts past 4,096 tokens; any board other than the K16
booting AIUEOS on real hardware (P5 remains UNVERIFIED, ADR-0084).

## Done means

1. `AIUEOS_BONSAI_GRAPH_READY` — the Bonsai contract admitted by the three
   objects on the K16, with the `INT32` sign array read as signed (marker
   naming follows `os-coverage.cljs`'s `/AIUEOS_[A-Z0-9_]+_OK/` for anything
   that should score).
2. Eight greedy tokens for `Hello` equal to Prism llama.cpp's, printed with
   the object's reason codes on the console and a decode count from ADR-0211.
3. A Murakumo job result signed by the device worker (ADR-0137) carrying
   those token ids, posted through the RTL8125 TLS path, and the K16's
   admission `accepted`.
4. Then, and only then, the token rate with the C reference deleted.
