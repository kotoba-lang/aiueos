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
the receipt's 40-hex commit and nonempty green verdict map. It prints the
recorded score of 48 and category weights, passes and gaps. Seven negative
controls change the required weight, duplicate an ID, remove a receipt
reference, clear owner confirmation, introduce an unknown category, remove a
receipt file, or turn one receipt verdict red. All must produce a nonzero
native result. The positive and negative outputs are checked exactly.

This is a **core schema and receipt-field probe**. Receipt marker text is
not yet checked against the underlying log/image, and the release signature
is not cryptographically verified by this native route. Receipt commit age
and relevant-source freshness remain unqualified. The 48 points describe
the existing reference VM ledger; they do not qualify physical hardware or
a release. The runner and receipt name keep `EVIDENCE_PENDING` until those
gates pass. The legacy checker stays historical and must not be used for
current native qualification.
