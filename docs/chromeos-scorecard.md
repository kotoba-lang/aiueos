# The ChromeOS-grade scorecard (v1)

Fixed on 2026-10-23 for Linear JUN-90, JUN-89 and JUN-140. Reconciled with
the 2026-10-25 signed-release receipt. The data is
[`os/aiueos/qualification/chromeos-scorecard-v1.edn`](../os/aiueos/qualification/chromeos-scorecard-v1.edn);
The retired hosted checker produced the historical scores used by this ledger. Its KBB/JVM
route must not be used for current native qualification. Current source-bound
native progress is recorded in
[`90-docs/shared-browser-native-progress.md`](../90-docs/shared-browser-native-progress.md).
The historical percentages are not a score for the current native OS or
evidence that shared Browser/Itonami/restoration have been integrated.

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

## Where AIUEOS stands (ledger reconciled 2026-10-25)

**Recorded reference-VM score: 48%.** Twenty-nine rows are `:pass`: A1, A3, B1, B2, B3, C1, C2, C3, C4, C5, C7, C8, C10, D1, D2, D4, D5, D6, D7, F1, F4, G2, G4, H4, I1, I4, I5, I7 and I8
(weight 48 of 100). A3 adds three points on the strength of the signed-release receipt and the earlier installation readback of the same disk image bytes. A4 and A5 remain `:unachieved`. The old hosted checker refuses a `:pass` without a readable green receipt, with an unconfirmed reference, or at `:release` level on a VM receipt alone. The checker has not been ported to the native Amu/Kotoba qualification route, so 48% is the recorded score in the EDN, not a freshly run native verdict. P5 real-hardware qualification remains `:spec`.

**Historical UI receipt.** `os/aiueos/qualification/receipts/vm-20261024/ui-current` is one boot after the size-optimized guest-browser build (ADR-0270) and the backup stage (ADR-0271), with 33 profiles green: the 28 of the first `ui` receipt (vm-20261023) plus clock, layout, notify, passkey and backup. The rows' own receipts stay the ones they cite. This receipt predates the current tree and does not qualify later native Browser/Itonami integration.

Beside the score, and never added to it, the **QEMU-level progress** is 61% of the required weight (it counts the rows above too) (a row whose behaviour is gated
on QEMU counts 1, a row with some of it 1/2, none 0). By category: shell 14.5 of 15, input 9 of 12, boot 8 of 10, web 6 of 20,
security 5 of 9, storage and network 5.5 of 9, identity 6 of 6, power and accessibility 6 of 10, update and recovery 1 of 9.

**Against the earlier "about 10%".** That figure was a provisional estimate of product readiness (JUN-89) with no scale behind it;
it cannot be reproduced and is replaced. At the time of that estimate, under this scale the answer was 0%, because every required row needs the real machine or a
release, and none had been run. The ladder in ADR-0226 (28 of 30 floors gated) measures the QEMU level of one corner of this
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
| F1 Internal storage | 2 | AIUEOS_VIRTIO_BLK_OK reads the internal disk; journal append-readback, recovery and committed-redo replay across a reboot | the durable payloads are journal/object records, not arbitrary files; the QEMU device is virtio-blk on the data image, not a physical NVMe; 'power cut' is the journal's crash-recovery path, not a host power loss |
| G2 Per-app isolation | 2 | a kernel self-test creates two processes with distinct CR3 and private pages; a cross-access faults and the test continues | kernel-created processes, not two user-launched apps; 'the other app is unaffected' is the test's continuation, not a separate user-visible check |
| A1 Firmware to desktop | 3 | a production release image reaches the guest desktop with a QMP-injected key event; the desktop marker appears within the 60 s timeout (boot log 62 s) and no evidence stage stops | the display is read back from serial markers, not a screenshot; QEMU is stopped at 60 s while the desktop persists; not a physical machine |

The eleven weigh 18 points: rejecting all of them would take the recorded score from 48% to 30%. This is a sensitivity check, not a second qualification score.

## ChromeOS comparison boundary

The required rows compare user-visible outcomes. The Google/Chromium sources behind the boot, update, recovery and accessibility rows are [verified boot](https://www.chromium.org/chromium-os/chromiumos-design-docs/security-overview/), [automatic updates with a fallback partition](https://www.chromium.org/chromium-os/chromiumos-design-docs/filesystem-autoupdate/), [user recovery](https://support.google.com/chromebook/answer/1080595?hl=en), and [accessibility features](https://support.google.com/chromebook/answer/177893?hl=en). These sources define comparison criteria; they are not AIUEOS test evidence. ChromeOS behaviour has not been measured in the owner's same-host comparison VM.

| Axis | Current AIUEOS evidence | Remaining product gate |
|---|---|---|
| Kotoba/Amu selfhost | Native compiler and diagnostic media receipts are recorded separately in `90-docs/native-selfhost-integration.md`. | Whole source and runtime closure, including shared Browser/CSSOM and Itonami, needs native self-rebuild and runtime receipts. |
| Boot and install | QEMU/OVMF desktop and the signed-image identity for A3 are recorded. | A4/A5 release runs and physical device boot/install remain open. |
| Security and update | Selected isolation and permission rows have VM receipts. | Hardware-rooted verified boot, encrypted user data, signed update, automatic rollback and recovery remain open. |
| Daily use | Some shell, input and accessibility rows have VM receipts. | Shared Browser, real Itonami workflow, screen reader, hardware/power and long-running use remain open. |

The 48% figure applies only to the owner-selected **QEMU/OVMF VM reference** and this scorecard's accepted receipts. It is not a ChromeOS feature-completeness claim, a native selfhost percentage, or a physical-hardware readiness figure.

## Reference scope and open comparison

The owner selected the QEMU/OVMF VM on the Mac as this scorecard's reference on 2026-10-23; the current EDN records `:confirmed-by-owner true` and no exclusions. The K16 remains a separate physical-machine qualification target. If the owner changes the supported machine or excludes a row, rederive the denominator and date the change here.

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
