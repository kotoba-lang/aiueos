# Kototama program fixtures

`amu-kototama-sum-v1.dagcbor` is the Amu output for the Kototama v1 program
used by the portable engine suite. It is copied byte-for-byte from
`kotoba-lang/kotoba-vm` at `0b429729675168309cd3a7810e60e62e65f227cb`, where
the fixture is documented as output from `bin/amu compile --target kototama`.

- Program CID: `bafyreiadqe4yzd45fvkz456jfesqq3tc46th46ufyzermn2mzvixu47edu`
- File SHA-256: `0381398c8f9d2d559e77c92925086e62e7a67e7a85c64916374ccd517a73e41d`
- Methods: `sum` and `choose`
- Value profile: `kotoba.i64-wrap/v1`

Tests load the binary block directly. Do not decode and re-encode it from a
host-language map in the integration path.

`amu-kototama-default-v1.dagcbor` is also compiled by Amu with no `--target`
and `--jvm-free`, from `examples/evm-answer.kotoba` at Amu commit
`1f8eb385f1b29c0bb28bce3583fab89d326b2c72`. The exact Amu provenance sidecar
is checked in beside the block. This is the default `kototama-core-v1`
program, not an EVM or native/Wasm deployment artifact.

- Program CID: `bafyreiddqnpygk2vba4uau2t2nh6lvlreewu6pyaa4slbjhoos6mbswz2e`
- File SHA-256: `63835f832b550839405353d34fe5d571212d4f3f000724b0a4ee74bcc0cad9d1`
- Entry: `main`, returns `42`
- Value profile: `kotoba.i64-wrap/v1`
