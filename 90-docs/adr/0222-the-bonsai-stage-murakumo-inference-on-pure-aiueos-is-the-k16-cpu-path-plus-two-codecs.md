# ADR-0222 — the Bonsai stage: Murakumo inference on pure AIUEOS is the K16 CPU path plus two codecs, not the B70 GPU path moved over

- Status: accepted (stage A is landing item by item — the PTQ1_0 dequant,
  the Hadamard object, the two-profile admission objects, the graph
  contract and the C translation are in the tree and graded; stage B item 6's
  first step — norm + activation live on the objects — is in the tree and
  parity-checked on a CPU, but no forward pass has run through it on any
  machine, and the Bonsai profile has not generated a token)
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

**Measured on the B70 2026-09-22, and it changes what stream A's row means:**
the unit `murakumo-b70-bonsai-native.service` is active but its ExecStart is
the Prism **llama-server** on :8093; :8092 is `murakumo-mishima-fleet-router`,
whose heads include that llama-server; no `kexe-loader` / `serve_http` /
`resident-replay` process runs and no unit under `/etc/systemd/system`
mentions either. So the native guest is **not serving today** — traffic for
`prism-ml/Ternary-Bonsai-2-27B-PTQ1_0` is answered by llama.cpp. That does
not touch the 2026-09-20 measurements (they were taken with the guest
running, and the artifact digest is now confirmed identical); it means the
row "stream A serves Bonsai natively" is a past measurement, not a running
system — and stage D's point, that nothing here holds a service across a
restart, is the same point one box over. Recorded in
kotoba-lang/inference `verify/evidence/ternary-bonsai-ptq1-native-e2e-20260920.json`.

Also 2026-09-22: the wire-42 GPU path gained a **fourth backend** — Apple
GPUs through MoltenVK (amu PR #1046: a portability driver is invisible to the
Vulkan loader unless the instance asks for `VK_KHR_portability_enumeration`,
so every Mac had answered `VK_ERROR_INCOMPATIBLE_DRIVER`). An M1 Max runs the
same guests at 146–163 GB/s (B70 160, Xavier 54, K16 44), `dot.kotoba` matching
an f64 twin to 3.1e-4. It does not move this ADR's plan: the AIUEOS path is
still CPU (stage A of this document), and no weights were mapped on that
backend.

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
   44,576 → 47,192 bytes — 5,232 bytes of the low-region headroom ADR-0221
   left. **Re-measured by a QEMU boot 2026-09-22 at `624a01d`**
   (`AIUEOS_QWEN35_KOTOBA_PARITY=1 smoke-qemu-uefi.sh`, exit 0,
   `IMAGE-FRESH artifacts=3`): the parity profile links the grown objects,
   `QWEN-PARITY dequant ok` / `dot ok` / `matvec ok` over the fifteen
   C-twin types, `AIUEOS_UEFI_SMOKE_OK`, 76 distinct `_OK` markers.
   `KERNEL.ELF` 635,528 bytes; its lowest PT_LOAD segments end at 0x186fd2 /
   0x195fac / 0x1cc758 / 0x1cd14c against the 0x1f4000 limit (159,412 bytes
   under it at the tightest), the 2.7 MB `.high_bss` above. That is the
   `--gc-sections` parity image, not the production profile's two-segment
   loader, so the production headroom is still ADR-0221's number minus
   5,232 until that profile boots. There is no C twin of PTQ1_0 or BF16, so
   the self-test cannot grade them; until a Bonsai boot the oracle vectors
   are their whole evidence.
2. **The signed Hadamard basis change as an object.** Normalized 1024-wide
   Sylvester blocks, explicit signs from GGUF metadata, inverse after the
   embedding, and the Qwen3.8 recurrent `ssm_out` reorder from tiled
   `[hd, nk, rep]` to grouped `[hd, rep, nk]` before the transform. It runs
   before every folded matvec, so it sits in the same arena plan as
   `qwen35-matvec` (ADR-0147's 96-byte plan, offsets 64-bit).
   **Landed 2026-09-22** as `qwen35-hadamard.kotoba` (own object, own
   64-byte plan: mode / width / input / signs / output / perm hd, nk, rep;
   `[arena arena-length plan plan-length]`). The oracle is Prism llama.cpp
   `9a9394a`'s own CPU path (`tests/prism_hadamard_oracle.c`: `build_lora_mm`
   permute → `ggml_mul` signs → `fwht`; `build_inp_embd` fwht → signs; the
   scalar butterfly loop of `ggml_compute_forward_fwht_impl`, whose SIMD pass
   is the same function bit for bit). 22 contract vectors — 8 transforms
   including the artifact's REAL 5120 and 6144 sign vectors and the ssm_out
   [128 16 3] permutation, 14 refusals, reasons −10..0 all observed — pass
   in the KIR oracle (`bonsai-hadamard-contract`, ~6 min); red with `u − v`
   written `v − u`. Fuel bisected on the real 6144 case: passes 524,288,
   traps 262,144 → 43–85 per element; kotoba-native #190 gives the object
   its row and a 33,554,432 tier (6.0× the 65,536-element ceiling), amu
   #1045 advances the pin (a9f8a3c → 207db01) — the previous pin refuses
   the object by name ("declares an aiueos export with no admitted symbol",
   measured). Native: 8,592 bytes, fuel word 33,554,432 in the object,
   deterministic, ABI verifier green, no slot-144 call
   (`bonsai-hadamard-native`). Found on the way: every element walk had to be
   chunked per 1024-block — a 2,048-deep recursion exhausted node's stack in
   the oracle where 1,024 did not; the machine code loops either way. Not
   booted; not linked into `KERNEL.ELF`; the K16 caller that packs its plan is
   stage B.
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
   mapping at use. The object now reads them exactly so — `sign_values` is
   `+1` / `0xFFFFFFFF` as INT32 words at a plan offset, refused by reason
   −10 for any other word. **The kv-scan side landed 2026-09-22 (option (a),
   owner's choice): the admission objects carry two profiles and no new
   argument.** `qwen35-gguf-header-valid` names the profile from the
   artifact byte length (10,934,860,704 / 5,946,648,928) and requires the
   matching counts; `qwen35-gguf-kv-scan` names it from the header's counts
   (866 / 50 vs 851 / 49), records it as bit 31 of the required-key mask,
   and every constant follows — the 40 Bonsai keys (30 of Qwen3.8's, not
   `nextn_predict_layers`, plus the ten `prism.hadamard.*`), `general.name`
   "Hf", padding 248044, file type 143, block_count 64, metadata end
   11,070,652, a 144-byte workspace whose four new words are the sign
   array's file offset (13,360) and count (28,672), the file type and the
   high mask. A Prism key under profile 0, or `nextn` under profile 1, is
   refused as −13 rather than skipped. Evidence: the Bonsai profile's
   contracts run in the KIR oracle over the REAL header (first 11,120,992
   bytes of the public file, sha256-pinned, refetched by range, refused when
   absent): header-valid 11/11; kv-scan **15/15 with the 144-byte workspace
   equal to one computed independently in python from the same header**,
   both profile mix-ups (−7, −201), one refusal per Prism key (−232, −237,
   −238, −239, −240) and the two end-of-section scalars (−227, −230). ~32
   minutes for the 15 (four full walks of 495,907 strings at ~7 min each).
   The Qwen3.8 profile is byte-for-byte the old behaviour by construction
   (128-byte workspace, same codes); the QEMU admission smoke
   (`smoke-qemu-qwen35-admission.cljk`, 2026-09-22, this tree) booted the
   rebuilt objects on the CPU against the 10,996,640-byte Qwen3.8 fixture:
   `QWEN-ADMIT reason=0 stage=0 admitted=1`,
   `AIUEOS_QWEN35_ADMISSION_QEMU_OK objects=3 offsets=match-host-reference`.
   **The Bonsai profile has been on a CPU since 2026-09-23** (below, after
   the C translation). It is still not what the K16 image is built to hand
   off in production — only the fixture branch accepts it. **The fixture for that boot
   landed 2026-09-22** — `tests/make-bonsai-boot-fixture.cljk`, task
   `bonsai-boot-fixture`. It does not SYNTHESISE a header the way
   `tests/make_qwen35_header_fixture.py` does for Qwen3.8: the Bonsai prefix
   is real and obtainable (the 11,120,992 sha256-pinned bytes
   `bonsai-admission-fixture` range-fetches), and it is the same prefix the
   three objects were graded over, so a synthesised second header would put
   the boot on bytes nothing else has read (ADR-0165). What the generator
   adds is the second arm: an independent walk of those bytes that derives —
   from the file, against the graph contract — the metadata end 11,070,652
   after walking 496,309 length-prefixed strings; the 28,672 INT32 signs at
   file offset 13,360, read SIGNED and each one ±1, with the offset and count
   cross-checked against slots 128/132 of the 144-byte workspace the kv-scan
   OBJECT produced; the 851 records with role, dimensions and ggml type; the
   48/16 schedule derived from `full_attention_interval` rather than assumed;
   the histogram 402/353/96; and the extents tiling with no gap to exactly
   `11,120,992 + 5,935,527,936 = 5,946,648,928`. It writes the fixture and a
   receipt of every number the boot gate will assert, so the next gate types
   none of them. Ten controls, each refused by its OWN reason literal
   (`:not-gguf :gguf-version :tensor-count :metadata-count
   :sign-value-not-unit :sign-values-count :unknown-role :role-dimensions
   :role-type :extent-not-contiguous`); the prefix missing is exit 3, not a
   pass. ~0.5 s. Found while writing it: searching for a tensor record by its
   length-prefixed name from byte 0 finds the copy inside the metadata's
   `prism.hadamard.weight_names` array first, and the control that mutated it
   was ADMITTED — the records are located from `metadata-end` now. This is a
   fixture and a walk; the boot that uses it is recorded below. Found on the way:
   amu `b36eb717` no longer compiles the PREVIOUS kv-scan object either —
   its 113-deep `if` key table exhausts the desugar stack ("desugared
   nesting exhausted the host stack"), so `reproduce-kotoba-objects` against
   the current amu could not have reproduced the committed object; the key
   tables are now balanced comparison trees (depth ~8) generated from the
   canonical strings and re-evaluated against them. **`qwen35-tensor-table-bind` followed the same day**: its profile is
   `metadata-end`, which the caller already passes and the kv-scan object
   derived (10,945,379 / 11,070,652), echoed into workspace slot 28144 — so
   again no new argument and no new slot. The two artifacts share every role
   SHAPE (checked: all 23 Bonsai roles carry the dimensions Qwen3.8's do);
   what differs is the record count (851), the artifact length, the data
   offset (11,120,992), the type histogram (`F32` 353 / `BF16` 96 / `PTQ1_0`
   402, counted in slot 31 because 143 would run off a 32-entry table), the
   model-level types (`token_embd` and `output` are PTQ1_0 where Qwen3.8's
   are Q2_K and Q4_K) and the absent MTP layer 64. 12 vectors in the oracle
   over the same fixture, 98.5 s: the admitted case's **28,160-byte
   workspace equals one computed independently in python** (851 binding
   slots, the type counters, the 65 role masks, the cursor), the profile
   mix-up refuses on the table floor (−5: the window that fills a Bonsai
   table cannot hold a Qwen3.8 one), and one mutation per refusal class
   (−21 retype, −20 dimension, −18 name, −16 offset, −13 unknown type).
   Both of its walks had to be chunked for the interpreter, like the kv-scan
   string walk: 851 records and a 7,040-word clear are host-stack overflows
   on node, and the chunk boundary is derived from the index because the ABI
   admits five parameters. After all three objects changed, the Qwen3.8
   profile was booted again: `QWEN-ADMIT reason=0 stage=0 admitted=1`,
   `AIUEOS_QWEN35_ADMISSION_QEMU_OK objects=3 offsets=match-host-reference`.
   **The C translation followed the same day, and is graded.**
   `aiueos_qwen35_model_translate` takes both profiles. Which one it is comes
   from bit 31 of kv workspace slot 124 — the bit the kv-scan object sets —
   and the four things that differ are CHECKED against that profile rather
   than assumed: record count (−106), artifact length (−107), metadata end
   (−108). The trunk is derived as `block_count − nextn` and refused unless
   it is the 64 the struct can hold (−109); the linear/full schedule is
   derived from the metadata's `full_attention_interval` rather than from a
   4 written in the C (−110 for a zero interval) and cross-checked against
   the count the object made from the role masks (−111), because a schedule
   that disagrees reads the mixer union as the wrong arm and nothing else
   would say so. PTQ1_0 (ggml type 143) counts in counter slot 31 — the slot
   `type-slot` in the object uses — while the tensor keeps 143 as its own
   type, so a dequantiser sees the file's number and the histogram stays the
   one the object checked. `aiueos_qwen35_model_parse` asks the OBJECT which
   workspace length this artifact wants instead of reading the header itself:
   128 first, and −3 — with a length the object admits — can only mean the
   other profile, so it retries with 144.
   New gate `run-task.cljk bonsai-c-translation`
   (`scripts/smoke-bonsai-runtime-translation.cljk` +
   `tests/bonsai_runtime_translation.c`; host `cc`, no artifact and no QEMU,
   because the translation records the model pointer and never dereferences
   it). Its INPUT is the two admission contracts' `:expect-plan-hex` — the
   144-byte and 28,160-byte workspaces computed independently in python and
   pinned to what the objects really produce by `bonsai-admission-contracts` —
   and its EXPECTATION is `contracts/bonsai2-qwen35-runtime-v1.edn`; no
   number is typed in either gate file. Two walks in opposite directions:
   851 shapes against the graph contract, and all 851 workspace records
   against the field each role id names, with the id → name half DECODED from
   the object's own `role-length` / `role-word` tables. The second walk is
   there because shape is not identity: `ffn_gate` and `ffn_up` are both
   5120 × 17408 PTQ1_0, and swapping them in `qwen35_slot` leaves the shape
   walk green — measured — while the identity walk names
   `blk.0.ffn_gate.weight`. Ten controls, every one seen red: the corrupted
   record the shape walk must NAME, and nine refusals asserted by reason
   literal (−103, −105, −106 twice, −107 … −111).
   The Qwen3.8 profile is unchanged where it can be checked byte for byte:
   `smoke-qwen35-runtime.sh` still reports 34 of 34 struct fields identical to
   the C reference parser, and the QEMU admission smoke booted this kernel —
   `QWEN-ADMIT reason=0 stage=0 admitted=1`,
   `AIUEOS_QWEN35_ADMISSION_QEMU_OK objects=3 translation=in-image
   offsets=match-host-reference` (2026-09-22, this tree, with the
   two-call kv-scan probe and the 144-byte workspace in the image).
   **Still Qwen3.8-only, and failing closed: `aiueos_qwen35_model_bind`.** It
   compares the data offset with Qwen3.8's 10,996,640 and binds the four MTP
   tensors, so a Bonsai model translates and then refuses to bind rather than
   fabricating a pointer. That is the floor after this one, and grading it
   needs the mapped 5.9 GB artifact.
   **The Bonsai profile's admission has run on a CPU (2026-09-23).**
   `kernel/main.c`'s fixture branch now takes two prefixes — Qwen3.8's
   synthesised 10,996,640 bytes and Bonsai's real 11,120,992 — and hands the
   objects the contract length that prefix belongs to
   (`AIUEOS_BONSAI2_DATA_OFFSET` / `AIUEOS_BONSAI2_ARTIFACT_BYTES` in
   `qwen35_runtime.h`). The prefix length chooses the LENGTH handed in; it
   does not choose the profile. The objects work that out from the header
   counts, and header-valid refuses a length that is not theirs. The
   printed tail is the last block's post-attention norm, indexed by the
   `block_count` the kv-scan object read: blk.64 under Qwen3.8 and blk.63
   under Bonsai, which has no MTP head. The line also prints `artifact=`.
   `smoke-qemu-qwen35-admission.cljk` with `AIUEOS_QWEN35_ADMISSION_BONSAI=1`
   (task `bonsai-qemu-admission`) builds the fixture through
   `tests/make-bonsai-boot-fixture.cljk` and reads its expected line from
   that generator's receipt, so no Bonsai number is typed in the gate. To
   reproduce, run `run-task.cljk bonsai-admission-fixture` and then the task
   (build + tcg boot, a few minutes). The result:
   `QWEN-ADMIT reason=0 stage=0 admitted=1`,
   `AIUEOS_QWEN35_ADMISSION_OK tensors=851 linear=48 full=16
   data-offset=11120992 embd=278138880 qkv=563159040 tail=5935507456
   artifact=5946648928`, `AIUEOS_BONSAI_ADMISSION_QEMU_OK`. Adding
   `AIUEOS_QWEN35_ADMISSION_MUTATE=1` retypes `token_embd.weight` from 143
   to 30, searching from metadata-end, and the gate requires
   `reason=-21 stage=3`. The Qwen3.8 line is unchanged except for the added
   `artifact=10934860704`. NOT measured: bind (the prefix is not the artifact,
   so `accessible != artifact` and bind is not reached), any tensor byte, and
   any token. PTQ1_0 / BF16 have no C twin, so `QWEN-PARITY` cannot score
   them.
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
   here). **Measured 2026-09-22**: `sha256sum` of the B70's copy
   (`/root/kgpu/models/Ternary-Bonsai-2-27B-PTQ1_0.gguf` on aiueos-6600hs)
   is `53107f53…`, equal to the value stream A's evidence records and to the
   graph contract here, so the two copies are the same bytes and the digest
   is no longer taken on trust. Split shape: 4,000,000,000 + 1,946,648,928. It is 4,988,211,776
   bytes smaller than the Qwen3.8 file that did fit the 16 GiB K16 beside
   the kernel; whether it fits is still a boot, not this subtraction.

### Stage B — the forward-pass cutover completes, and a second token exists

6. **The remaining stages of ADR-0220's cutover, in its order:**
   norm + activation → attention → recurrent-step (gated delta net) → rope.

   **Norm + activation: LIVE on the objects.** In `kernel/qwen35_infer.c`
   the forward pass's `rms_norm` (4 call sites), `rms_norm_heads_weighted`
   (q/k norm) and `l2_norm_heads` call `aiueos-qwen35-norm` modes 0/2/1;
   the FFN SwiGLU, the conv SiLU, the β sigmoid, the output-gate SiLU and
   the decay `exp(a·softplus(α+dt))` call `aiueos-qwen35-activation` modes
   4/0/1/0/2+3, each once over the whole vector. The C is kept as
   `*_c` under `AIUEOS_QWEN35_KOTOBA_PARITY == 2 || AIUEOS_QWEN35_C_REFERENCE_NORM`;
   the flag is set for the host smokes and parity profiles 1, 3 and 4 (which
   do not link the two objects). Still C, by stage: the gated RMS reduction
   of the linear-attention output (no norm mode matches it bit for bit — it
   has no finiteness refusals and no rescaled fallback) and rope's `exp`
   (rope stage).
   Reproduce: `AIUEOS_QWEN35_KOTOBA_PARITY=2 smoke-qemu-uefi.sh` →
   `QWEN-PARITY activation ok` / `norm ok` in `build/aiueos/evidence-all.log`
   (the smoke's exit does not grep them); the harness now calls the live
   wrappers (`activate`, `rms_norm`, `l2_norm_heads`,
   `rms_norm_heads_weighted`), so the argument order checked is the
   forward pass's. Seen red: input/output swapped in the live `rms_norm`
   → `QWEN-PARITY norm mismatch`, activation still `ok`, exit 1. Profile 1
   still `dequant/dot/matvec ok`; `bonsai-qemu-admission` (model-handoff
   image, now linking the two objects) → `AIUEOS_BONSAI_ADMISSION_QEMU_OK`;
   `smoke-qwen35-decode-math.sh` → `AIUEOS_QWEN35_DECODE_MATH_OK`.
   **Not measured**: a forward pass through these calls (QEMU has no model
   path; the parity vectors are 128 wide, the live widths are 5,120 /
   10,240 / 17,408 — inside both objects' admitted ceilings and fuel tiers,
   but never executed at that width); `smoke-qwen35-first-token-model.sh`
   (needs the real GGUF, not run).

   **Attention: LIVE on the object.** `full_attention`'s query/gate
   de-interleave, the fused softmax over the causal prefix of the KV cache
   (score, `exp`, denominator, weighted sum, gate sigmoid) and the
   position-zero reduction call `aiueos-qwen35-attention` modes 0/1/2. The
   object takes one arena and a plan of offsets; the live regions are the
   workspace (`scratch_a` / `scratch_c` / `scratch_b`, `beta_values` as the
   object's 128-byte scratch) and the decode context's key/value rows, so
   `attention_call` makes the arena the smallest span covering every region
   the plan names and rebases each address against it. Still C in
   `full_attention`: `rope_heads` (rope stage), the KV cache write, its FNV
   hash and `resolved_cached_key` (custody, not arithmetic — the object
   header says why). The C is kept as `attention_*_c` under
   `AIUEOS_QWEN35_KOTOBA_PARITY == 3 || AIUEOS_QWEN35_C_REFERENCE_ATTENTION`;
   the flag is set for the host smokes and parity profiles 1, 2 and 4 (which
   do not link the object). A refusal maps to `FULL QUERY` (−11), `SOFTMAX`
   (every other mode-1 refusal), `FULL KEY` (mode 0) or `FULL OUT` (mode 2).
   Reproduce: `AIUEOS_QWEN35_KOTOBA_PARITY=3 smoke-qemu-uefi.sh` →
   `QWEN-PARITY attention ok` in `build/aiueos/evidence-all.log` (appended
   across runs; truncate it first). The harness calls the live wrappers with
   addresses, so the plan layout and the rebase checked are the forward
   pass's. Seen red: key and value cache swapped in the live softmax plan →
   `QWEN-PARITY attention mismatch`, exit 1. Profiles 1/2/4 still
   `dequant/dot/matvec ok`, `activation/norm ok`, `recurrent ok`;
   `bonsai-qemu-admission` (model-handoff image, now linking the object) →
   `AIUEOS_BONSAI_ADMISSION_QEMU_OK`; `smoke-qwen35-decode-math.sh` →
   `AIUEOS_QWEN35_DECODE_MATH_OK`. The production node image links with it:
   `aiueos_low_end` 0x1ef000, 20,480 B under 0x1f4000 (`.text` 0xb4f82).
   **Not measured**: a forward pass through these calls (no model path in
   QEMU; the parity geometry is 8 heads / group 2 / position 5, the live one
   24 / 6 / 1..7), and a live arena span — in the model the workspace and
   the decode context are separate allocations, so the span the object is
   handed is their distance apart, which no run has exercised.

   **Recurrent-step: LIVE on the object.** `linear_attention`'s per-head
   gated DeltaNet step (decay, `remembered`, the correction, the rank-one
   update and the read-out, 48 heads × 48 layers) calls
   `aiueos-qwen35-recurrent-step`. The wrapper `recurrent_step` builds the
   96-byte plan (dimension 128, six region words, decay and β as binary32
   bit patterns) and, as for attention, makes the arena the smallest span
   covering the head state in `decode->recurrent` and the five workspace
   vectors, rebasing each address against it. Still C in
   `linear_attention`: the conv mix, the q/k L2 norm call sites, the decay
   transition's `+ dt` and `a *` products, the position-zero reduction that
   overwrites the output at position 0, and the gated RMS of the output.
   The C is kept as `recurrent_step_c` under
   `AIUEOS_QWEN35_KOTOBA_PARITY == 4 || AIUEOS_QWEN35_C_REFERENCE_RECURRENT`;
   the flag is set for the host smokes and parity profiles 1, 2 and 3 (which
   do not link the object). Any non-zero answer maps to `LINEAR RECURRENT`
   (the caller has already refused a non-finite decay/β as `LINEAR DECAY`,
   so the object's −6 is unreachable). Reproduce:
   `AIUEOS_QWEN35_KOTOBA_PARITY=4 smoke-qemu-uefi.sh` → `QWEN-PARITY
   recurrent ok` in `build/aiueos/kernel-serial.log`. The harness calls the
   live wrapper with addresses, so the plan layout and the rebase checked
   are the forward pass's. Seen red: key and query swapped in the live plan
   → `QWEN-PARITY recurrent mismatch`, exit 1. Profiles 1/2/3 still
   `dequant/dot/matvec ok`, `activation/norm ok`, `attention ok`;
   `bonsai-qemu-admission` → `AIUEOS_BONSAI_ADMISSION_QEMU_OK`;
   `smoke-qwen35-decode-math.sh` → `AIUEOS_QWEN35_DECODE_MATH_OK`. The
   production node image links with it: `aiueos_low_end` 0x1f1000, 12,288 B
   under 0x1f4000 (`.text` 0xb65a2). **Not measured**: a forward pass
   through the call; the live arena span (state and workspace are separate
   allocations — `decode->recurrent` is 150,994,944 B — so the span is their
   distance apart, which no run has handed the object); the per-token cost
   of 2,304 scalar calls where the C was an O3 loop.
   `smoke-qwen35-first-token-model.sh` needs the Qwen3.8 GGUF and was not run.

   **The production node image links again: `aiueos_low_end` is 0x1ec000,
   32 KiB under 0x1f4000.** Before, with `build-qwen38-murakumo-node-pxe.sh`'s
   flags (`AIUEOS_PHYSICAL_NETWORK_QUALIFICATION`, `…_DIRECT_HTTPS_…`,
   `AIUEOS_MURAKUMO_DEVICE_RESULT`, `AIUEOS_QWEN38_MODEL_HANDOFF`,
   `AIUEOS_PERSISTENT_BOOT`), `ld.lld` failed with `low kernel/user layout
   overlaps process-private aperture`: the low region ended at 0x1f8000 at
   231fe0b and at 0x1fa000 after this step (`.text` +9,776 B). `paging.c`'s
   `pci_pdpts` (16 KiB) and `pci_directories` (32 KiB) moved to `.high_bss`.
   Both are kernel-only page tables, reached through the kernel map that every
   process space copies, and zeroed explicitly in `aiueos_paging_initialize`.
   `.bss` went from 215,400 B to 158,056 B. `.high_bss` now ends at 0x486000,
   against its 0x600000 limit. Receipt:
   `os/aiueos/qualification/low-region-budget.edn`. To reproduce, run the
   build with `AIUEOS_ALLOW_DIRTY_QUALIFICATION_BUILD=1` if the tree is dirty
   and read the `aiueos_low_end`, `.bss` and `.high_bss` lines of
   `build/aiueos-qwen38-murakumo-node-pxe/core/kernel.map`. Seen red: on
   origin/main without the move, the same build fails with that `ld.lld`
   error, exit 1. Boots with the move: `smoke-qemu-uefi.sh` →
   `AIUEOS_UEFI_SMOKE_OK`. There, virtio modern and MSI-X are mapped through
   `aiueos_map_pci_mmio`, so the moved tables are walked. `bonsai-qemu-admission`
   → `AIUEOS_BONSAI_ADMISSION_QEMU_OK`. **Not measured**: the production node
   image booting on the physical K16. With the attention and recurrent-step
   objects linked the headroom is 12,288 B; the next object that grows
   `.text` past it needs the next move; the candidates left are the TLS and
   NIC scratch (tls13 8.8 KiB, rtl8125 8.3 KiB, main 6.8 KiB) or a third
   loader segment. The 64 KiB boot stack cannot move, because it is in use
   before `.high_bss` is zeroed.
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
count is measured; the mapping is not); the CPU-bound rate of a PTQ1 matvec on the K16's Ryzen; the
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
