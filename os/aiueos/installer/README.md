# aiueos encrypted storage and second-disk installer (first slice)

This directory contains two deliberately separate host-side tools:

1. `encrypted-image.mjs` defines `aiueos.encrypted-chunks.v1`, a deterministic,
   canonical container format for encrypted local data images.
2. `install.mjs` validates an existing aiueos release-image receipt and can
   copy that boot image only to an explicitly named, empty, second internal
   disk after layered destructive confirmation.

Neither tool is a native aiueos block driver. The encrypted data image is not
yet mounted by the bare-metal kernel, and the installer does not create an
encrypted root filesystem. TPM sealing, automatic unlock, Secure Boot key
management, partition resizing, and in-place Windows dual boot are **not
implemented**.

## Encrypted image format

The v1 format uses AES-256-GCM chunks because authenticated AES-XTS is not a
single safe primitive exposed by the chosen standard Node runtime. A 256-bit
recovery key is expanded by HKDF-SHA-256 into separate metadata-authentication,
data-encryption, and key-verification keys. Each data chunk uses a random
64-bit image nonce prefix plus its unsigned 32-bit chunk index as a unique
96-bit GCM nonce. Index `0xffffffff` is reserved for metadata under its separate
metadata key and is never a data-chunk index. Canonical metadata is authenticated, and every chunk
binds its index, clear length, and metadata digest as additional authenticated
data. Truncation, reordering, extension, wrong keys, and byte tampering fail
closed.

Fresh images randomly generate both a 256-bit salt and nonce prefix. Tests may
inject fixed values to produce byte-identical vectors; production callers
must not reuse such values with a recovery key. Recovery keys use the checked,
versioned representation `AIUEOS1-...-CHECKSUM`. Store that key offline: there
is no TPM or escrow path in this slice.

```sh
node os/aiueos/installer/encrypted-image-cli.mjs keygen
node os/aiueos/installer/encrypted-image-cli.mjs encrypt \
  --input data.img --output data.aiueenc --recovery-key 'AIUEOS1-...'
node os/aiueos/installer/encrypted-image-cli.mjs decrypt \
  --input data.aiueenc --output recovered.img --recovery-key 'AIUEOS1-...'
```

Output files are created with mode `0600` and are never overwritten.
The current host implementation reads the complete clear/encrypted image into
memory; it is a format and integrity slice, not yet a large-volume streaming
implementation. Passing a recovery key on a command line may expose it through
shell history or process inspection, so production key-file/console input is
also still required before deployment.

## Internal-disk installer

The installer never lists-and-picks a target. `--device`, `--image`, and
`--receipt` are mandatory. Without `--install` it only validates and reports.

```sh
node os/aiueos/installer/install.mjs \
  --device /dev/diskN \
  --image build/aiueos/aiueos-x86_64-gpt.img \
  --receipt build/aiueos/aiueos-x86_64-build-receipt.json
```

An install is refused unless OS inspection establishes all of these facts:

- target is a whole internal disk, not a partition;
- target has no partition table, child partitions, filesystem signature, or
  mount;
- target differs from every detected current boot/system disk;
- at least one current system disk was positively identified, proving this is
  a second disk;
- release-image size and SHA-256 match its build receipt.

Only after a successful dry run may the operator add all three destructive
arguments. The phrase printed by dry-run includes the exact canonical device.

```sh
sudo node os/aiueos/installer/install.mjs --install \
  --device /dev/diskN --confirm-device /dev/diskN \
  --destructive-phrase 'ERASE /dev/diskN FOR AIUEOS' \
  --image build/aiueos/aiueos-x86_64-gpt.img \
  --receipt build/aiueos/aiueos-x86_64-build-receipt.json
```

The complete written extent is read back and checked against the validated
digest. The target is inspected again after confirmation and before opening it
for writing; any identity or safety-state change aborts. The tool intentionally
cannot install beside Windows on the same disk;
use a physically separate empty disk. Firmware boot-order changes remain a
manual operation.

The fake backend is gated by both `NODE_ENV=test` and
`AIUEOS_INSTALLER_ALLOW_FAKE=1`; it exists only for isolated automated tests.

## Tests

```sh
node --test os/aiueos/installer/test/*.test.mjs
```

Tests use temporary regular files and injected fake devices. They never open a
real block device.
