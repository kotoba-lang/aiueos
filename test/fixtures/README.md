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
`df1850895ba525ad38eb6755e5e0f862aa8b3d87`. The exact Amu provenance sidecar
and publication marker are
checked in beside the block. This is the default `kototama-core-v1` program,
not an EVM or native/Wasm deployment artifact.

- Program CID: `bafyreihewgmseht6nsokbezrb5kx6hyietul3piqwzqhtxqvjczmerxtre`
- File SHA-256: `e4b199221e7e6c9ca093310f557f1f0824e8bdbd10b66079de1548b2c246f389`
- Entry: `main`, returns `42`
- Value profile: `kotoba.i64-wrap/v1`
- Exports: `main`
