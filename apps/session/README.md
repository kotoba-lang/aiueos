# apps/session — hosted daily shell

The product face of root ADR-2608221625 **P1**. The OS UI engine contract is
`kotoba-lang/browser`; `kotoba-browser.edn` is the boundary. One HTML document, jp-go-dds
(DADS), `--hig-*` via `jp-go-dds.tokens/skin-css`. Fragments `#session`
`#desktop` `#setup` `#manage` `#devices` `#operator`. `#itonami` is the same operator view. `#desktop` is the hosted WM
face (ADR-0085) plus hosted IME (ADR-0086 / ADR-0088) plus the hosted
kami.webgpu presenter (ADR-0089, `/kami-presenter.js`); it is not a second document.
The committed HTML/JavaScript build is a hosted verification adapter and does
not count as the native Kotoba Browser guest. The native product target is the
same document/action model in `kotoba-clj/WASM` through browser desktop-backend
contract v1.

This is not `clojure -M:cloud-live check`. CID read and murakumo infer leave
from the **session process** (`POST /api/session/read-cid`,
`POST /api/session/infer`) when the operator presses a button in this
document. P3 notes guest is `GET /api/session/guests` and
`POST /api/session/guest` (`{"grant":"allow"}` / `{"grant":"deny"}`).
P4 operator is `GET/POST /api/session/operator` against `itonami.cloud`
(not folded into smoke).

## Generate the document

From a checkout that has `jp-go-digital-design-system`, `html`, and `css`:

```bash
nbb --classpath "apps/session/src:<dds>/src:<dds>/resources:<html>/src:<css>/src" \
  apps/session/generate.cljs -- --repo . --dds-css <dds>/resources/jp_go_dds/dds.css
```

`index.html` is generated. Do not hand-edit it.

## Serve / prove

```bash
clojure -M:session smoke   # HTTP only; real kotobase GET + murakumo infer
clojure -M:session guest   # P3: grant-limited app/notes in this document
clojure -M:session operator  # P4: grant-gated live itonami.cloud from this document
clojure -M:session serve   # leave the SPA up
clojure -M:phone-bind smoke  # P1b: same SPA, headless QEMU + phone HTTP bind
clojure -M:compositor smoke  # named-partial desktop: same SPA + surfaces + virtio-gpu-pci
clojure -M:compositor wm     # hosted WM: two stacked windows, z-order, DADS title bars
clojure -M:compositor ime    # hosted IME: romaji→kana in the same document
clojure -M:compositor kanji  # hosted IME: Space converts か→加
nbb --classpath src scripts/compositor-guest.cljs guest-ime  # KERNEL.ELF Kotoba k+a→U+304B
nbb --classpath src scripts/compositor-guest.cljs guest-wm   # KERNEL.ELF Kotoba two-surface z-hit
nbb --classpath src scripts/compositor-guest.cljs guest-paint # KERNEL.ELF paints both rects in z-order
nbb --classpath src scripts/compositor-guest.cljs guest-input # KERNEL.ELF virtio-keyboard used-ring event
nbb --classpath src scripts/compositor-guest.cljs guest-gpu-two # KERNEL.ELF two virtio-gpu 2D resources
nbb --classpath src scripts/compositor-guest.cljs guest-scanout-two # KERNEL.ELF scanout 1 bound to resource 2
nbb --classpath src scripts/compositor-guest.cljs guest-broker # KERNEL.ELF Kotoba clipboard admit / picker refuse
nbb --classpath src scripts/compositor-guest.cljs guest-session # KERNEL.ELF Kotoba packed front 2 restore
```

From the superproject, the generated document can also be rendered by the
actual Kotoba Browser engine (no Chrome/Playwright engine):

```bash
cd /tmp
clojure -Sdeps '{:paths ["<aiueos>/scripts"] :deps {io.github.kotoba-lang/browser {:local/root "<superproject>/orgs/kotoba-lang/browser"}}}' \
  -M -m aiueos.kotoba-browser-smoke <aiueos>/apps/session/index.html
```
