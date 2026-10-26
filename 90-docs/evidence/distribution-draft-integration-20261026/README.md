# Distribution / AWAI-90 draft integration — 2026-10-26

The [matrix](matrix.json) binds source commit
`5729ce55e9102a4c3122c075c889a2d79abdedbe`, which merges AIUEOS main
`45e3ae8d32f92c04f7a356b051bf9a2346b0d255` (#520), to four complete native
builds. AIUEOS and AWAI each built both `desktop` and `local-draft` roots.
Every build compiled all 31 locked modules. Within each profile, kernel,
EFI, font and GPT/FAT16 media are byte-identical across distributions;
descriptors are staged and digest-bound separately. The two profile roots
produce different kernels as expected.

The pinned ARM64 native compiler ran in the immutable local Linux image in
`matrix.json`, with networking disabled, through Amu's shared build queue.
No JVM, Node, Graal, C compiler or C loader supplied an artifact. Python only
staged/invoked/hashed outputs and observed guest state. The CLI and direct
controller both refused unknown distribution/profile values before output.

[Normal desktop QEMU](normal-desktop-qemu.json) has 12 named observations,
including F5 persistence disabled. [Local draft QEMU](native-draft-restart.json)
has 11 named observations: actual native ATA commit and cold-process restore,
SIGKILL after an uncommitted edit preserving the last commit, alternate-slot
commit, unpublished higher generation ignored, and seven refusal controls.
Every refusal that has a disk preserves its bytes. The observers independently
validate EFI/media and retain the immutable boot-media digest. Their output
kernel/EFI/media hashes match the matrix exactly. Both run the AWAI candidates;
byte equality binds the equivalent AIUEOS candidates, without claiming a second
independent guest run for them.

Reproduce using the pinned compiler / owner roots in
[the progress guide](../../native-workspace-restore-progress.md), passing both
`--distribution aiueos|awai` and `--profile desktop|local-draft` to the builder.
Each output must be a fresh scratch directory. The first local observer launch
was refused by the macOS sandbox at QMP socket bind, before guest boot. It is
retained in scratch as `qemu-desktop`; the permitted rerun above is the measured
result. No source change was needed for that host restriction.

These are QEMU TCG component observations. They do not qualify a D2 workspace
image, shared Browser/CSSOM parity, registered Itonami user artifact, signed
Cell head or release admission, installer publication, distributed durability,
or physical hardware. Historical 2026-10-25 receipts remain unchanged.
AWAI-90 and draft PR #508 remain open for those owner integrations.
