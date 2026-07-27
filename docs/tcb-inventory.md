# Trusted computing base inventory

Status: machine checked  
Inventory version: 1  
Date: 2026-07-23

The authoritative inventory is
`qualification/tcb-inventory.edn`. It lists every in-repository source boundary
whose compromise can grant authority, escape Wasm isolation, falsify
production admission or audit evidence, or access hardware outside a
component. Each entry has a role and SHA-256 digest.

Run:

```sh
clojure -M:tcb-check
```

The check fails on missing files, duplicate paths, unsupported inventory
versions, missing roles or any source digest drift. Regulated boot evidence
must bind the inventory digest and assert that this check passed.

## Included boundaries

- authority schema, manifest identity, signing, signer lifecycle, policy and
  broker;
- Wasm host ABI, security entropy provider, hard watchdog, launch control,
  local topic isolation,
  authenticated network-topic protocol, graph and provider surface;
- deployment admission and PID-1;
- plaintext audit emission, sealed production audit and component-state
  storage;
- HVT, VFIO/IOMMU, virtio, VM launch and boot-image construction.

The external TCB records Chicory parser/runtime versions, `java.base`, and the
shared security package. The shared package is currently a local-root
dependency rather than a content-addressed release; the inventory records that
fact as an assurance gap instead of hiding it.

## Evidence still required

Digest pinning detects unreviewed drift but does not establish correctness.
A-02 remains incomplete until the inventory is independently reviewed, the
broker/grant/link invariants receive selected formal models, and the
hypervisor/runtime boundary receives an escape-oriented penetration test and
retest.
