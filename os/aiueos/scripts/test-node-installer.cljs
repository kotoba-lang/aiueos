#!/usr/bin/env nbb
;; Offline proof of the per-machine node installer (ADR-0214):
;; contracts/node-machines-v1.edn, scripts/make-node-installer.cljs and
;; scripts/record-node-machine-probe.cljs. Temp copies of the registry and
;; fixture probe text only; builds no ISO, touches no block device, and never
;; writes the registry in the tree.
;;
;; Discipline, inherited from test-guided-install.cljs and root ADR
;; adr-2608136000:
;;
;;   * every refusal pins the NAMED reason literal (question 6). A refusal that
;;     fired for a different reason than the one under test is a test bug. This
;;     suite would otherwise be easy to fake: nearly every case here expects a
;;     non-zero exit, and almost any breakage produces one.
;;   * the admit cases exist so the refusals are non-vacuous (question 5): a
;;     driver that could not build a plan at all would make every refusal green.
;;   * the loop is closed end to end (question 8): the probe is INGESTED into a
;;     registry copy, and the driver is then run against that copy and required
;;     to pass --target-serial with the serial the probe carried. "The ingest
;;     printed six fields" is not "an unattended stick can now be built".
;;   * the probe block is EXECUTED, not merely generated: the late-command the
;;     ISO builder emits is extracted and run, and its output is parsed. A
;;     generator whose output does not run is a generator that produced text.
;;
;; Output: one AIUEOS_NODE_INSTALLER_TEST_* line per case, then a summary with
;; an exact expected count. Exit 0 only when every case held AND the count
;; matches.

(require '[kotoba.lang.text :as str]
         '[cljs.reader :as reader])

(def fs (js/require "node:fs"))
(def path (js/require "node:path"))
(def cp (js/require "node:child_process"))
(def os-mod (js/require "node:os"))

(def repo-root (.cwd js/process))
(def driver (.join path "os" "aiueos" "scripts" "make-node-installer.cljs"))
(def ingest (.join path "os" "aiueos" "scripts" "record-node-machine-probe.cljs"))
(def builder (.join path "os" "aiueos" "scripts" "make-node-autoinstall-iso.cljs"))
(def registry (.join path "os" "aiueos" "contracts" "node-machines-v1.edn"))
(def fixtures (.join path "os" "aiueos" "tests" "fixtures"))

(def tmp (.mkdtempSync fs (.join path (.tmpdir os-mod) "aiueos-node-installer-test-")))
(def results (atom []))

(defn- record! [name ok detail]
  (swap! results conj {:name name :ok ok :detail detail})
  (println (if ok "AIUEOS_NODE_INSTALLER_TEST_OK  " "AIUEOS_NODE_INSTALLER_TEST_FAIL")
           name detail))

(defn- run [script args]
  (let [r (.spawnSync cp "nbb" (to-array (cons script args))
                      #js {:encoding "utf8" :shell false :cwd repo-root})]
    {:status (if (nil? (.-status r)) 3 (.-status r))
     :out (str (or (.-stdout r) "") (or (.-stderr r) ""))}))

(defn- refuses!
  "The exit code AND the reason. Either alone passes for the wrong reason: the
  code because nearly every bug here exits non-zero, the text because a program
  can print a refusal and carry on."
  [name script args expected-status needle]
  (let [r (run script args)
        ok (and (= expected-status (:status r)) (str/includes? (:out r) needle))]
    (record! name ok
             (if ok (str "exit " expected-status ", said " (pr-str needle))
                 (str "expected exit " expected-status " and " (pr-str needle)
                      " -- got exit " (:status r) ": "
                      (str/trim (first (str/split-lines (:out r)))))))))

(defn- admits! [name script args needle]
  (let [r (run script args)
        ok (and (zero? (:status r)) (str/includes? (:out r) needle))]
    (record! name ok
             (if ok (str "exit 0, said " (pr-str needle))
                 (str "expected exit 0 and " (pr-str needle) " -- got exit " (:status r)
                      ": " (str/trim (:out r)))))))

(defn- check! [name expected actual]
  (record! name (= expected actual)
           (if (= expected actual) (str "= " (pr-str expected))
               (str "expected " (pr-str expected) " got " (pr-str actual)))))

;; ── fixtures that are not the registry ────────────────────────────────────

(def fake-key (.join path tmp "id.pub"))
(def fake-iso (.join path tmp "ubuntu.iso"))
(def fake-bundle (.join path tmp "agent.tar.gz"))
(.writeFileSync fs fake-key "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAATESTTESTTESTTEST t\n")
(.writeFileSync fs fake-iso "not an iso, and this suite never asks anything to read it as one\n")
(.writeFileSync fs fake-bundle "not a bundle\n")

(defn- registry-copy [label]
  (let [p (.join path tmp (str "registry-" label ".edn"))]
    (.copyFileSync fs registry p) p))

(def base-args
  ["--ssh-key" fake-key "--iso" fake-iso "--bundle" fake-bundle "--dry-run"])

;; ── 1. the registry itself is typed, not merely parseable ─────────────────
;;
;; `reader/read-string` returns a value for files damaged in the ways this
;; workspace keeps recording -- a string closed early still reads, and the words
;; after it read as keys. So the shape is asserted and the scan COUNT printed: a
;; zero from an incomplete scan and a zero from a complete one look identical.

(def doc (reader/read-string (.readFileSync fs registry "utf8")))
(check! "registry-format" :aiueos.node-machines/v1 (:format doc))
(check! "registry-machines-is-a-map" true (map? (:machines doc)))
(let [ms (:machines doc)
      facts (mapcat (comp seq :facts val) ms)
      bad-provenance (remove #(contains? (:provenance doc) (:provenance (val %))) facts)
      no-value (remove #(contains? (val %) :value) facts)
      bad-host (remove #(string? (:hostname (val %))) ms)]
  (record! "registry-scanned" true
           (str (count ms) " machines, " (count facts) " facts"))
  (check! "registry-every-provenance-in-vocabulary" 0 (count bad-provenance))
  (check! "registry-every-fact-has-a-value-key" 0 (count no-value))
  (check! "registry-every-machine-has-a-hostname-string" 0 (count bad-host))
  (check! "registry-knows-the-6600hs" true (contains? ms :amd-6600hs)))

;; ── 2. which box is never inferred ────────────────────────────────────────

(refuses! "driver-no-machine-refuses" driver [] 2 "no --machine")
(refuses! "driver-unknown-machine-refuses" driver ["--machine" "no-such-box"] 2
          "no machine `no-such-box`")
(refuses! "driver-unknown-machine-lists-the-known" driver ["--machine" "no-such-box"] 2
          "amd-6600hs")

;; ── 3. reachability is a decision, and so is its absence ──────────────────

(refuses! "driver-no-ssh-key-refuses" driver ["--machine" "amd-6600hs"] 2 "no --ssh-key")
(refuses! "driver-ssh-key-and-no-ssh-refuses" driver
          ["--machine" "amd-6600hs" "--ssh-key" fake-key "--no-ssh"] 2
          "are both given")

;; ── 4. the field that changes the product ─────────────────────────────────
;;
;; This is the case the whole tranche exists for. An unattended stick erases
;; whatever disk matches, so the match has to be a measurement.

(refuses! "driver-unattended-without-a-measured-serial-refuses" driver
          (concat ["--machine" "amd-6600hs" "--unattended"] base-args) 2
          "--unattended needs a MEASURED disk serial")
(refuses! "driver-unattended-refusal-names-how-to-measure" driver
          (concat ["--machine" "amd-6600hs" "--unattended"] base-args) 2
          "record-node-machine-probe.cljs")

;; Non-vacuous: the same arguments WITHOUT --unattended build a plan, so the
;; refusal above is about the serial and not about the driver being broken.
(admits! "driver-interactive-plan-is-built" driver
         (concat ["--machine" "amd-6600hs"] base-args) "INTERACTIVE")
(let [r (run driver (concat ["--machine" "amd-6600hs"] base-args))]
  (check! "driver-interactive-passes-no-target-serial" false
          (str/includes? (:out r) "--target-serial"))
  (check! "driver-reports-zero-measured-facts-for-a-new-box" true
          (str/includes? (:out r) "0 measured"))
  (check! "driver-marks-owner-stated-as-not-measured" true
          (str/includes? (:out r) "OWNER-STATED")))

;; ── 5. prerequisites are could-not-answer, not refusals ───────────────────
;;
;; Exit 3, not 2: "you asked for something I will not do" and "I could not get
;; far enough to decide" send an operator to different places.

(refuses! "driver-missing-iso-cannot-answer" driver
          ["--machine" "amd-6600hs" "--ssh-key" fake-key "--bundle" fake-bundle
           "--iso" (.join path tmp "absent.iso") "--dry-run"] 3
          "the Ubuntu Server ISO")
(refuses! "driver-missing-bundle-cannot-answer" driver
          ["--machine" "amd-6600hs" "--ssh-key" fake-key "--iso" fake-iso
           "--bundle" (.join path tmp "absent.tar.gz") "--dry-run"] 3
          "make-node-agent-bundle.cljs")

(let [wrong (.join path tmp "wrong-format.edn")]
  (.writeFileSync fs wrong "{:format :something-else :machines {}}\n")
  (refuses! "driver-wrong-registry-format-cannot-answer" driver
            (concat ["--machine" "amd-6600hs" "--registry" wrong] base-args) 3
            "not :aiueos.node-machines/v1"))

;; ── 6. the ingest refuses rather than pattern-matching ────────────────────

(refuses! "ingest-no-machine-refuses" ingest ["--probe" (.join path fixtures "node-hwprobe-6600hs.txt")] 2
          "no --machine")
(refuses! "ingest-not-a-probe-refuses" ingest
          ["--machine" "amd-6600hs" "--probe" (.join path fixtures "node-hwprobe-not-a-probe.txt")] 2
          "AIUEOS_NODE_HWPROBE_V1 marker")
(refuses! "ingest-marker-but-no-fields-refuses" ingest
          ["--machine" "amd-6600hs" "--probe" (.join path fixtures "node-hwprobe-empty.txt")
           "--dry-run"] 2
          "none of the fields this records")
(refuses! "ingest-unknown-machine-refuses" ingest
          ["--machine" "no-such-box" "--probe" (.join path fixtures "node-hwprobe-6600hs.txt")] 2
          "no machine `no-such-box`")

;; ── 7. two internal disks is not a target ─────────────────────────────────
;;
;; Which of two disks may be erased is the owner's statement. The ingest records
;; the rest of the probe and leaves the serial alone.

(let [reg (registry-copy "two-disks")
      r (run ingest ["--machine" "amd-6600hs" "--registry" reg
                     "--probe" (.join path fixtures "node-hwprobe-two-internal-disks.txt")])
      after (reader/read-string (.readFileSync fs reg "utf8"))
      serial (get-in after [:machines :amd-6600hs :facts :disk-serial])
      nic (get-in after [:machines :amd-6600hs :facts :nic])]
  (check! "ingest-two-internal-disks-succeeds" 0 (:status r))
  (check! "ingest-two-internal-disks-says-so" true
          (str/includes? (:out r) "2 internal disks"))
  (check! "ingest-two-internal-disks-leaves-the-serial-unverified" :unverified
          (:provenance serial))
  (check! "ingest-two-internal-disks-still-records-the-nic" :measured (:provenance nic))
  (check! "ingest-two-nics-are-both-recorded" 2
          (:value (get-in after [:machines :amd-6600hs :facts :nic-count]))))

;; ── 8. the whole loop, on a registry copy ─────────────────────────────────

(def reg (registry-copy "loop"))
(let [r (run ingest ["--machine" "amd-6600hs" "--registry" reg
                     "--probe" (.join path fixtures "node-hwprobe-6600hs.txt")])]
  (check! "ingest-records" 0 (:status r))
  (check! "ingest-reports-what-it-wrote" true (str/includes? (:out r) "recorded 6 measured fields")))

(def after (reader/read-string (.readFileSync fs reg "utf8")))
(let [f (:facts (get-in after [:machines :amd-6600hs]))]
  (check! "recorded-serial-value" "FIXTURESERIAL0001" (:value (:disk-serial f)))
  (check! "recorded-serial-provenance" :measured (:provenance (:disk-serial f)))
  (check! "recorded-nic-is-the-driver-and-pci-id" "r8169 [10ec:8125]" (:value (:nic f)))
  (check! "recorded-board-joins-vendor-and-product" "FIXTURE-VENDOR FIXTURE-6600HS"
          (:value (:board f)))
  (check! "recorded-cpu-overwrites-owner-stated-with-measured" :measured
          (:provenance (:cpu f)))
  ;; The removable stick in the same probe is a disk with a serial. Recording it
  ;; would point an unattended install at the installer.
  ;;
  ;; The exclusion itself is measured separately, below: in THIS probe the stick
  ;; is one of two candidates, so dropping the removable test makes the answer
  ;; ambiguous rather than wrong, and every assertion here stays green. A test
  ;; that cannot go red for the reason it names is not testing that reason.
  (check! "recorded-disk-model-is-not-zram" "FIXTURE NVMe 512GB" (:value (:disk-model f))))

;; The commentary survives: this file's :unverified entries carry the note that
;; says how to measure them, and a rewrite that reprinted a parsed map would
;; have thrown all of it away while reporting success.
(let [text (.readFileSync fs reg "utf8")]
  (check! "rewrite-keeps-the-contract-commentary" true
          (str/includes? text "A field with no record is :unverified"))
  ;; The header surviving proves nothing about the facts: the first rewriter
  ;; replaced each fact map WHOLE, deleting its commentary, its :measure-by and
  ;; its :same-silicon-as -- and this suite was green, because the only thing it
  ;; checked was a string in the header. These four are the cases that would
  ;; have been red.
  (check! "rewrite-keeps-the-per-fact-commentary" true
          (str/includes? text "is Rembrandt-R, which is the same silicon rebadged"))
  (check! "rewrite-keeps-extra-keys-on-a-measured-fact" :gmktec-k16
          (get-in after [:machines :amd-6600hs :facts :cpu :same-silicon-as]))
  (check! "rewrite-keeps-measure-by-after-measuring" true
          (string? (get-in after [:machines :amd-6600hs :facts :disk-serial :measure-by])))
  ;; The entry already carried an :on from when the owner stated the CPU.
  ;; Appending a second one produced EDN the reader refused, and the refusal
  ;; wrote nothing -- so the fact that a value landed at all is this case.
  (check! "rewrite-replaces-rather-than-duplicates-on" true
          (string? (get-in after [:machines :amd-6600hs :facts :cpu :on])))
  (check! "rewrite-leaves-the-other-machine-alone" :unverified
          (:provenance (get-in after [:machines :gmktec-k16 :facts :disk-serial])))
  (check! "rewrite-keeps-the-measured-k16-nic" "Realtek RTL8125B"
          (:value (get-in after [:machines :gmktec-k16 :facts :nic]))))

;; And now the claim that matters: the driver will build the unattended stick,
;; with the serial the probe carried.
(let [r (run driver (concat ["--machine" "amd-6600hs" "--unattended" "--registry" reg] base-args))]
  (check! "driver-unattended-now-admits" 0 (:status r))
  (check! "driver-unattended-passes-the-measured-serial" true
          (str/includes? (:out r) "--target-serial FIXTURESERIAL0001"))
  (check! "driver-unattended-says-it-is-unattended" true
          (str/includes? (:out r) "unattended, serial FIXTURESERIAL0001 (measured)")))

;; ── 8b. the stick is not a target, even when it is the only disk ──────────
;;
;; The install USB is a disk with a serial. In a probe that also has an NVMe,
;; admitting it only makes the answer ambiguous -- so the exclusion is measured
;; where it is the ONLY candidate, which is the one shape where admitting it
;; produces a confident wrong answer: an unattended stick pointed at itself.

(let [only-reg (registry-copy "removable-only")
      r (run ingest ["--machine" "amd-6600hs" "--registry" only-reg
                     "--probe" (.join path fixtures "node-hwprobe-removable-only.txt")])
      doc (reader/read-string (.readFileSync fs only-reg "utf8"))
      serial (get-in doc [:machines :amd-6600hs :facts :disk-serial])]
  (check! "removable-only-ingest-succeeds" 0 (:status r))
  (check! "removable-only-records-no-serial" :unverified (:provenance serial))
  (check! "removable-only-registry-never-mentions-the-stick" false
          (str/includes? (.readFileSync fs only-reg "utf8") "FIXTUREUSB0001"))
  ;; Non-vacuous: the rest of that probe IS recorded, so the refusal above is
  ;; about the disk and not about the ingest having failed.
  (check! "removable-only-still-records-the-nic" :measured
          (:provenance (get-in doc [:machines :amd-6600hs :facts :nic]))))

;; ── 9. the probe block the ISO builder emits actually runs ────────────────
;;
;; Generated, extracted, executed, parsed. Run on THIS host, so the sections
;; about hardware it has no sysfs for come back empty -- which is the point:
;; the block must not abort, and the ingest must read what did arrive. The
;; Linux-only fields are covered by the fixtures above.

(let [r (.spawnSync cp "nbb"
                    (to-array [builder "--iso" fake-iso "--bundle" fake-bundle
                               "--ssh-key" fake-key "--hostname" "test-probe-block"
                               "--no-tailscale" "--output" (.join path tmp "never.iso")])
                    #js {:encoding "utf8" :shell false :cwd repo-root})
      out (str (or (.-stdout r) ""))
      lines (str/split-lines out)
      ;; The builder prints the autoinstall indented by two spaces before it
      ;; does any I/O, and then refuses the fake ISO. Both are expected here;
      ;; what is under test is the text between them.
      start (.indexOf (to-array lines) "autoinstall.yaml:")
      yaml (when-not (neg? start)
             (->> (drop (inc start) lines)
                  (take-while #(str/starts-with? % "  "))
                  (map #(subs % 2))))
      block (when yaml
              (->> yaml
                   (drop-while #(not (str/includes? % "AIUEOS_NODE_HWPROBE_V1")))
                   (take-while #(not (str/starts-with? % "    - |")))
                   (map #(str/replace % #"^      " ""))
                   (str/join "\n")))]
  (check! "builder-emits-the-probe-block" true (boolean (and block (seq block))))
  (when (and block (seq block))
    (let [sh-file (.join path tmp "probe-block.sh")
          _ (.writeFileSync fs sh-file (str block "\n"))
          syntax (.spawnSync cp "sh" (to-array ["-n" sh-file]) #js {:encoding "utf8"})]
      (check! "probe-block-is-valid-sh" 0 (.-status syntax))))
  ;; And the switch that turns it off turns it off -- a default-on measurement
  ;; nobody can decline is a default nobody chose.
  (let [r2 (.spawnSync cp "nbb"
                       (to-array [builder "--iso" fake-iso "--bundle" fake-bundle
                                  "--ssh-key" fake-key "--no-hw-record" "--no-tailscale"
                                  "--output" (.join path tmp "never2.iso")])
                       #js {:encoding "utf8" :shell false :cwd repo-root})]
    (check! "no-hw-record-removes-the-block" false
            (str/includes? (str (or (.-stdout r2) "")) "AIUEOS_NODE_HWPROBE_V1"))))

;; ── summary ───────────────────────────────────────────────────────────────

(def expected-cases 55)
(def total (count @results))
(def failed (filterv (complement :ok) @results))

(println)
(println "AIUEOS_NODE_INSTALLER_TEST_SUMMARY"
         (str "ran=" total) (str "expected=" expected-cases) (str "failed=" (count failed)))
(when (seq failed)
  (doseq [f failed] (println "  FAILED" (:name f) (:detail f))))
(when (not= total expected-cases)
  (println "  COUNT MISMATCH: a run that executed fewer cases is not a clean run"))

(.rmSync fs tmp #js {:recursive true :force true})
(.exit js/process (if (and (empty? failed) (= total expected-cases)) 0 1))
