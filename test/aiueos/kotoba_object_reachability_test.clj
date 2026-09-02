(ns aiueos.kotoba-object-reachability-test
  "Every `os/aiueos/kotoba/*.kotoba` is either built into something this repo
  ships, or is named below with the reason it is not.

  ## Why this exists

  The kernel's decisions live in these objects — C owns registers, MMIO, the
  GDT and paging, and the judgements (admission, verification, capability
  issue, dispatch planning) are compiled Kotoba linked into the ELF. Which is
  true of 57 of them. It was not true of all of them, and nothing said so.

  Measured 2026-08-12: `murakumo-join-plan.kotoba` is compiled by no script
  here. It is referenced by `contracts/murakumo-node-v1.edn` and executed only
  by a parity test in a different repository, so on this side it is a source
  file that no build reads. That is a defensible state for it to be in — a
  node's self-enrolment decision has no caller inside the kernel — but it has
  to be a stated one, because the same shape describes an object that was
  supposed to be linked and quietly is not.

  ## What this does not claim

  Being referenced by a build script is not being executed. `build-multiboot.sh`
  links 10 of these and says in comments which it deliberately leaves out; the
  UEFI path links 57. This asks the weaker question — is anything at all built
  from this source — because that is the one whose answer should never be no
  by accident."
  (:require [clojure.java.io :as io]
            [clojure.string :as str]
            [clojure.test :refer [deftest is]]))

(def ^:private kotoba-dir (io/file "os" "aiueos" "kotoba"))
(def ^:private scripts-dir (io/file "os" "aiueos" "scripts"))

(def ^:private value-runtime-reason
  (str "The process-local ValueRuntime. No build links any of the twelve, "
       "because the thing they would be linked INTO does not build: "
       "`value-runtime-kernel-image` is one of them, and it is still refused -- "
       "kernel-value-provider-queue, kernel-value-runtime-capability-table and "
       "kernel-publish-current-domain are three facilities the kernel would "
       "have to publish through hidden-context slots it does not have yet. "
       "What executes them is os/aiueos/scripts/aiueos/verify-<name>.clj, one "
       "per object, driven by aiueos.verify-value-runtime-all: it compiles each "
       "against the pinned compiler and checks it against its contract, and the "
       "receipt is qualification/value-runtime-baseline.edn. Three of the twelve "
       "now compile and verify (arena, cas-verify, syscall-plan). They are "
       "declared here rather than wired into a build because linking an object "
       "into the kernel image is a decision about the kernel, not a way to "
       "satisfy this test."))

(def ^:private not-built-here
  "Sources no script in this repo compiles, and why.

  Adding a name here is a claim about that object, not a way to quiet the test:
  say what does execute it, or that nothing does."
  (merge
   {"tcp-seq-acceptable"
   (str "RFC 9293 3.10.7.4, the window-acceptability decision. Nothing links "
        "it because nothing calls it yet: tcp-segment-valid stops before this "
        "question -- it says its own arity is spent -- and giving the probe in "
        "kernel/pci.c a second call site is a change to the kernel rather than "
        "to this object. It is driven through the KIR interpreter by "
        "aiueos.tcp-seq-acceptable-parity-test and required to agree with "
        "kotoba-lang/org-ietf-tcp's tcp.seq on every input, so the wrap "
        "arithmetic it had to copy is checked rather than asserted.")

   ;; These two were UNDECLARED and this test was red for them on main before
   ;; ADR-0132 touched anything -- neither is built by a script and neither was
   ;; listed here, which is exactly the state the list exists to make
   ;; impossible. Declared now, with what does execute them, because the same
   ;; measurement that had to be made for the two objects below answers them:
   "cid-v1-admit"
   (str "CANNOT be built here, and that is a measured fact rather than an "
        "omission: it declares `(:require [aiueos.value-runtime-sha256 ...])`, "
        "so it is a multi-module project, and `amu compile` refuses to package "
        "one for x86_64-aiueos-kernel-v1 at all -- "
        ":kotoba.error/namespace-require-needs-project, and --unpinned does not "
        "change it (measured 2026-09-02 against amu b1fdaad2). That is why no "
        "`.o` sits beside it. What executes it is contracts/cid-v1-admit-v1.edn "
        "through os/aiueos/scripts/verify-admissions.cljs, which links the two "
        "modules itself and runs 14 vectors and a trap through the KIR "
        "interpreter.")

   "unixfs-file-admit"
   (str "A single-module source with no committed object and no build. What "
        "executes it is contracts/unixfs-file-admit-v1.edn through "
        "os/aiueos/scripts/verify-admissions.cljs -- 22 vectors covering all "
        "18 of its reason codes, measured through the KIR interpreter. Nothing "
        "in the kernel calls it yet: the UnixFS root it admits names blocks the "
        "model channel fetches, and giving that path a call site is a change to "
        "the kernel rather than a way to satisfy this test.")

   "hkdf-sha256"
   (str "HMAC-SHA256 and HKDF-Expand-Label, the TLS 1.3 key schedule "
        "(ADR-0132). Its two neighbours -- aes128-gcm and tls13-record -- are "
        "linked and are called by kernel/tls13.c since stage 5; this one is "
        "NOT, and the reason is measured rather than pending. Compiled and "
        "linked into the UEFI kernel and handed the RFC 4231 test case 1 that "
        "its own contract runs in the KIR interpreter, the object does not "
        "return: it exhausts its 10,000,000-fuel budget and traps `ud2`, and "
        "with the budget patched to 2,147,483,647 it exhausts that too. "
        "Measured 2026-09-02 under QEMU 10.1 TCG; the faulting instruction is "
        "the fuel guard `cmpq $0,0x8(%r9)` in the object's own prologue, so "
        "this is the compiler's lowering and not a bound this repository can "
        "raise. What executes it remains contracts/hkdf-sha256-v1.edn through "
        "verify-admissions.cljs: 18 vectors from RFC 4231 section 4 and RFC "
        "8448 section 3.")

   "qwen35-gguf-header-valid"
   (str "The GGUF v3 container header of the Qwen3.8-27B artifact -- magic, "
        "version, tensor count, metadata count -- ported from the opening of "
        "kernel/qwen35_runtime.c's aiueos_qwen35_model_parse. No script "
        "compiles it yet because the C still owns the whole parse: swapping "
        "its header guards for a call to this object is a change to the "
        "kernel, and it lands with the two objects beside it (the metadata "
        "scan and the tensor table) rather than one at a time, so that the C "
        "is never half-delegated. It is driven through the KIR interpreter by "
        "aiueos.qwen35-gguf-header-parity-test over the same 24 header bytes "
        "scripts/smoke-qwen35-runtime.sh accepts, and every one of its eight "
        "refusal codes is required to be the only producer of its own code.")

   "qwen35-gguf-kv-scan"
   (str "The 50 GGUF metadata entries between the container header and the "
        "tensor table, against the 31 keys kernel/qwen35_runtime.c's "
        "parse_metadata requires and the scalars exact_contract_valid "
        "requires. Unbuilt for the same reason as its neighbour above: the C "
        "still owns the whole parse, and the three objects land in the build "
        "together so it is never half-delegated. It is driven through the KIR "
        "interpreter by aiueos.qwen35-gguf-kv-scan-parity-test over a "
        "10,945,379-byte metadata section rebuilt in the test and pinned to "
        "the sha256 of the fixture scripts/smoke-qwen35-runtime.sh feeds the "
        "C, including the 495,907-string tokenizer walk that admission "
        "actually costs.")

   "qwen35-tensor-table-bind"
   (str "The 866-record GGUF tensor table against the exact graph: name to "
        "role, role to shape, the contiguous 32-aligned extents, the ggml type "
        "histogram, and the per-layer role sets of the hybrid schedule. The "
        "third and last of the Qwen3.8 admission objects, and unbuilt with the "
        "other two -- they land in the build together so the C is never "
        "half-delegated. It is driven through the KIR interpreter by "
        "aiueos.qwen35-tensor-table-parity-test over a 51,242-byte table "
        "rebuilt in the test and pinned to the sha256 of the same slice of the "
        "fixture the C gate accepts, and it must reproduce the four tensor "
        "offsets tests/qwen35_runtime_model.c asserts.")

   "murakumo-join-plan"
   (str "A node's own fleet-enrolment decision. It has no caller inside the "
        "kernel, so no build links it; kotoba-lang/murakumo drives it through "
        "the KIR interpreter in aiueos-join-plan-parity-test and compares every "
        "result against murakumo.infer.join. The encoding both sides share is "
        "pinned by contracts/murakumo-node-v1.edn.")}
   (zipmap ["value-handle-arena" "value-handle-plan"
            "value-runtime-capability-table" "value-runtime-cas-verify"
            "value-runtime-digest-equal" "value-runtime-dispatch"
            "value-runtime-domain" "value-runtime-entry"
            "value-runtime-provider-policy" "value-runtime-provider-transport"
            "value-runtime-sha256" "value-runtime-syscall-plan"]
           (repeat value-runtime-reason))))

(defn- basenames []
  (->> (.listFiles kotoba-dir)
       (filter #(str/ends-with? (.getName %) ".kotoba"))
       (map #(str/replace (.getName %) #"\.kotoba$" ""))
       sort))

(def ^:private script-text
  (delay
    (->> (.listFiles scripts-dir)
         (filter #(.isFile %))
         (map slurp)
         (str/join "\n"))))

(defn- built-here? [base]
  ;; Objects reach a build as `kotoba/<name>.o`; `user-smoke` reaches it as an
  ;; ELF that goes into the image as an application rather than into the kernel.
  (or (str/includes? @script-text (str "kotoba/" base ".o"))
      (str/includes? @script-text (str "kotoba/" base ".elf"))
      (str/includes? @script-text (str "kotoba/" base ".kotoba"))))

(deftest the-kotoba-directory-is-not-empty
  ;; A gate that reads an empty directory reports that every source is fine.
  (is (<= 59 (count (basenames)))
      "source count only grows; a shrunk directory means sources were dropped"))

(deftest every-kotoba-source-is-built-or-declared-unbuilt
  ;; This carried ^:upstream-blocked until 2026-08-20, and `:test-fleet` left it
  ;; out, because the twelve value-* objects were red and nothing here could fix
  ;; them: they were blocked on kotoba-lang/amu#625 and #626, and a gate that
  ;; cannot go green until someone else answers is not a gate for this fleet
  ;; (ADR-0050, ADR-0063).
  ;;
  ;; #626 is answered and #625 landed as a lock, so three of the twelve now
  ;; compile and verify. The tripwire that watched for exactly that -- the one
  ;; assertion in this repository that failed on GOOD news -- fired, and its
  ;; instruction was to stop excluding this. The twelve are now DECLARED rather
  ;; than silently unbuilt, which is the answer this test was always asking for,
  ;; and the exclusion is gone rather than left as decoration.
  (doseq [base (basenames)]
    (is (or (built-here? base) (contains? not-built-here base))
        (str base ".kotoba is compiled by no script in os/aiueos/scripts and is"
             " not listed in `not-built-here`. Either wire it into a build, or"
             " add it there with what does execute it."))))

(deftest the-declared-exceptions-still-exist-and-are-still-unbuilt
  ;; An entry that outlives its source, or one kept after the object was wired
  ;; into a build, turns the list above into decoration.
  (doseq [[base reason] not-built-here]
    (is (.exists (io/file kotoba-dir (str base ".kotoba")))
        (str base " is declared unbuilt but has no source file"))
    (is (not (built-here? base))
        (str base " is declared unbuilt but a script builds it — drop the entry"))
    (is (<= 40 (count reason))
        (str base " needs a reason, not a placeholder"))))
