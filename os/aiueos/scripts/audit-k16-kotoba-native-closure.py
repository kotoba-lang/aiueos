#!/usr/bin/env python3
"""Report the physical K16 pure-native closure without upgrading claims."""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parent.parent
CONTRACT = ROOT / "contracts" / "k16-kotoba-native-closure-v1.edn"
EXPECTED_COMPILER = "795800cedbf602108c801aee13704f3af8c65043"


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    text = CONTRACT.read_text(encoding="utf-8")
    kernel_builder = (ROOT / "scripts" / "build-kotoba-native-kernel.sh").read_text(
        encoding="utf-8"
    )
    boot_builder = (ROOT / "scripts" / "build-kotoba-native-boot.sh").read_text(
        encoding="utf-8"
    )
    pure_source = ROOT / "native" / "kernel.kotoba"

    expected_pin = f"expected={EXPECTED_COMPILER}"
    reference_files = {
        "nic": ["kernel/rtl8125.c", "kernel/rtl8125.h"],
        "https": ["kernel/tls13.c", "kernel/tls_aes_gcm.c"],
        "qwen": [
            "kernel/qwen35_infer.c",
            "kernel/qwen35_quant.c",
            "kernel/qwen35_runtime.c",
        ],
    }
    native_sources = {
        "nic": [],
        "https": [
            str(pathlib.Path("kotoba") / "aes128-gcm.kotoba"),
            str(pathlib.Path("kotoba") / "hkdf-sha256.kotoba"),
            str(pathlib.Path("kotoba") / "tls13-record.kotoba"),
        ],
        "qwen": [
            str(pathlib.Path("kotoba") / "qwen35-gguf-header-valid.kotoba")
        ],
    }
    layers = {
        "boot": {
            "state": "implemented",
            "source": str(pure_source.relative_to(ROOT)),
            "source_sha256": sha256(pure_source),
            "compiler_pin_matches": expected_pin in kernel_builder
            and expected_pin in boot_builder,
        }
    }
    for name, paths in reference_files.items():
        present = [path for path in paths if (ROOT / path).is_file()]
        layers[name] = {
            "state": "not-implemented-in-pure-closure",
            "foreign_reference_files_present": present,
            "native_sources": {
                path: sha256(ROOT / path)
                for path in native_sources[name]
                if (ROOT / path).is_file()
            },
        }

    declared_incomplete = all(
        marker in text
        for marker in (
            ":all-native-ready? false",
            ":status :in-progress",
            ":nic {:state :not-implemented-in-pure-closure",
            ":https {:state :not-implemented-in-pure-closure",
            ":qwen {:state :not-implemented-in-pure-closure",
        )
    )
    result = {
        "format": "aiueos.k16-kotoba-native-closure-audit/v1",
        "contract": str(CONTRACT.relative_to(ROOT)),
        "contract_declares_incomplete": declared_incomplete,
        "compiler": EXPECTED_COMPILER,
        "layers": layers,
        "all_native_ready": False,
        "next_blocker": "rtl8125-pci-mmio-dma-provider-in-kotoba",
    }
    json.dump(result, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")

    if not declared_incomplete or not layers["boot"]["compiler_pin_matches"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
