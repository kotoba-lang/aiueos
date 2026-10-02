# ADR-0240 — the guided screens default to what the machine says

- Status: accepted
- Date: 2026-09-26
- Owner request, after running the guided stick in QEMU (ADR-0239): 「この辺りは
  自動的に入る様にしてほしい」 — the disk pick, hostname, model and SSH key.
- Extends ADR-0208 (guided installer) and ADR-0239 (the stick that asks installs).

## Decision

Four screens gain a default, so Enter answers them; the confirm screen (retype
the hostname) and the typed `ERASE <device> FOR AIUEOS` do not, because they
are the erase confirmation.

| prompt | default | source |
|---|---|---|
| Disk to erase | the one candidate measured **empty** | `blkid -p` per disk |
| Hostname | slug of the machine model, else `aiueos` | DMI |
| Machine model | `<sys_vendor> <product_name>` | `/sys/class/dmi/id` |
| OpenSSH public key | the key the stick was built with | `--default-ssh-key` |

**Empty is measured, never assumed.** The first version read lsblk's
PTTYPE/FSTYPE. The live installer has no udev, so lsblk reported both empty
for every disk: in the QEMU trial a disk with a DOS partition table was listed
`[empty]` beside the real empty one. It failed safe only because two "empty"
disks produce no default. The probe now asks `blkid -p -o export` for each
whole disk; exit 0 or 2 sets `:signatures-measured?`, anything else leaves the
disk unmeasured, and `blank-disk?` never calls an unmeasured disk empty. The
listing says `[empty]`, `[has data: <type>]` or `[unmeasured]`. Two measured
empty disks still get no default: a default between them would be a guess.

`--default-ssh-key` is refused on an intent stick (the intent already names its
key) and accepts only an `ssh-ed25519` line. It is a public key; the person at
the console can still type another.

## Evidence (2026-09-26)

- `test-guided-install` 64/64 (8 new: empty/used/filesystem/unmeasured, two
  empties, single candidate, hostname slug and fallback), `test-install-bundle`
  36/36.
- QEMU trial (TCG, 09-15 release image and live UKI): USB + an 8 GiB disk with a
  DOS table and data + an empty 16 GiB disk. Listed `1) 8.6 GB [has data: dos]`,
  `2) 17.2 GB [empty]`, `Disk to erase (number) [2]`; every screen answered
  with Enter except the hostname retype and the ERASE phrase; `AIUEOS_INSTALL_OK`
  on the 16 GiB disk; the 8 GiB disk's sha256 identical before and after.

## Not claimed

The K16's DMI strings are not measured; if its firmware leaves a placeholder
the model default is `unspecified` and the hostname default `aiueos`.

## Reproducible smoke — landed as a script, NOT yet green (2026-09-26)

`os/aiueos/scripts/smoke-qemu-guided-install.cljk` automates the trial above as
three boots: A types the ERASE phrase wrong (must refuse with
`--destructive-phrase must exactly equal`, nothing written), B takes every
default (must install on the empty disk, the used disk byte-identical), C boots
the installed disk alone (evidence floor 25 markers). `--interactive` hands the
console to a person for the QR link.

Owner bound: the whole verification under 1 GB. The live guest is 512M with
`-accel tcg,tb-size=64` (QEMU's TCG buffer defaults up to 1 GiB), and a
governor sums the RSS of the gate's process tree every second, records each
stage's peak, and kills the tree by name over `AIUEOS_GUIDED_GATE_BUDGET_MIB`
(default 1000).

Measured: with the budget at 50 MiB the gate stops at
`memory-budget-exceeded stage=usb-build rss=485MiB budget=50MiB` and leaves no
child process — the governor refuses by its own reason. **Not measured:** a full
run under the 1000 MiB budget, whether the live installer boots in 512M, and
each stage's peak. The first attempt (2048M guest) stalled after
`AIUEOS_LIVE_INIT start` under host memory pressure and was reaped; the second
(1024M) was stopped by hand when the owner set the 1 GB bound.

Resume: `AIUEOS_OUT=<aiueos>/build/aiueos kbb --backend sci
os/aiueos/scripts/smoke-qemu-guided-install.cljk` from the west checkout (the
USB build resolves `../text` and `../security`), and read the
`AIUEOS_GUIDED_GATE memory` line. If the live boot does not reach the screens at
512M, raise `AIUEOS_GUIDED_GATE_MEM` only as far as the budget allows.
