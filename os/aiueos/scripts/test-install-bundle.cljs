#!/usr/bin/env nbb
;; The install USB bundle, EXECUTED rather than merely built (aiueos ADR-0209,
;; root ADR adr-2608136000 question 8).
;;
;; What this exists to catch, stated as the failure it was written after:
;;
;;   On 2026-09-09 this repository's .cljs tooling moved from clojure.string to
;;   kotoba.lang.text. The fix -- where that namespace lives -- was written
;;   into nbb.edn. nbb.edn is not on the USB. Measured 2026-09-10 on main: the
;;   live installer's /init extracts the bundle, hands over to
;;   install-live.cljs, and it dies with `Could not find namespace:
;;   kotoba.lang.text` before doing anything at all. So did install-to-disk,
;;   install-intent and make-provision-record -- the whole chain.
;;
;;   Nothing was red. `install-usb-build` built a USB whose receipt chain
;;   verified, `installer-test` and `install-chain-test` ran the same scripts
;;   from the REPOSITORY, where nbb.edn resolves, and stayed green. The QEMU
;;   install gate (I3) was measured 2026-08-25, two weeks before the move, and
;;   its green was about a tree that no longer existed.
;;
;; So the assertion here is deliberately not "the bundle was produced" and not
;; "the tar contains N files". It assembles the bundle the builder produces,
;; extracts it, and RUNS each bundled entry point from inside it -- exactly
;; where /init runs them, with only what the stick carries. A script is allowed
;; to refuse for its own named reason (no device, no arguments); it is not
;; allowed to fail to load.
;;
;; The control is the other half: the same assembly WITHOUT the classpath must
;; produce the namespace error. A detector that cannot see the defect it was
;; written for is not a detector, and this one has been shown both ways.
;;
;; Exit 0 only when every case held and the count matches.

(require '[kotoba.lang.text :as str])

(def fs (js/require "node:fs"))
(def path (js/require "node:path"))
(def cp (js/require "node:child_process"))
(def os-mod (js/require "node:os"))

(def scripts-dir (.dirname path *file*))
(def repo (.resolve path scripts-dir ".." ".." ".."))
(def builder (.join path scripts-dir "make-install-usb-image.py"))
(def live-builder (.join path scripts-dir "make-live-installer.py"))
(def tmp (.mkdtempSync fs (.join path (.tmpdir os-mod) "aiueos-bundle-")))

(def results (atom []))
(defn- record! [name ok detail]
  (swap! results conj {:name name :ok ok :detail detail})
  (println (if ok "AIUEOS_BUNDLE_TEST_OK  " "AIUEOS_BUNDLE_TEST_FAIL") name detail))

(defn- run [cmd args opts]
  (let [r (.spawnSync cp cmd (to-array args)
                      (js/Object.assign #js {:encoding "utf8" :shell false}
                                        (clj->js (or opts {}))))]
    {:status (if (nil? (.-status r)) 3 (.-status r))
     :out (or (.-stdout r) "") :err (or (.-stderr r) "")}))

;; --------------------------------------------------------------- assemble

(def intent-path (.join path tmp "install-intent.json"))
(.writeFileSync fs intent-path
                (.stringify js/JSON
                            #js {"schema" "aiueos.install-intent.v1"
                                 "mode" "interactive"
                                 "hostname" "aiueos-bundle-test"}))
(def receipt-path (.join path tmp "release-receipt.json"))
(.writeFileSync fs receipt-path
                (.stringify js/JSON #js {"schema" "aiueos.build-receipt.v1"}))

(defn- assemble!
  "Build the bundle tar through the REAL builder function and extract it.

  The builder module is imported and `make_bundle_tgz` is called directly:
  building a whole USB image would need a release image, a Linux node binary
  and an nbb tree, none of which an offline test has, and none of which change
  what is being measured -- whether the files the bundle carries can load."
  [label classpath-dirs]
  (let [tgz (.join path tmp (str label ".tgz"))
        dest (.join path tmp label)
        py (str "import sys, pathlib; sys.path.insert(0, " (pr-str scripts-dir) ");\n"
                "import importlib.util;\n"
                "spec = importlib.util.spec_from_file_location('mk', " (pr-str builder) ");\n"
                "mk = importlib.util.module_from_spec(spec); spec.loader.exec_module(mk);\n"
                "data = mk.make_bundle_tgz(" (pr-str (.join path repo "os/aiueos/installer")) ",\n"
                "  " (pr-str scripts-dir) ",\n"
                "  pathlib.Path(" (pr-str intent-path) ").read_bytes(),\n"
                "  pathlib.Path(" (pr-str receipt-path) ").read_bytes(),\n"
                ;; JSON, not pr-str. A Clojure vector prints as ["a" "b"], and
                ;; Python reads adjacent string literals as ONE concatenated
                ;; string -- so two roots silently became a single path that
                ;; does not exist, the builder produced a clean bundle with no
                ;; cp/ in it, and nothing said so. That is what put the
                ;; empty-root refusal in the builder.
                "  None, None, classpath_dirs="
                (.stringify js/JSON (clj->js (vec classpath-dirs))) ");\n"
                "pathlib.Path(" (pr-str tgz) ").write_bytes(data)\n")
        r (run "python3" ["-c" py] {})]
    (when-not (zero? (:status r))
      (println "  builder stderr:" (str/trim (:err r))))
    (.mkdirSync fs dest #js {:recursive true})
    (let [x (run "tar" ["xzf" tgz "-C" dest] {})]
      {:ok (and (zero? (:status r)) (zero? (:status x)))
       :dir (.join path dest "aiueos-installer")
       :err (str (:err r) (:err x))})))

(def text-src (.resolve path repo ".." "text" "src"))
(def aiueos-src (.join path repo "src"))

(record! "west-sibling-text-src-exists" (.existsSync fs text-src) text-src)

(def with-cp (assemble! "with-cp" [text-src aiueos-src]))
(def without-cp (assemble! "without-cp" []))

(record! "bundle-assembles-with-classpath" (:ok with-cp) (:dir with-cp))
(record! "bundle-assembles-without-classpath" (:ok without-cp) (:dir without-cp))

;; ------------------------------------------------------- what it carries

(record! "bundle-carries-kotoba-lang-text"
         (.existsSync fs (.join path (:dir with-cp) "cp/kotoba/lang/text.cljc"))
         "cp/kotoba/lang/text.cljc")
(record! "bundle-carries-the-guided-decision-core"
         (.existsSync fs (.join path (:dir with-cp) "cp/aiueos/installer/guided.cljc"))
         "cp/aiueos/installer/guided.cljc")
(record! "bundle-carries-the-guided-program"
         (.existsSync fs (.join path (:dir with-cp) "guided-install.cljs"))
         "guided-install.cljs (flattened from installer/live/)")

;; ---------------------------------------------------- the entry points run

(def entry-points
  ["install-live.cljs"        ; what /init hands over to
   "install-to-disk.cljs"     ; what install-live spawns
   "install-intent.cljs"      ; the admission
   "make-provision-record.cljs"
   "guided-install.cljs"])    ; ADR-0208

(defn- load-check
  "Run one bundled script from inside the bundle, with only what the stick
  carries. Returns whether it LOADED -- the script is free to refuse for its
  own reason once running."
  [dir script classpath]
  (let [r (run "nbb" (concat (when classpath ["--classpath" classpath]) [script])
               {:cwd dir
                :env (js/Object.assign #js {} (.-env js/process)
                                       #js {"AIUEOS_LIVE_PAYLOAD_DEV" "/dev/aiueos-test-absent"})})
        text (str (:out r) (:err r))]
    {:loaded (not (str/includes? text "Could not find namespace"))
     :status (:status r)
     :missing (second (str/re-find #"Could not find namespace: (\S+)" text))}))

(doseq [script entry-points]
  (let [{:keys [loaded status missing]}
        (load-check (:dir with-cp) script (.join path (:dir with-cp) "cp"))]
    (record! (str "loads-from-bundle:" script) loaded
             (if loaded
               (str "loaded; refused on its own terms, status=" status)
               (str "COULD NOT LOAD: " missing)))))

;; The control. Without the classpath every one of them must fail to load --
;; that is the state main was in on 2026-09-10, and a green control here would
;; mean this test cannot see the defect it exists for.
(let [broken (mapv (fn [script]
                     [script (:loaded (load-check (:dir without-cp) script nil))])
                   entry-points)]
  (record! "control-without-classpath-nothing-loads"
           (every? (comp false? second) broken)
           (str/join " " (map (fn [[s l]] (str s "=" (if l "LOADED" "no-namespace"))) broken))))

;; ------------------------------------------------- the two halves must agree

;; The bundle can carry cp/ and /init can still fail to pass it. That is a
;; drift between two files, so it is asserted between two files.
(def init-text (.readFileSync fs live-builder "utf8"))
(record! "init-passes-the-classpath-on-the-install-handover"
         (str/includes? init-text
                        "--classpath /run/aiueos-installer/cp install-live.cljs")
         "make-live-installer.py /init")

;; install-live.cljs spawns `nbb` BY NAME for the second hop, so the shim on
;; the stick is where that hop gets its classpath. The shim is only emitted
;; with an nbb tree, so this reads the builder source rather than a bundle
;; assembled without one.
(def builder-text (.readFileSync fs builder "utf8"))
(record! "nbb-shim-carries-the-classpath"
         (str/includes? builder-text "--classpath \"$DIR/cp\" ")
         "bin/nbb shim in make_bundle_tgz")

;; Two source roots merge into one cp/. A shared relative path would let one
;; file silently replace the other, and which one survives depends on argument
;; order -- so it refuses instead.
(let [r (run "python3"
             ["-c" (str "import sys, importlib.util, pathlib;\n"
                        "spec = importlib.util.spec_from_file_location('mk', " (pr-str builder) ");\n"
                        "mk = importlib.util.module_from_spec(spec); spec.loader.exec_module(mk);\n"
                        "try:\n"
                        "  mk.make_bundle_tgz(" (pr-str (.join path repo "os/aiueos/installer")) ",\n"
                        "    " (pr-str scripts-dir) ", b'{}', b'{}', None, None,\n"
                        "    classpath_dirs=[" (pr-str text-src) ", " (pr-str text-src) "])\n"
                        "  print('NO_REFUSAL')\n"
                        "except ValueError as e:\n"
                        "  print('REFUSED' if 'written twice' in str(e) else 'WRONG_REFUSAL', e)\n")]
             {})]
  ;; The same root twice is byte-identical, so it must NOT refuse -- the check
  ;; is on differing bytes, not on a repeated name. This case pins that the
  ;; refusal is about content, not about tidiness.
  (record! "identical-duplicate-roots-are-not-a-conflict"
           (str/includes? (:out r) "NO_REFUSAL")
           (str/trim (str (:out r) (:err r)))))

;; A root that exists but carries nothing is refused too: "asked for a root and
;; got no files" must not be reportable as "no root was asked for".
(let [empty-root (.join path tmp "empty-root")]
  (.mkdirSync fs empty-root #js {:recursive true})
  (let [r (run "python3"
               ["-c" (str "import importlib.util;\n"
                          "spec = importlib.util.spec_from_file_location('mk', " (pr-str builder) ");\n"
                          "mk = importlib.util.module_from_spec(spec); spec.loader.exec_module(mk);\n"
                          "try:\n"
                          "  mk.make_bundle_tgz(" (pr-str (.join path repo "os/aiueos/installer")) ",\n"
                          "    " (pr-str scripts-dir) ", b'{}', b'{}', None, None,\n"
                          "    classpath_dirs=[" (pr-str empty-root) "])\n"
                          "  print('NO_REFUSAL')\n"
                          "except ValueError as e:\n"
                          "  print('REFUSED' if 'carries no' in str(e) else 'WRONG_REFUSAL', e)\n")]
               {})]
    (record! "empty-classpath-root-refuses"
             (str/includes? (:out r) "REFUSED")
             (str/trim (str (:out r) (:err r))))))

(let [fake (.join path tmp "fake-root/kotoba/lang")]
  (.mkdirSync fs fake #js {:recursive true})
  (.writeFileSync fs (.join path fake "text.cljc") "(ns kotoba.lang.text) ;; imposter\n")
  (let [r (run "python3"
               ["-c" (str "import sys, importlib.util;\n"
                          "spec = importlib.util.spec_from_file_location('mk', " (pr-str builder) ");\n"
                          "mk = importlib.util.module_from_spec(spec); spec.loader.exec_module(mk);\n"
                          "try:\n"
                          "  mk.make_bundle_tgz(" (pr-str (.join path repo "os/aiueos/installer")) ",\n"
                          "    " (pr-str scripts-dir) ", b'{}', b'{}', None, None,\n"
                          "    classpath_dirs=[" (pr-str text-src) ", "
                          (pr-str (.join path tmp "fake-root")) "])\n"
                          "  print('NO_REFUSAL')\n"
                          "except ValueError as e:\n"
                          "  print('REFUSED' if 'written twice' in str(e) else 'WRONG_REFUSAL', e)\n")]
               {})]
    (record! "colliding-roots-refuse-rather-than-overwrite"
             (str/includes? (:out r) "REFUSED")
             (str/trim (str (:out r) (:err r))))))

;; ------------------------------------------------------------------ summary

(def expected-cases 17)
(def total (count @results))
(def failed (filterv (complement :ok) @results))

(println)
(println "AIUEOS_BUNDLE_TEST_SUMMARY"
         (str "ran=" total) (str "expected=" expected-cases)
         (str "failed=" (count failed)))
(doseq [f failed] (println "  FAILED" (:name f) (:detail f)))
(when (not= total expected-cases)
  (println "  COUNT MISMATCH: a run that executed fewer cases is not a clean run"))

(.exit js/process (if (and (empty? failed) (= total expected-cases)) 0 1))
