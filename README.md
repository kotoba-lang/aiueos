<p align="center">
  <img src="docs/assets/header.png" alt="aiueos" width="480">
</p>

# aiueos

AI-agent-native capability OS examples and documentation, expressed as Kotoba
EDN/CLJ data.

`aiueos` no longer owns a Rust runtime crate. The semantic authority for
manifests, policy decisions, broker plans, audit receipts, and the
`aiueos/component` Wasm Component Model boundary lives in
`../aiueos-cljc-contract`.

This repository keeps examples, deployment notes, incident exercises, and
portable distribution metadata that consume that CLJC/EDN authority. New runtime
or host work should be expressed through Kotoba/CLJ contracts and Wasm Component
Model boundaries rather than reintroducing Rust adapters.

## Authority

- `../aiueos-cljc-contract/src/aiueos/contract.cljc`
- `../aiueos-cljc-contract/resources/aiueos/*.edn`
- `examples/**/*.edn`
- `examples/**/*.clj`

## Verify

```bash
cd ../aiueos-cljc-contract
bb test:cljc
```
