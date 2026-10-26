# aiueos (shell)

This repository is the **shell** of aiueos. The components (the OS tree `os/`, the sources, resources, tests, qualification suite and documentation) are the library `kotoba.aiueos` in [kotoba-lang/kotoba](https://github.com/kotoba-lang/kotoba) under `lib/kotoba.aiueos`; `lib/kotoba.vmm` carries the VMM decision cores it uses.

What stays here: the session app (`apps/session`), the distributions (`distributions/`), the computer example with its backing container (`examples/computer`) and the maintainer profile (`hermes/`). It is not usable on its own; it needs that library and an operator.

Everything that moved out is in this repository's history up to `0093b25`, and the files are byte-identical in the library (sha256 per file in `lib/kotoba.aiueos/imports.json`). Split on 2026-10-26 by owner decision: a core product is core only as a library; as an app it is an Itonami dapp.
