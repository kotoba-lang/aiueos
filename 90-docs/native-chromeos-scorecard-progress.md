# ChromeOS scorecard native probe

JUN-409 moves the 65-row comparison ledger away from the historical
`scripts/chromeos-scorecard.cljk` KBB/Node checker. The current Kotoba source
is `os/aiueos/kotoba/aiueos/chromeos_scorecard.kotoba`. A signed native Amu
Mac compiler builds it, and a signed native Kotoba packager emits the test
Mach-O. The host Python script stages immutable inputs, checks source/image
identities and runs the result; scorecard semantics execute in Kotoba.

The reader takes the exact `chromeos-scorecard-v1.edn` bytes through an FS2
file capability. It skips comments and collapses EDN whitespace outside
quoted strings before the bounded native document reader parses the map.
No hosted EDN reader, JVM, NBB, Node, Graal, C compiler or Linux guest runs
in this probe. All file reads use a private FD3 root containing the scorecard
and a copy of the qualification receipt tree.

Run the probe with the pinned Amu Mac images and a **new** output directory
under `github/workspaces`:

```sh
python3 os/aiueos/scripts/probe-chromeos-scorecard-native.py \
  --compiler "$AMU_MAC_COMPILER" \
  --compiler-sha256 012bfb77a364b3b68cb5e2c406ae49535784fa2b851dfdbbe1bd10efffcb950a \
  --packager "$AMU_MAC_PACKAGER" \
  --packager-sha256 2ee308409db572c08733a4b0a28fc1908c8b15c44b045cf78e671ff798439ad0 \
  --output "$NEW_WORKSPACE_OUTPUT"
```

The current Mac images are local diagnostic artifacts from the Amu
selfhost chain tracked in [Amu #1588](https://github.com/kotoba-lang/amu/pull/1588).
The output receipt `native-scorecard-core.json` binds their digests, the
Kotoba source, the scorecard and the compiled native executable. It records
whether the source and scorecard bytes match the named Git commit.

The native probe checks the fixed reference schema, 55 required rows,
required weight 100, zero-weight optional rows, unique IDs, known categories
and levels, owner confirmation, valid status/progress, receipt presence, and
the receipt's 40-hex commit and nonempty green verdict map. A passing
`:release` or `:user` row must cite the fixed v1 signed-release receipt; that
receipt must have 64-hex signature and artifact digest fields plus the three
named green verdicts. The probe reads the referenced JSON and binary signature
as FS2 bytes and computes their SHA-256 in Kotoba; both must match the receipt
fields. This uses the pure SHA-256 source copied from Amu
`seed/03-source-sha256.kotoba` at commit `392118c8` (source SHA-256
`40be511b6879d1c4c6a4b443b796ec1ef5cbe561d5c69c6ae52b8e62a96393d1`).
Known empty, `abc`, and multiblock vectors run in the native executable.
The native verifier then checks RSA-2048 exponent 65537 and the complete
PKCS#1 v1.5 SHA-256 encoding against the JSON bytes. It adapts the bounded
calculation in `os/aiueos/kotoba/rsa2048.kotoba` to the Mac native vector ABI.
The embedded modulus matches the checked-in qualification public key; no
private key is read by this verifier. This is the v1 **qualification** key,
not a production release key. The native probe also hashes the saved primary
serial log for every referenced passing VM receipt and the secondary log where
the receipt declares one. The `settings` secondary is
`kernel-serial.log.boot1`; the install-refusal receipt uses
`boot4-serial.log` and `install-serial.log`. Missing or modified logs fail the
ledger admission. This verifies the declared log bytes, not the truth of every
feature claim or screenshot digest in a receipt.
The probe prints the recorded score of 48 and category weights, passes and
gaps. Eighteen negative controls cover malformed schema, missing or red
receipts, missing signed-release reference, malformed digest fields, missing
named verdicts, one-byte artifact changes, a modified signature with an
updated matching digest, a different public modulus, and missing or changed
primary and secondary serial logs. Each must return a nonzero native result;
expected output is checked exactly.

This is a **core schema and qualification-release signature probe**. Other
receipt marker text and non-serial artifact digests are not yet checked against
the underlying log/image, and the production release signature is not qualified.
Receipt commit age and relevant-source freshness remain unqualified. The 48
points describe the existing reference VM ledger; they do not qualify physical hardware or
a release. The runner and receipt name keep `EVIDENCE_PENDING` until those
gates pass. The legacy checker stays historical and must not be used for
current native qualification.

## Evidence audit of the recorded VM score (2026-10-26)

A read-only host diagnostic inspected the canonical ledger's 31 receipt
references (14 distinct files). The 17 checked digest fields for adjacent
serial logs, WAV files, the qualification signature, and the signed JSON
receipt matched their files. The qualification release signature also passed
the existing host verifier with the checked-in qualification public key.
These observations are independent of the native scorecard probe and do not
make the release-production signing key or release gate qualified.

Six VM receipts name feature commits which are known locally but are not
ancestors of this branch's source commit: `ui`, `audio`, `backup`, `layout`,
`notify`, and `passkey`. Twenty-one `:pass` rows, weighing 33 of the ledger's
48 recorded points, reference at least one of them. Later squash merges may
contain those changes, so ancestry failure alone is not a failed feature.
The files changed by each original feature commit have since changed again;
the old receipts do not prove the present source at this branch's commit.
Rerun the affected gates against the selected release source before promoting
their points from recorded VM evidence to current-release evidence.

The six receipts' screenshot maps contain 117 distinct PPM names and no
conflicting hashes. A read-only check found all 117 files in local diagnostic
output `github/workspaces/claude/aiueos-gate/out31` and matched every declared
SHA-256. The image bytes are outside this repository and are not a durable
release evidence bundle. This check establishes local artifact integrity for
that output only; it does not establish source freshness, hardware behavior,
or native evidence admission. The diagnostic summary is kept under
`github/workspaces/codex/aiueos-scorecard-evidence-audit-20261026`.
