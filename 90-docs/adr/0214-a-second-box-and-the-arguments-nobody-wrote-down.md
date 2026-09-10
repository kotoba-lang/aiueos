# ADR-0214 — a second box, and the arguments nobody wrote down

- Status: accepted
- Date: 2026-09-10
- Follows ADR-0208/0210 (the guided stick) and the node-agent tranche. Reuses
  ADR-0207's reason for not pointing a second identity at an admitted name.

## What was asked, and what turned out to be needed

"Rebuild the K16 installer for an AMD Ryzen 5 6600HS box."

The installer needed no rebuilding. `make-node-autoinstall-iso.cljs` is Ubuntu
Server plus an autoinstall plus the agent bundle, and **nothing in it is CPU- or
NIC-specific** — that was the point of standing on a distribution rather than on
a hand-built initramfs. Measured before writing anything: the only per-box inputs
are six arguments (`--hostname`, `--target-serial`, `--ssh-key`, `--endpoint`,
`--console-password*`, `--tailscale-authkey-file`) plus the ISO's codename.

And the CPU change justifies none of them. **6600HS is Rembrandt; the K16's
7735HS is Rembrandt-R — the same silicon rebadged.** Same family 19h, same Zen 3+
core, same RDNA2 gfx1035 display block. The useful form of that fact is negative
and is now recorded where someone will hit it: this board needs no new kernel
argument, no new firmware package and no new driver on account of its CPU. A boot
failure here that the K16 did not have is the BOARD — its NIC, its BIOS, its disk
controller mode.

So what was missing is what the node-agent commit said was missing in its own
first line: *"the box exists and the means to make another one did not."* The
means are the ARGUMENTS, and they lived in one session's shell history.

## The decision

`contracts/node-machines-v1.edn` holds the boxes, and every box-specific field
carries its own provenance: `:measured` (read off the machine, by a named command,
on a named date), `:owner-stated` (the owner said so; nobody has read it off the
box), `:unverified` (nobody has looked — **not absent, not false**).

Writing the 6600HS entry is what made the file worth having. Every field in it is
unverified, because nobody has met the box — and the one that matters is the disk
serial. A stick built from a plausible-looking serial installs unattended onto
whatever disk matches, which is the single failure the whole install chain exists
to prevent (install-v1 already refuses "largest disk" and "first NVMe" for this
reason). So:

> `make-node-installer.cljs --machine <id>` builds the stick that **ASKS** unless
> that box's disk serial is measured. `--unattended` demands the measured field
> and refuses without it, naming the command that would measure it.

That is the whole product difference, and it is now decided by a recorded
measurement rather than by how confidently someone typed.

The driver also prints every fact with its provenance before building, so
"the stick built" cannot be read as "these facts about the box are right". For
the 6600HS today it prints `0 measured`.

## The box measures itself

A new box has no measured fields and the cheapest moment to read its NIC, disk
serial and board off it is the one boot that is happening anyway. The autoinstall
now writes `/var/log/aiueos-node-hwprobe.txt` (and prints it on the installer
console); `record-node-machine-probe.cljs` turns it into `:measured` fields.

From **sysfs and util-linux only**, deliberately: `lspci` and `dmidecode` live in
packages the live installer is not required to carry, and a probe that reports
nothing because a tool was absent looks exactly like a box with no NICs. `lspci`
is appended if it happens to exist, as a bonus and never as the source.

Read-only, additive, and **not on the critical path** — no `set -eu`, every
command guarded, the block ends in `exit 0`. A measurement that can fail an
install costs more than it measures. `--no-hw-record` turns it off, because a
default-on measurement nobody can decline is a default nobody chose.

Two things in that block were wrong when first written and are recorded because
both were found by running it rather than by reading it:

- a single `pci=%s:%s` printf emits a bare `:` for a NIC with no PCI parent, and
  `:` reads as a PCI id to anything that only checks for non-blank. Two
  independently blank fields now.
- the internal-disk rule keyed on `nvme*`. A box with a SATA SSD has no `nvme*`
  at all, so its serial could **never** be measured. It now asks lsblk
  (`RM`/`HOTPLUG`) — and excludes the virtual devices that pass that test, `zram`
  being the one that did.

## Evidence

`scripts/test-node-installer.cljs`, 55 cases, offline, registered as
`:node-installer-test` and run through `scripts/run-task.cljs`. It ingests a
probe into a registry COPY and then requires the driver to build the unattended
stick with the serial that probe carried — "the ingest printed six fields" is not
"an unattended stick can now be built". The probe block is extracted from the
builder's own output and executed.

Four mutations, and the cases each turned red, measured 2026-09-10:

| break | what turned red |
|---|---|
| the `--unattended` serial guard deleted | exactly the 2 cases that name it |
| `measured?` loses its provenance half (a value is enough) | exactly `driver-reports-zero-measured-facts-for-a-new-box` |
| the removable test dropped from the internal-disk rule | 9, including both stick-exclusion cases |
| the whole-fact registry rewrite restored | exactly the 4 commentary cases |

The third and fourth are the ones worth keeping, because both were **green
before**:

- The first version of "the stick is not a target" compared the recorded serial
  to the stick's. That passes whenever the field is nil, so dropping the
  removable test left it green. The exclusion is now measured on a probe whose
  only disk IS removable — the one shape where admitting it produces a confident
  wrong answer rather than an ambiguous one.
- The first rewriter replaced each fact map WHOLE. It measured correctly and
  silently deleted the commentary, the `:measure-by` that says how to read the
  field off a box again, and the `:same-silicon-as` above. The suite was green:
  the only thing it checked was a string in the file's HEADER, which survived.
  Only the fact's head is rewritten now, and the tail is copied byte for byte.

One more thing the read-back guard caught rather than the author: the 6600HS
entry already carried an `:on` from when the owner stated its CPU, so appending
a second one produced `Map literal contains duplicate key: :on`. Nothing was
written. The head now consumes `:on`/`:by` if present, and the read-back stays as
the net — it re-reads the rewrite, compares the machine and fact key sets, and
checks each value landed, before anything reaches the disk.

## Registered, which it was not

`make-node-agent-bundle.cljs` and `make-node-autoinstall-iso.cljs` were not in
`scripts/tasks.edn`. The registry is how anything here is found and run, so the
tooling that produced the working node could not be found by the task that is
supposed to list it. Four entries now: `:node-agent-bundle`,
`:node-installer-build`, `:node-machine-probe-record`, `:node-installer-test`.

## What this does not claim

- **No ISO was built.** Not once, for this box or any other. It needs an Ubuntu
  Server ISO, the agent bundle's pinned linux-x64 node binary, and an SSH key
  that is the owner's choice. What is measured is the generated autoinstall (valid
  YAML, every late-command valid `sh`), the probe block executed on Linux, and the
  driver's plan. The repack itself is exercised only through its refusal path —
  handed a file that is not an ISO, it refuses on the boot record, which is the
  guard the node-agent commit added after a 378 KB "ISO" reported success.
- **Nothing about the 6600HS board is measured.** Its registry entry says so in
  six places. Its CPU is `:owner-stated`.
- **The K16 entry is not a receipt for the box that was working on 2026-09-10.**
  Its hardware facts are measured and were paid for while the bespoke installer
  was failing on it. Whether that board is the one the first Ubuntu node ran on
  is not recorded in this repository, and this entry does not decide it.
- **The K16 entry's hostname is `aiueos-k16-ubuntu`, not `gmktec-k16-lan2`.**
  That name belongs to the native PXE relay's enrolment, and pointing a second
  identity at an admitted name is what ADR-0207 measured the plane refusing (409,
  a name cannot be re-pointed to a different DID).
- **I6 and I7 are unchanged.** No physical probe receipt, no physical install.
