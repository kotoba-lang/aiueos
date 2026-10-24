# The ChromeOS-grade scorecard (v1)

Fixed on 2026-10-23 for Linear JUN-90, JUN-89 and JUN-140. The data is
[`os/aiueos/qualification/chromeos-scorecard-v1.edn`](../os/aiueos/qualification/chromeos-scorecard-v1.edn);
`kbb --backend sci scripts/chromeos-scorecard.cljk` validates it and prints the score
(registered as the task `:chromeos-scorecard`).

## What 100% is

- There are 55 **required** rows. Their weights sum to 100. Ten more rows are optional, weigh 0 and are never counted.
- A row counts, in full, only when its status is `:pass` at its **required level** -- the real machine (`:device`), or a signed
  release artifact installed on it (`:release`) -- with evidence another person can read back (a receipt, a log, a screenshot).
  Nothing counts in part.
- A row nobody has verified is `:unachieved`, which is the same as failed ("unverified = unachieved").
- The score is the weight of the `:pass` rows over 100.
- Design text, code size, QEMU-only success, hosted runs and the old os-coverage marker share are **not** on this scale
  (JUN-89: "設計記述、QEMUだけの成功、コード量、旧 os-coverage のマーカー割合は製品完成度に換算しない").
- ChromeOS is the comparison, not the implementation: a row names what a person does and what they see, not how it is built.

Evidence levels, in order: `:design` < `:qemu` < `:device` < `:release` < `:user`. A QEMU run is `:qemu` unless it is a run in the reference VM with a kept receipt (below); only then does it count as `:device`.

## Where AIUEOS stands (re-scored 2026-10-23)

**SCORE 38%.** Twenty-five rows are `:pass`, each by a receipt of a run in the reference VM (below): B1, B2, B3, C1, C2, C3, C4, C5, C7, C8, C10, D1, D2, D4, D5, D6, D7, F4, G4, H4, I1, I4, I5, I7 and I8
(weight 38 of 100). The checker refuses a `:pass` without a receipt that reads back, with a red verdict, with an unconfirmed
reference, or at `:release` level on a VM receipt alone. Every other row stays `:unachieved`; real-hardware rows (README's P5 is
`:spec`) and the release rows (A3..A5) are not covered by the VM.

**Current-tree receipt.** `os/aiueos/qualification/receipts/vm-20261024/ui-current` is one boot of the tree after the size-optimized guest-browser build (ADR-0270) and the backup stage (ADR-0271), with 33 profiles green: the 28 of the first `ui` receipt (vm-20261023) plus clock, layout, notify, passkey and backup. The rows' own receipts stay the ones they cite; this one shows the passes above still hold on the current build.

Beside the score, and never added to it, the **QEMU-level progress** is 57.5% of the required weight (it counts the rows above too) (a row whose behaviour is gated
on QEMU counts 1, a row with some of it 1/2, none 0). By category: shell 9 of 15, input 7.5 of 12, boot 6.5 of 10, web 6 of 20,
security 4 of 9, storage and network 4 of 9, identity 1.5 of 6, power and accessibility 1.5 of 10, update and recovery 0 of 9.

**Against the earlier "about 10%".** That figure was a provisional estimate of product readiness (JUN-89) with no scale behind it;
it cannot be reproduced and is replaced. Under this scale the answer is 0%, because every required row needs the real machine or a
release, and none has been run. The ladder in ADR-0226 (28 of 30 floors gated) measures the QEMU level of one corner of this
scorecard -- windowing, input and the shell's feedback: rows C1-C4, C10 (partly), D1, D2, D4, D6 (partly), D7, G4, I5 and I8
(partly). The rest of the 55 rows are outside it.

## Passes whose limits the owner may reject (written 2026-10-24)

Each of these rows is `:pass` by a reference-VM receipt, and each row's `:note` states what it does not do. They are listed here in one place,
weakest first, so that a rejection is one edit of the row to `:unachieved`:

| Row | Weight | What the pass is | What it is not |
|---|---|---|---|
| I1 Suspend and resume | 2 | the sleep key blanks the screen and idles the machine in HLT; the wake key redraws; host screendumps, a surface fingerprint, 51 ms | not ACPI S3: RAM stays powered, no device powered down; the lid is a key |
| B1 Device-bound passkey | 3 | a P-256 key from the virtio-rng, kept in sector 1035; a WebAuthn-shaped ceremony; another device's key refused | the private key is in the clear on the volume (no secure element, G3 unmet); the other device is a test key |
| H4 Backup and restore | 1 | the settings and passkey records copied, wiped, restored byte for byte | two records, not files; the backup is on the same volume; no UI |
| C8 Settings | 1 | contrast, scale and a network flag changed by clicks, kept across a reboot | the network flag does not configure an adapter; controls are text lines |
| I4 Audio output | 1 | a virtio-sound tone at a set level, measured on the host's WAV | the volume is a software gain; one stream; a VM sound card writing a file |
| D5 Other layouts | 1 | US and JIS in English mode | two layouts, English mode only, not persisted |
| C7 Notifications | 1 | toasts, dismiss, a list; frames equal a model | raised by a test hook; keyboard-only |
| F4 Ethernet and DHCP | 1 | lease 10.0.2.15 / router 10.0.2.2 and three refusals | a virtio NIC on SLIRP, not the RTL8125 |

The eight weigh 11 points: rejecting all of them would take the score from 38% to 27%. The 27% is the stricter figure; the 38% is what the checker prints.

## What is not fixed by this file -- the owner's to confirm

JUN-90's acceptance asks the owner to confirm the **supported machine and the exclusions**. The file records the target as the
K16 (JUN-140 names "実機 K16") and leaves `:model`, `:firmware`, the ChromeOS device it is compared with and `:exclusions` as
`:owner-to-confirm`; `:confirmed-by-owner` is `false`. Until it is `true`, no row can be `:pass`. The rows, weights and levels
are fixed; the machine they run on is not. If the owner removes a row for the chosen machine (a camera, Bluetooth, a touchpad),
the denominator is re-derived and the change is dated here.

## How a row changes

A row becomes `:pass` in the commit that adds its evidence: the run's receipt and readback, kept with the run, named in
`:evidence`, with `:status :pass`. Changing a weight, a level or the rule above is a change to this document and to the data, dated,
with the reason -- not a quiet edit of a number. The checker fails on a row that is `:pass` without evidence or while the machine
is unconfirmed, on a dead evidence link (an ADR with no file, a gate that is not a profile, a path that does not exist), and on
weights that do not sum to 100.

## The required rows

| id | row | weight | needs | qemu |
|---|---|---|---|---|
| A1 | Firmware to desktop on the reference machine | 3 | device | partial |
| A2 | Desktop survives a display change | 2 | device | none |
| A3 | Install from USB | 3 | release | pass |
| A4 | A second install is refused | 1 | release | pass |
| A5 | First-run setup | 1 | release | pass |
| B1 | Sign in with a device-bound passkey | 3 | device | partial |
| B2 | Lock and unlock the screen | 2 | device | pass |
| B3 | Sign out | 1 | device | pass |
| C1 | Launch an app from the launcher and the dock | 2 | device | pass |
| C2 | Move, resize, focus and raise windows | 4 | device | pass |
| C3 | Switch windows from the keyboard | 1 | device | pass |
| C4 | Maximize and restore | 1 | device | pass |
| C5 | Minimize, and a list of open windows | 2 | device | pass |
| C7 | Notifications | 1 | device | none |
| C8 | Settings | 1 | device | none |
| C9 | Status area: network, power, clock | 1 | device | none |
| C10 | Interaction states and feedback | 2 | device | pass |
| D1 | Keyboard | 2 | device | pass |
| D2 | Pointer and wheel | 1 | device | pass |
| D3 | Touchpad gestures | 2 | device | none |
| D4 | Japanese input | 3 | device | pass |
| D5 | Other layouts and input methods | 1 | device | none |
| D6 | Text editing | 1 | device | pass |
| D7 | Copy and paste with permission | 1 | device | pass |
| D8 | Drag and drop | 1 | device | none |
| E1 | Render a web page with the shared Browser/CSSOM | 7 | device | partial |
| E2 | HTTPS pages | 3 | device | partial |
| E3 | JavaScript execution | 3 | device | none |
| E4 | Tabs, history and bookmarks | 1 | device | none |
| E5 | Download, upload and file picker | 2 | device | partial |
| E6 | Audio and video playback | 1 | device | none |
| E7 | Run an Itonami dapp from the launcher | 3 | device | none |
| F1 | Internal storage (NVMe) | 2 | device | partial |
| F2 | A durable file system | 1 | device | partial |
| F3 | Wi-Fi | 3 | device | partial |
| F4 | Ethernet and DHCP | 1 | device | partial |
| F5 | USB storage | 1 | device | partial |
| F6 | Bluetooth | 1 | device | none |
| G1 | Verified boot and signed images | 3 | release | partial |
| G2 | Per-app isolation | 2 | device | partial |
| G3 | Data encrypted at rest | 2 | device | none |
| G4 | Permission broker | 1 | device | pass |
| G5 | Key lifecycle and revocation | 1 | release | partial |
| H1 | Signed update download and apply | 3 | release | none |
| H2 | Rollback after a bad update | 3 | release | none |
| H3 | Recovery mode and recovery image | 2 | release | none |
| H4 | Backup and restore of user data | 1 | release | none |
| I1 | Suspend and resume | 2 | device | none |
| I2 | Battery and power management | 1 | device | none |
| I3 | Display scaling and external displays | 2 | device | none |
| I4 | Audio output | 1 | device | none |
| I5 | High-contrast mode | 1 | device | pass |
| I6 | Screen reader | 1 | device | none |
| I7 | Text and UI scaling for low vision | 1 | device | pass |
| I8 | Everything reachable by keyboard alone | 1 | device | pass |

(Each row's procedure, expected result, required evidence, current note and links are in the EDN.)

## Reference machine (owner, 2026-10-23)

The owner's answer: the reference is a VM on the Mac the agent works on, and the ChromeOS it is compared with is the
latest ChromeOS in a VM on the same Mac. For the details the owner said to take the recommended choice, and this file does:

- **The VM** is the QEMU + UEFI stack the gates already run on this Mac (MacBookPro18,4, Apple M1 Max); each receipt records the
  host, the QEMU version, the commit and a hash of the serial log and of every screenshot.
- **A run in it with a kept receipt counts as `:device`-level evidence** (`scripts/vm-receipt.cljk` writes the receipt;
  `os/aiueos/qualification/receipts/vm-20261023/` holds the first two).
- **No row is excluded.** A row the VM cannot exercise (Wi-Fi, Bluetooth, battery, suspend, an external display, a touchpad) stays
  `:unachieved`; `:release` rows still need the release artifact.
- **Not done:** the expected behaviours were taken from ChromeOS and were not re-measured in a ChromeOS VM on this Mac. The owner can
  overturn any of the above; the checker follows `reference-machine` in the scorecard file.
