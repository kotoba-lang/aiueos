# apps/session — hosted daily shell

The product face of root ADR-2608221625 **P1**. AIUEOS opens on an app desktop
with launch links for Murakumo, Itonami, Yataverse, and Aozora. The app surfaces
are `.cljk` markup rendered by `kotoba-lang/browser` using the `jp-go-dds`
design tokens. Murakumo invokes the existing session inference route and
Itonami invokes the existing grant-gated operator route. Yataverse and Aozora
open their own app views; their service catalogs are not connected in this
build. `#session`, `#compositor`, `#setup`, `#manage`, and `#devices` remain
available as system views. `#operator` remains an alias for `#itonami`.

The configured native app catalog and Browser desktop entrypoint live in
`src/aiueos/session/native_desktop.cljk`. It registers all four apps as
Browser-owned surface state; `browser.input` launches them from the app row or
centered Dock and closes their windows, while `browser.surface` and
`cssom.layout` produce a retained frame through `browser.desktop-backend` v1.
Repeated launches focus the existing window. These native app windows are
launchable shells; the Murakumo inference and Itonami operator API actions are
currently in the hosted adapter. This path does not use the hosted document's
JavaScript.
`src/aiueos/session/app_catalog.cljk` supplies the app labels to the hosted
Browser route and the Amu guest renderer from one ordered catalog; its Kotoba
branch provides the title codepoints directly to the bounded framebuffer
surface.

The committed HTML/JavaScript output remains a hosted verification adapter. It
does not count as the native Kotoba Browser guest or as a C-free AIUEOS boot.
The Browser surface smoke runs on kbb's Node host. It does not compile or
execute an Amu-built Browser guest, and the current C-free kernel still has no
framebuffer/input transport for this session entrypoint.
The formal Passkey authority, origin, and WebAuthn RP ID are
`auth.kotoba.cloud`; `auth.itonami.cloud` is not an aiueos OS-auth route.

Device addition uses the authority's one-time `/v1/aiueos/device/*` flow
(ADR-0113), not its generic Device Code API. Both buttons start the same bound
ceremony. `Passkey` opens the public approval URL;
`phone-scan` renders that same URL as a QR locally in the AIUEOS helper. The
authority's poll token stays only in helper memory and is never returned to the
document, placed in the QR, or persisted. The helper accepts a grant only when
the authority result matches the exact flow, device DID, challenge, model,
method, origin and RP ID, and then separately verifies possession of the local
device key. Browser-supplied `verified` flags have no effect.

The repository test injects the authority boundary and proves pending, grant,
binding refusal, one-time consumption and secret non-disclosure. It is not a
claim that the production authority route has been deployed or that a human
Passkey ceremony has run on the physical K16.

This is not `kbb -M:cloud-live check`. CID read and murakumo infer leave
from the **session process** (`POST /api/session/read-cid`,
`POST /api/session/infer`) when the operator presses a button in this
document. P3 notes guest is `GET /api/session/guests` and
`POST /api/session/guest` (`{"grant":"allow"}` / `{"grant":"deny"}`).
P4 operator is `GET/POST /api/session/operator` against `itonami.cloud`
(not folded into smoke).

## Generate the document

From a checkout that has `jp-go-digital-design-system`, `html`, and `css`:

```bash
kbb --backend sci --classpath "apps/session/src:<dds>/src:<dds>/resources:<html>/src:<css>/src" \
  apps/session/generate.cljk -- --repo . --dds-css <dds>/resources/jp_go_dds/dds.css
```

`index.html` is generated. Do not hand-edit it.

## Serve / prove

```bash
kbb -M:session smoke   # HTTP only; real kotobase GET + murakumo infer
kbb -M:session guest   # P3: grant-limited app/notes in this document
kbb -M:session operator  # P4: grant-gated live itonami.cloud from this document
kbb -M:session serve   # leave the SPA up
kbb -M:phone-bind smoke  # P1b: same SPA, headless QEMU + phone HTTP bind
kbb -M:compositor smoke  # named-partial desktop: same SPA + surfaces + virtio-gpu-pci
kbb -M:compositor wm     # hosted WM: two stacked windows, z-order, DADS title bars
kbb -M:compositor ime    # hosted IME: romaji→kana in the same document
kbb -M:compositor kanji  # hosted IME: Space converts か→加
kbb --backend sci --classpath src scripts/compositor-guest.cljk guest-ime  # KERNEL.ELF Kotoba k+a→U+304B
kbb --backend sci --classpath src scripts/compositor-guest.cljk guest-wm   # KERNEL.ELF Kotoba two-surface z-hit
kbb --backend sci --classpath src scripts/compositor-guest.cljk guest-paint # KERNEL.ELF paints both rects in z-order
kbb --backend sci --classpath src scripts/compositor-guest.cljk guest-input # KERNEL.ELF virtio-keyboard used-ring event
kbb --backend sci --classpath src scripts/compositor-guest.cljk guest-gpu-two # KERNEL.ELF two virtio-gpu 2D resources
kbb --backend sci --classpath src scripts/compositor-guest.cljk guest-scanout-two # KERNEL.ELF scanout 1 bound to resource 2
kbb --backend sci --classpath src scripts/compositor-guest.cljk guest-broker # KERNEL.ELF Kotoba clipboard admit / picker refuse
kbb --backend sci --classpath src scripts/compositor-guest.cljk guest-session # KERNEL.ELF Kotoba packed front 2 restore
```

From the AIUEOS checkout, the configured `start-session` entrypoint renders
through Kotoba Browser's surface and CSSOM pipeline. The smoke drives each app
from both the launcher and Dock through Kotoba's input reducer, without
JavaScript:

```bash
BROWSER_ROOT="$(cd ../browser && pwd)"
KBB_NO_NPM=1 kbb -Sdeps "{:paths [\"scripts\" \"apps/session/src\"] :deps {io.github.kotoba-lang/browser {:local/root \"$BROWSER_ROOT\"}}}" \
  -M -m aiueos.kotoba-browser-smoke
```

This executes the Kotoba Browser source through kbb's Node bootstrap host. It
does not prove the Amu-built compiler or the C-free kernel can present the
frame; that requires the kernel frame/input transport.
