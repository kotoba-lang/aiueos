# apps/session — hosted daily shell

The product face of root ADR-2608221625 **P1**. One HTML document, jp-go-dds
(DADS), `--hig-*` via `jp-go-dds.tokens/skin-css`. Fragments `#session`
`#desktop` `#setup` `#manage` `#devices` `#operator`. `#itonami` is the same operator view. `#desktop` is the hosted WM
face (ADR-0085) plus hosted IME (ADR-0086 / ADR-0088) plus the hosted
kami.webgpu presenter (ADR-0089, `/kami-presenter.js`); it is not a second document.

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
clojure -M:compositor guest-ime  # KERNEL.ELF Kotoba k+a→U+304B
clojure -M:compositor guest-wm   # KERNEL.ELF Kotoba two-surface z-hit
clojure -M:compositor guest-paint # KERNEL.ELF paints both rects in z-order
```
