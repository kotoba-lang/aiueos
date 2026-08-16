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

(def ^:private not-built-here
  "Sources no script in this repo compiles, and why.

  Adding a name here is a claim about that object, not a way to quiet the test:
  say what does execute it, or that nothing does."
  {"tcp-seq-acceptable"
   (str "RFC 9293 3.10.7.4, the window-acceptability decision. Nothing links "
        "it because nothing calls it yet: tcp-segment-valid stops before this "
        "question -- it says its own arity is spent -- and giving the probe in "
        "kernel/pci.c a second call site is a change to the kernel rather than "
        "to this object. It is driven through the KIR interpreter by "
        "aiueos.tcp-seq-acceptable-parity-test and required to agree with "
        "kotoba-lang/org-ietf-tcp's tcp.seq on every input, so the wrap "
        "arithmetic it had to copy is checked rather than asserted.")

   "murakumo-join-plan"
   (str "A node's own fleet-enrolment decision. It has no caller inside the "
        "kernel, so no build links it; kotoba-lang/murakumo drives it through "
        "the KIR interpreter in aiueos-join-plan-parity-test and compares every "
        "result against murakumo.infer.join. The encoding both sides share is "
        "pinned by contracts/murakumo-node-v1.edn.")})

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
