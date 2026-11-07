# Agent rules

AIUEOS is part of a Kotoba / Amu selfhost-only Lisp machine, with Kotoba as its
language and Amu as its compiler (owner decision 2026-11-07, AWAI-160).

- Its build, packaging and runtime stay on Kotoba and Amu selfhost.
- Apps and OS features may be built first in `.cljc` as Awai Otehon and used
  later as an oracle for the selfhost implementation. See
  `network-awai/network-awai-apex` ADR 0026 D18.
- Otehon is not an AIUEOS release or distribution, and it never enters an AIUEOS
  route or acts as a fallback.
- Oracle rules are in `kotoba-lang/kotoba-lang/AGENTS.md`.
