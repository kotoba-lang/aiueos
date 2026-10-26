# AIUEOS distributions

AIUEOS is the common OS foundation. **Itonami OS is its Awai Network distribution**,
maintained in this same repository. Both use the same kernel, drivers, syscall
ABI, build pipeline and shared authority/runtime contracts.

| Distribution ID | Display name | Configuration owner | Purpose |
|---|---|---|---|
| `aiueos` | AIUEOS | `kotoba-lang` | General OS foundation |
| `awai` | Itonami OS | `network-awai` | Awai-oriented composition and distribution |

The JSON files are static build inputs describing composition intent. They
are not app release manifests, validators, grants, enrollment records or an
installer. `presetIntent` names integration targets; it does not claim those
components are installed or qualified. Future distributions use the same
repository and core; adding a name does not add a new runtime profile.

The native desktop candidate accepts `--distribution aiueos|awai` (default
`aiueos`). The shell wrapper accepts `AIUEOS_DISTRIBUTION=awai`. It stages an
exact copy as `distribution.json` and binds its SHA-256/size in
`native-desktop.json`. Both selections invoke the identical native core
producer. The selection currently changes candidate metadata and composition
intent; it does not change kernel/UI bytes or install the intended components.
The output remains a diagnostic candidate, not a qualified Itonami OS release.

```sh
AIUEOS_DISTRIBUTION=awai \
AIUEOS_NATIVE_DESKTOP_OUT="$OUT" \
  os/aiueos/scripts/build-kotoba-native-desktop.sh "$AMU"
```

Use the pinned native compiler, owner roots and build queue described in
[native desktop/media](../90-docs/native-desktop-media-v1.md). Distribution is
independent of architecture, runtime target, security profile and diagnostic
entry selection. Existing Browser/CSSOM, user target, workspace restore and
signed release requirements remain component-specific gates. The publisher
must bind this configuration, core and component digests in the existing
OS release authority, retaining the separate app release authority (AWAI-86);
the local metadata is not a signed release.

The [decision](../90-docs/adr/0274-awai-os-is-an-aiueos-distribution.md) defines
ownership, composition and migration. Keep namespace `aiueos.*`, wire/schema,
ABI and historic receipts intact. Legacy NixOS installers and Linux VM monitor
previews keep their original names and qualification scope.
