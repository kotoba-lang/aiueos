# Mac native selfhost build progress (2026-10-26)

This is a local Apple Silicon build candidate for JUN-417 and JUN-89. It is
separate from Linux static-ELF builder qualification, QEMU boot, the Itonami
desktop gate, release signing and physical hardware admission.
The bounded result is recorded in
[the evidence receipt](../docs/evidence/aiueos-mac-native-build-20261026/receipt.json).

## Native product route measured

- Signed arm64 Mac Amu full compiler SHA-256:
  e56548a0717e819c411f56393fa8657c06d28e3028213f4de231eaca87fd3d63.
  Signed Kotoba Mac packager SHA-256:
  4a656761e34e47cdcb640d68a1f4e99425253cdc1d69bea41c8c6e4eded69be0.
  Both passed strict code-signature verification; otool listed no dylibs.
- The Mac native font builder assembled the same 41,640-byte Kotoba source
  unity as the Linux comparator (SHA-256
  eb7179906ab035f6668a210e0ad8777ff6d5678c2548d516e5612ef939e4dba9).
  Amu compiled it to a Mac container, extracted main, and the Kotoba packager
  emitted a signed Mach-O. The native program padded 7,422 licensed glyphs
  to 524,288 bytes, SHA-256
  2dec7f39b3e2daea96d595805adaa9ed45b96eca912c955b515067e30a435b5f.
- The Mac native kernel builder staged the current AIUEOS Kotoba tree and
  manifest-pinned owner files as virtual absolute paths beneath FD3. The
  native modules command discovered 31 modules. The KSL1 lock was 3,937
  bytes, SHA-256
  37495e49f1133c747f2f4da84b4606db7f671ffce65277aa33f93e2b7de0444a.
  The Mac compiler reproduced the desktop-entry kernel ELF at fuel
  9007199254740991: 3,350,808 bytes, SHA-256
  0234d8a727e4f7803f0e89b7b0a52dc0d63e5a0061cb658f67e9f6685850ae19.
  Native package-aiueos-boot reproduced the EFI with the Mac-generated font:
  3,903,488 bytes, SHA-256
  6d1c32811ff076e3d060469893702c69c64bceab14659107fe67b93f6135bf7f.
- The comparator build in this exact AIUEOS checkout used the same 31 source
  hashes, source unity/font bytes and kernel fuel on a Linux arm64 static Amu
  image. Direct SHA-256 and size comparisons show the Mac font, kernel ELF
  and EFI are byte-identical to those comparator outputs. This comparison
  did not execute Linux during the Mac build. A wrong Mac compiler pin was
  refused before creating an output directory.

Python staged and observed bytes; it did not compile or package them. Mac
code signing and Mach-O inspection used macOS OS tools. The actual font,
kernel and EFI were produced by Kotoba/Amu native programs.

## Media remains blocked

The unmodified Kotoba media-builder source compiled to a signed Mac program,
but its first FS2 F/E read trapped (-5) because the Mac FS2 provider currently
implements D/A/B/C and KSL1, not F/E. A scratch-only diagnostic replaced
chunked reads with full-file reads: it emitted an 8 MiB GPT/FAT16 disk whose
SHA-256 matched the Linux comparator exactly,
dcbb5274789f345d20519aa61137166085fe511d5ad6ec7b42f02bca5ef6b60a.
That diagnostic trapped while reading back the full 8 MiB file, and is not
an accepted Mac media builder or native verifier. The scratch source was not
adopted. The next product step is bounded FS2 F/E range/stat support in the
Mac runtime, followed by the canonical media builder's positive and negative
gates, independent format verification, and a QEMU boot of that exact media.

The full Mac compiler's fixed-point self-rebuild and end-to-end release or
physical device qualification also remain open.
