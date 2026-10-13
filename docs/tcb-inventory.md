# Trusted computing base inventory

Status: machine checked  
Inventory version: 3  
Date: 2026-10-14

The authoritative inventory is
`qualification/tcb-inventory.edn`. It lists every in-repository source boundary
whose compromise can grant authority, escape Wasm isolation, falsify
production admission or audit evidence, or access hardware outside a
component. Each entry has a role and SHA-256 digest.

Kototama execution and Grant-backed manifest admission are separate pinned
source entries: portable VM qualification does not implicitly qualify the
permission decision path.

Run:

```sh
kbb -M:tcb-check
```

The check fails on missing files, duplicate paths, unsupported inventory
versions, missing roles or any source digest drift. Regulated boot evidence
must bind the inventory digest and assert that this check passed.

## External dependencies are content-addressed (inventory version 3)

`:tcb/external` records the same kind of fact for code this repository does not
contain. A dependency is pinned by *content*, never by a path or a bare version
string:

| source | content address | verified against |
| --- | --- | --- |
| `:maven` | SHA-256 of the resolved jar | the jar in the local Maven repository the running JVM resolved from |
| `:git` | `:git-sha` (git's own content address for the tree) | `deps.edn`, and `security-adoption.edn` for the shared security package |
| `:platform` | none possible from inside this repository | must carry an explicit `:assurance-gap` |

Three checks make the record non-decorative, all fail-closed:

1. **no unpinned dependency** — an entry with neither a content address nor a
   declared `:assurance-gap` is an error;
2. **no silent drift** — the inventory's version/commit must equal `deps.edn`'s,
   and the shared security commit must also equal `security-adoption.edn`'s, so
   the three records of one fact cannot disagree;
3. **no dependency outside the inventory** — every coordinate in `deps.edn`'s
   base `:deps` and every profile named by `:tcb/profiles` must appear in
   `:tcb/external`; each profile's entries are checked against that alias only.

The digest check cannot be skipped by an unresolved environment: the jars must
already be resolved for the JVM running the check to have started.

Version 1 recorded the external TCB as unverified prose, and had drifted — the
shared security package was recorded as a local-root path while `deps.edn` had
moved to a commit pin, and `io.github.kotoba-lang/abi` was absent entirely.
Both are the classes of error the checks above now reject.

## Included boundaries

- authority schema, manifest identity, signing, signer lifecycle, policy and
  broker;
- Kototama artifact admission and execution, its optional Inga state-machine
  adapter, Wasm compatibility host ABI, security entropy provider, hard
  watchdog, launch control, local topic isolation,
  authenticated network-topic protocol, graph and provider surface;
- deployment admission and PID-1;
- hosted `auth.kotoba.cloud` device authorization, its state machine, and
  device-key possession proof;
- plaintext audit emission, sealed production audit and component-state
  storage;
- HVT, VFIO/IOMMU, virtio, VM launch and boot-image construction.

The base external TCB excludes Chicory. Its parser/runtime jars are recorded
under the optional `:wasm-compat` profile, which is loaded only for explicit
legacy Wasm artifacts. The `:distributed` profile separately inventories
Inga, the committed-replica runtime. The base inventory records the Kototama
engine and IPLD store, portable Kotoba primitives, QR renderer, shared
security and capability-authority packages, hosted desktop session-state
package, owned TLS/HTTP transports, and `java.base`.

`:tcb/classpath` records the transitive closure `:tcb/external` cannot express:
every jar actually on the running classpath, by SHA-256. Chicory entries are
scoped to `:wasm-compat`, not to the default runtime. `org.clojure/clojure`,
`spec.alpha` and `core.specs.alpha` reach it via dependencies and are named by
no `:deps` entry — and the loaded clojure is 1.12.5 where the declaration says
1.12.4. A jar on the classpath and not recorded is an error; recorded-but-absent
is not, because the check runs under more than one alias. Regenerate digests
with `kbb -M:test:tcb-check classpath`; roles are a human judgement and are
not generated.

The inventory is also the SBOM's component list — `aiueos.sbom` derives release
attestations from it rather than scanning a second time (ADR-0017), so an
inventory that fails its drift check produces evidence that reports
`:tcb-drift-check? false` instead of a passing claim.

## Evidence still required

Digest pinning detects unreviewed drift but does not establish correctness.
A-02 remains incomplete until the inventory is independently reviewed, the
broker/grant/link invariants receive selected formal models, and the
hypervisor/runtime boundary receives an escape-oriented penetration test and
retest.

One scope limit is recorded rather than hidden (ADR-0016):

- `java.base` carries `:minimum-version 25` and cannot be content-addressed from
  inside this repository, so nothing verifies it. `.github/workflows/ci.yml`
  provisions temurin 21, which does not satisfy that floor; whether the floor or
  the runner is wrong is unresolved.
