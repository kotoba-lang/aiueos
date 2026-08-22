# apps/session — hosted daily shell

The product face of root ADR-2608221625 **P1**. One HTML document, jp-go-dds
(DADS), `--hig-*` via `jp-go-dds.tokens/skin-css`. Fragments `#session`
`#desktop` `#setup` `#manage` `#devices`. `#desktop` is the compositor
face (named partial); it is not a second document.

This is not `clojure -M:cloud-live check`. CID read and murakumo infer leave
from the **session process** (`POST /api/session/read-cid`,
`POST /api/session/infer`) when the operator presses a button in this
document.

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
clojure -M:session serve   # leave the SPA up
clojure -M:phone-bind smoke  # P1b: same SPA, headless QEMU + phone HTTP bind
clojure -M:compositor smoke  # named-partial desktop: same SPA + surfaces + virtio-gpu-pci
```
