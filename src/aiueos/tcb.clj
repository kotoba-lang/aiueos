(ns aiueos.tcb
  "Machine-verifiable trusted-computing-base inventory.

  Two halves, both fail-closed:

  - **in-repository sources** (`:tcb/files`) are pinned by SHA-256 of the file
    itself, so changing trusted code without an inventory update fails;
  - **external dependencies** (`:tcb/external`) are pinned by *content*, not by
    a path or a bare version string, and cross-checked against the two other
    records of the same fact (`deps.edn`, `security-adoption.edn`) so the three
    cannot silently disagree.

  A Maven dependency's content address is the SHA-256 of its resolved jar; a git
  dependency's is its commit id (git's own content address for the tree). An
  entry that is neither content-addressable nor carrying an explicit
  `:assurance-gap` is an error — an unpinned dependency may not pass silently."
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.string :as str])
  (:import [java.security MessageDigest]))

(def inventory-path "qualification/tcb-inventory.edn")
(def deps-path "deps.edn")
(def adoption-path "security-adoption.edn")

(def inventory-version
  "The only inventory shape this checker admits. Version 2 added content
  addressing for `:tcb/external`; version 1 recorded it as unverified prose."
  2)

(def security-coordinate "io.github.kotoba-lang/security")

(def external-sources
  "`:source` values an external entry may declare.

  `:platform` is the host runtime (`java.base`), which this repository cannot
  content-address from inside itself — such an entry must carry an explicit
  `:assurance-gap` instead."
  #{:maven :git :platform})

(defn- hex [^bytes value]
  (apply str (map #(format "%02x" (bit-and (int %) 0xff)) value)))

(defn sha256-file [path]
  (with-open [input (io/input-stream path)]
    (let [digest (MessageDigest/getInstance "SHA-256")
          buffer (byte-array 16384)]
      (loop []
        (let [read (.read input buffer)]
          (when (pos? read)
            (.update digest buffer 0 read)
            (recur))))
      (hex (.digest digest)))))

(defn read-inventory []
  (edn/read-string (slurp inventory-path)))

(defn- read-edn-file [path]
  (let [file (io/file path)]
    (when (.exists file)
      (edn/read-string (slurp file)))))

(defn- maven-local-repo
  "The local Maven repository the running JVM resolved its jars from."
  [deps]
  (io/file (or (System/getProperty "maven.repo.local")
               (:mvn/local-repo deps)
               (str (System/getProperty "user.home") "/.m2/repository"))))

(defn maven-jar
  "The local-repository jar for COORDINATE at VERSION, in Maven's
  group-as-directories layout (`com.dylibso.chicory/wasm` 1.4.0 ->
  `com/dylibso/chicory/wasm/1.4.0/wasm-1.4.0.jar`)."
  [repo coordinate version]
  (let [[group artifact] (str/split (str coordinate) #"/")
        artifact (or artifact group)]
    (apply io/file repo (concat (str/split group #"\.")
                                [artifact version (str artifact "-" version ".jar")]))))

(defn- declared-dependencies
  "`deps.edn`'s `:deps` keyed by coordinate string. Alias dependencies are out
  of scope on purpose: `:tcb/excluded` already excludes tests and qualification
  fixtures, which is where they are used."
  [deps]
  (into {} (map (fn [[coordinate coords]] [(str coordinate) coords])) (:deps deps)))

(defn- maven-entry-errors [repo {:keys [coordinate version sha256]} declared-dep]
  (cond
    (not= version (:mvn/version declared-dep))
    [{:kind :external-version-drift :coordinate coordinate
      :expected version :actual (:mvn/version declared-dep)}]

    (not (string? sha256))
    [{:kind :external-unpinned :coordinate coordinate}]

    :else
    (let [jar (maven-jar repo coordinate version)]
      (cond
        (not (.exists jar))
        [{:kind :external-artifact-missing :coordinate coordinate
          :path (.getPath jar)}]

        (not= sha256 (sha256-file jar))
        [{:kind :external-digest-drift :coordinate coordinate
          :expected sha256 :actual (sha256-file jar)}]

        :else []))))

(defn- git-entry-errors [{:keys [coordinate git-sha]} declared-dep]
  (cond
    (not (string? git-sha))
    [{:kind :external-unpinned :coordinate coordinate}]

    (not= git-sha (:git/sha declared-dep))
    [{:kind :external-git-sha-drift :coordinate coordinate
      :expected git-sha :actual (:git/sha declared-dep)}]

    :else []))

(defn- entry-errors
  [repo declared {:keys [coordinate role source assurance-gap] :as entry}]
  (let [declared-dep (get declared coordinate)]
    (cond
      (not (keyword? role))
      [{:kind :external-missing-role :coordinate coordinate}]

      (not (contains? external-sources source))
      [{:kind :external-unknown-source :coordinate coordinate :source source}]

      (= :platform source)
      (if (keyword? assurance-gap)
        []
        [{:kind :external-unpinned :coordinate coordinate}])

      (nil? declared-dep)
      [{:kind :external-not-in-deps :coordinate coordinate}]

      (= :maven source) (maven-entry-errors repo entry declared-dep)
      :else (git-entry-errors entry declared-dep))))

(defn- adoption-errors
  "`security-adoption.edn` records the shared security package's commit for the
  adoption gate; the inventory records it for the TCB. They are the same fact,
  so a disagreement is an error rather than two independent truths."
  [entries adoption]
  (let [pinned (:security/git-sha adoption)
        recorded (some #(when (= security-coordinate (:coordinate %)) (:git-sha %))
                       entries)]
    (if (and (string? pinned) (string? recorded) (not= pinned recorded))
      [{:kind :external-adoption-drift :coordinate security-coordinate
        :expected recorded :actual pinned}]
      [])))

(defn- external-errors [inventory deps adoption]
  (let [declared (declared-dependencies deps)
        repo (maven-local-repo deps)
        entries (:tcb/external inventory)
        covered (into #{} (map :coordinate) entries)]
    (concat
     (mapcat #(entry-errors repo declared %) entries)
     ;; A runtime dependency that never reached the inventory is the drift the
     ;; per-entry checks above cannot see.
     (for [coordinate (sort (keys declared))
           :when (not (contains? covered coordinate))]
       {:kind :external-undeclared :coordinate coordinate})
     (adoption-errors entries adoption))))

(defn- file-errors [files]
  (mapcat
   (fn [{:keys [path role sha256]}]
     (let [file (io/file path)]
       (cond
         (not (.exists file))
         [{:kind :missing-file :path path}]
         (not (keyword? role))
         [{:kind :missing-role :path path}]
         (not= sha256 (sha256-file file))
         [{:kind :digest-drift :path path
           :expected sha256 :actual (sha256-file file)}]
         :else [])))
   files))

(defn validate
  "Return structured drift/errors. An inventory update must be an intentional
  review action; changing trusted code — in this repository or in a dependency
  — without updating its content address fails."
  ([] (validate (read-inventory)))
  ([inventory]
   (validate inventory (read-edn-file deps-path) (read-edn-file adoption-path)))
  ([inventory deps adoption]
   (let [files (:tcb/files inventory)
         paths (mapv :path files)
         errors
         (into []
               (concat
                (when-not (= inventory-version (:tcb/version inventory))
                  [{:kind :unsupported-version
                    :actual (:tcb/version inventory)}])
                (when-not (= (count paths) (count (set paths)))
                  [{:kind :duplicate-path}])
                (file-errors files)
                (external-errors inventory deps adoption)))]
     {:valid? (empty? errors)
      :files (count files)
      :external (count (:tcb/external inventory))
      :errors errors})))

(defn -main [& _]
  (let [result (validate)]
    (prn result)
    (when-not (:valid? result)
      (System/exit 1))))
