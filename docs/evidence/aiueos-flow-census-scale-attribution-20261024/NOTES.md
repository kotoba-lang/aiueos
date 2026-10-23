# FLOW census-miss attribution: the SETTINGS stage's persisted UI scale, not the object (JUN-31, 2026-10-24)

Question this cell answers: the guest-browser full profile reports
`AIUEOS_GUEST_BROWSER_FLOW leftover=census-miss ops=182 text-px=9564 hash=b2fad8a7`
against the gate pin `text-px=3593 hash=a19f95a8` (calibrated ADR-0234,
`90f8b03`, 2026-09-25). Is that miss caused by the selfhost-built kotoba
kernel object route, or by stage history inside the profile?

## Measured

* **A — selfhost object, full profile (the miss run).** Object
  `kernel-probe-abi.o` sha256 `0e04108e…` (841 B, `kotoba_aiueos_probe`),
  image `KERNEL.ELF` sha256 `ab5f25d0…` (823,944 B), `AIUEOS_GUEST_BROWSER=1`,
  real virtio-keyboard + virtio-tablet via the QMP injector. Serial log stage
  order: `… SIGN_OUT -> SCALE -> RING -> EDIT -> CLOCK -> SETTINGS -> FLOW`.
  The SCALE stage walks 100→125→150→100 (`SCALE_OK events=16`, back at
  pct=100). The SETTINGS stage then sets **pct=125** (`n=4 key=53 … pct=125
  text-px=1757 hash=244cc84c written=2 stored=1`; the SETTINGS frame census
  goes 668→1757 px = **2.63×**) and `SETTINGS_OK events=6` is immediately
  followed by FLOW: `ops=182 text-px=9564 hash=b2fad8a7` — **2.66×** the pin
  (`3593/a19f95a8`). Excerpt:
  `selfhost-full-profile-scale-settings-flow.log`.
* **B — same object route, synthetic-input profile (no scale stage; scale
  stays pct=100):** `AIUEOS_GUEST_BROWSER_FLOW_OK windows=4 ops=182
  text-px=3593 hash=a19f95a8` — the pin matches **exactly**. Excerpt:
  `synthetic-profile-flow-ok.log`.
* **C — op stream identity.** The `FLOW_OPS n=182 words=…` stream (8,736 hex
  chars) has sha1 `42fd9ed630edc8c031119c0106a4a0c9ba510dd5` and is
  **byte-identical between A and B**. The drawn content is the same; only the
  rendered magnification differs.
* **D — object containment, byte-proven.** Committed control object
  `os/aiueos/kotoba/kernel-probe.o` sha256 `3f7409bf…` (864 B). Each image
  contains its own object's `.text` at file offset **325888**, byte-identical
  except `.text+3..+5` (bytes `41 57 07` = the link-time relocation
  `lea r9,[rip+0x75741]` displacement). The *other* object's `.text` is absent
  from each image (reloc-masked clean-window search: 51 B selfhost window and
  28 B committed window each hit only their own image). So the two runs'
  images differ by the object route and nothing else.
* **E — control cell (committed object, same full profile).** This tick
  rebuilt the committed-object image — `KERNEL.ELF` sha256
  `3e8bdb0e94690638b0e219611338ab37872c0c864f78a3252e4d108ca40e183a`,
  **byte-identical** to the previous control build — and booted it detached at
  05:13 JST (`build/aiueos-d`, run log `control-run4.log`). Its FLOW census
  is **in flight at commit time** and lands in a follow-up commit; one earlier
  attempt (`control-run2-attempt1-flake.log`) stalled at
  `AIUEOS_KOTOBA_ELF_PROCESS_OK` — the known #108 HOST-FLAKE-SIGNATURE — and
  was retried, not counted as evidence.

## Attribution

The census-miss tracks the **SETTINGS stage's persisted UI scale** (pct=125 is
active at the FLOW measurement; the frame census is magnified 2.63–2.66× at
pct=125), **not** the object route: the op stream is byte-identical across the
miss and pass runs, the same object route passes the pin exactly at pct=100,
and the object linked into each image is byte-proven.

Root-cause timeline: the FLOW pin was calibrated at pct=100 in ADR-0234
(`90f8b03`, 2026-09-25). The scale stage landed later — ADR-0261
("the UI scale is chosen by kotoba and magnified by C", `62490d0`,
2026-10-23 16:32, PR #470) — and runs before FLOW in the same profile. The
gate is therefore scale-unaware by construction: **the pin predates the stage
that changes the scale before the measurement.**

## Gate proposal (FLOW stage / census pin)

* **Option A (recommended):** measure the FLOW census at a fixed pct — restore
  pct=100 before the FLOW stage — and keep the single pin `3593/a19f95a8`.
  One canonical pin keeps the census comparable across profile changes.
* **Option B:** scale-indexed pins (`pct=100: 3593/a19f95a8`;
  `pct=125: 9564/b2fad8a7`, as measured here) and compare against the pin for
  the active pct.

## Boundary (say this, do not round it up)

* `kernel/*.c` is still compiled by zig cc; the C-free claim covers the
  compiler lineage and the kotoba object route only.
* The #108 process-phase stall (`AIUEOS_KOTOBA_ELF_PROCESS_OK`, one
  HOST-FLAKE-SIGNATURE retry in this evidence) is a separate known flake and
  is not used as evidence here.
* The control cell's FLOW census (E) is not yet measured at commit time; the
  attribution above is established by A–D and E closes object-independence.
