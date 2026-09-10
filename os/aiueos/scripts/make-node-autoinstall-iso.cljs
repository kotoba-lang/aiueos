#!/usr/bin/env nbb
;; An Ubuntu Server install USB that leaves behind a murakumo node.
;;
;; The bespoke initramfs this replaces lost to hardware, not to the product:
;; an RTL8125 asking for firmware that does not exist upstream, a BIOS ACPI
;; table, a UEFI that would not start a hand-built UKI. The same board runs
;; Ubuntu. So the agent goes onto a distribution that already boots, and the
;; distribution's installer does the part distributions are good at.
;;
;; Since 23.10 subiquity reads `/autoinstall.yaml` off the install media, so no
;; second CIDATA volume and no kernel-cmdline edit is needed -- the file and the
;; agent bundle are added to the ISO and it is repacked with the ORIGINAL boot
;; arguments, which xorriso reports from the source image rather than anyone
;; writing them out again.
;;
;;   nbb os/aiueos/scripts/make-node-autoinstall-iso.cljs \
;;     --iso ubuntu-24.04.4-live-server-amd64.iso \
;;     --bundle build/aiueos/aiueos-node-agent.tar.gz \
;;     --ssh-key ~/.ssh/id_ed25519.pub \
;;     --endpoint https://murakumo.cloud \
;;     --output build/aiueos/aiueos-node-autoinstall.iso

(require '[clojure.string :as str]
         '["node:fs" :as fs]
         '["node:path" :as path]
         '["node:crypto" :as crypto]
         '["node:child_process" :as cp])

(def argv (vec *command-line-args*))
(defn- opt [f d] (let [i (.indexOf argv f)] (if (neg? i) d (nth argv (inc i)))))
(defn- flag? [f] (not (neg? (.indexOf argv f))))

(def iso (opt "--iso" nil))
(def bundle (opt "--bundle" "build/aiueos/aiueos-node-agent.tar.gz"))
(def ssh-key-path (opt "--ssh-key" nil))
(def no-ssh? (flag? "--no-ssh"))
(def endpoint (str/replace (opt "--endpoint" "https://murakumo.cloud") #"/+$" ""))
(def hostname (opt "--hostname" "aiueos-node"))
(def username (opt "--username" "aiueos"))
(def target-serial (opt "--target-serial" nil))
(def console-password
  ;; A file, or the value. The file exists so the password does not sit in
  ;; argv, where `ps` shows it to every process on the machine and the shell
  ;; keeps it in history.
  (or (when-let [f (opt "--console-password-file" nil)]
        (str/trim (fs/readFileSync f "utf8")))
      (opt "--console-password" nil)))
(def no-console-password? (flag? "--no-console-password"))
(def output (opt "--output" "build/aiueos/aiueos-node-autoinstall.iso"))

(defn- die [& m] (println (str "make-node-autoinstall-iso: " (str/join " " m)))
  (js/process.exit 2))
(defn- need [p what] (when-not (and p (fs/existsSync p)) (die "missing" what (str "(" p ")"))) p)

(need iso "the Ubuntu Server ISO (--iso)")
(need bundle "the node agent bundle (--bundle)")

;; A box nobody can log into is not an installed node. install-v1's headless
;; floor is a real login, not a key file's presence, so the absence of a key
;; has to be chosen rather than defaulted into.
(def ssh-key
  (cond
    no-ssh? nil
    ssh-key-path (str/trim (fs/readFileSync (need ssh-key-path "the public key (--ssh-key)") "utf8"))
    :else (die "no --ssh-key. After this install nobody can reach the box."
               "Pass a public key, or --no-ssh to accept that deliberately.")))

(when (and ssh-key (str/includes? ssh-key "PRIVATE KEY"))
  (die "--ssh-key was given a PRIVATE key. Only the public half belongs on an install medium."))

;; ── the autoinstall ───────────────────────────────────────────────────────
;;
;; storage is left interactive unless a serial names the target. install-v1
;; refuses "largest disk" and "the first NVMe" for the reason this inherits:
;; the wrong answer erases the wrong machine, and a stick that guesses is a
;; stick that eventually guesses on a box someone cared about.

(def storage-section
  (if target-serial
    (str "  storage:\n"
         "    layout:\n"
         "      name: direct\n"
         "      match:\n"
         "        serial: " target-serial "\n")
    (str "  storage:\n"
         "    layout:\n"
         "      name: direct\n")))

(def interactive
  (if target-serial "  interactive-sections: []\n" "  interactive-sections:\n    - storage\n"))

;; ── the console door ──────────────────────────────────────────────────────
;;
;; MEASURED, 2026-09-10: the first box built from this ISO installed, rebooted,
;; showed `aiueos-node login:` -- and was reachable by nothing. SSH was the only
;; door and the network had not come up, so the machine sat there with no way
;; in at all. Worse, the throwaway hash also takes `sudo` away from the user it
;; creates, since sudo asks for exactly that password.
;;
;; So a console password is a DECISION now, the same way the SSH key is: either
;; name one, or say out loud that this box gets no console door.
(defn- crypt-sha512 [pw]
  (let [r (.spawnSync cp "openssl" (clj->js ["passwd" "-6" "-stdin"])
                      #js {:encoding "utf8" :input (str pw "\n")})]
    (when-not (zero? (.-status r))
      (die "could not hash the console password:" (str/trim (str (.-stderr r)))))
    (str/trim (str (.-stdout r)))))

;; Random and thrown away: not a password, a refusal to have one. Written so it
;; cannot match any string, rather than being a weak string someone might guess.
(def throwaway-hash
  (str "$6$" (.toString (crypto/randomBytes 8) "hex") "$"
       (.toString (crypto/randomBytes 43) "base64url")))

(def locked-hash
  (cond
    console-password (crypt-sha512 console-password)
    no-console-password? throwaway-hash
    :else (die "no --console-password. If the network does not come up, this box"
               "has no door at all -- and the user it creates cannot sudo either."
               "\n       Pass --console-password <pw>, or --no-console-password to"
               "accept that deliberately.")))

(def autoinstall
  (str "#cloud-config\n"
       "autoinstall:\n"
       "  version: 1\n"
       interactive
       "  locale: en_US.UTF-8\n"
       "  keyboard:\n    layout: us\n"
       "  identity:\n"
       "    hostname: " hostname "\n"
       "    realname: aiueos murakumo node\n"
       "    username: " username "\n"
       "    password: \"" locked-hash "\"\n"
       "  ssh:\n"
       "    install-server: true\n"
       "    allow-pw: false\n"
       (if ssh-key
         (str "    authorized-keys:\n      - \"" ssh-key "\"\n")
         "    authorized-keys: []\n")
       storage-section
       "  late-commands:\n"
       ;; The agent is installed INTO the target, which is not running yet, so
       ;; install.cljs takes --root and enables the unit offline. It reports
       ;; ENABLED_OFFLINE rather than claiming the node is answering: those are
       ;; different states and the first boot is what turns one into the other.
       "    - |\n"
       "      set -eu\n"
       "      mkdir -p /target/opt\n"
       "      cp /cdrom/aiueos-node-agent.tar.gz /target/opt/\n"
       "      tar xzf /cdrom/aiueos-node-agent.tar.gz -C /target/opt\n"
       "      cd /target/opt/aiueos-node-agent\n"
       "      ./node-linux-x64 nbb-bundle/node_modules/nbb/cli.js install.cljs \\\n"
       "        --endpoint " endpoint " --root /target 2>&1 | tee /target/var/log/aiueos-node-install.log\n"))

;; ── repack ────────────────────────────────────────────────────────────────
;;
;; The boot arguments come from the SOURCE image: `xorriso -report_el_torito
;; as_mkisofs` prints what the original was built with, and reusing that is the
;; difference between an ISO that boots the way Canonical's does and one that
;; boots the way someone remembered.

(def work "/work")
(def script
  ;; MEASURED, 2026-09-10: handed a 10-byte file as --iso, this script ran to
  ;; completion and reported success -- a 378 KB "ISO" that boots nothing, with
  ;; a receipt saying it was fine. xorriso extracts nothing and says nothing,
  ;; the el-torito report comes back EMPTY, and mkisofs happily builds a
  ;; data-only image out of the two files we added. So the boot record is
  ;; checked on the way IN and on the way OUT.
  ;;
  ;; What may differ between the two reports was measured, not assumed: the
  ;; appended-partition byte range and the UEFI image LBA move because content
  ;; moved, and everything else -- catalog, BIOS image path, emulation, load
  ;; sizes 4 and 10160, boot-info-table -- must be identical.
  ;;
  ;; Single quotes only, deliberately. The first version of this check wrote
  ;; \" inside these Clojure strings, which on disk is an escaped backslash
  ;; followed by a string terminator: the reader accepted it and the meaning
  ;; changed silently. That is the trap this workspace already documents.
  (str/join
   "\n"
   ["set -eu"
    "apt-get -qq update >/dev/null"
    "apt-get -qq install -y --no-install-recommends xorriso >/dev/null"
    "mkdir -p /out/extract"
    "xorriso -osirrox on -indev /src/SOURCE_ISO -extract / /out/extract >/dev/null 2>&1"
    "chmod -R u+w /out/extract"
    "cp /in/autoinstall.yaml /out/extract/autoinstall.yaml"
    "cp /in/agent.tar.gz /out/extract/aiueos-node-agent.tar.gz"
    "xorriso -indev /src/SOURCE_ISO -report_el_torito as_mkisofs > /out/mkisofs.args 2>/dev/null"
    ;; The pattern lives in a FILE. Written as BOOT='...' and used as $BOOT it
    ;; was split on its own spaces into nine arguments, grep died with
    ;; 'Unmatched (', `|| true` swallowed it, and both sides came out empty --
    ;; which compares EQUAL. A check that cannot run must not look like a check
    ;; that ran and agreed.
    "printf '%s\\n' '^-c ' '^-b ' '^-e ' '^-eltorito-alt-boot' '^-no-emul-boot' '^-boot-load-size ' '^-boot-info-table' '^--grub2-boot-info' '^-appended_part_as_gpt' > /out/boot.pat"
    "grep -E -f /out/boot.pat /out/mkisofs.args | sed 's/^-e .*/-e UEFI-IMAGE-PRESENT/' > /out/src.boot || true"
    "if [ ! -s /out/src.boot ]; then"
    "  echo 'REFUSING: the source image reports no El Torito boot record.'"
    "  echo 'That is not an Ubuntu install ISO, or it could not be read at all.'"
    "  exit 3"
    "fi"
    "if [ ! -d /out/extract/casper ]; then"
    "  echo 'REFUSING: the extracted tree has no casper/ -- not a live-server ISO.'"
    "  exit 3"
    "fi"
    "echo '--- the source image was built with:'"
    "cat /out/mkisofs.args"
    "cd /out/extract"
    "eval xorriso -as mkisofs $(tr '\\n' ' ' < /out/mkisofs.args | sed 's#-o [^ ]*##') -o /out/result.iso . >/dev/null 2>&1"
    "xorriso -indev /out/result.iso -report_el_torito as_mkisofs > /out/result.args 2>/dev/null"
    "grep -E -f /out/boot.pat /out/result.args | sed 's/^-e .*/-e UEFI-IMAGE-PRESENT/' > /out/res.boot || true"
    "if [ ! -s /out/res.boot ]; then"
    "  echo 'REFUSING: the ISO produced reports no El Torito boot record at all.'"
    "  exit 3"
    "fi"
    "if ! cmp -s /out/src.boot /out/res.boot; then"
    "  echo 'REFUSING: the ISO produced does not carry the boot record the source had.'"
    "  diff /out/src.boot /out/res.boot || true"
    "  exit 3"
    "fi"
    "echo '--- the ISO produced carries the same boot record:'"
    "cat /out/res.boot"
    "ls -la /out/result.iso"]))

(def staging (fs/mkdtempSync "/tmp/aiueos-iso-"))
(fs/writeFileSync (path/join staging "autoinstall.yaml") autoinstall)
(fs/copyFileSync bundle (path/join staging "agent.tar.gz"))
;; The source ISO is MOUNTED, not copied. Copying it cost 3.4 GB of staging
;; and, on 2026-09-10, the build: the disk filled at the very last step and
;; threw away an ISO that had already been produced. Nothing writes to it, so
;; a read-only mount of its directory is the same input for none of the cost.
(fs/mkdirSync (path/join staging "out") #js {:recursive true})

(println "autoinstall.yaml:")
(println (str/join "\n" (map #(str "  " %) (str/split-lines autoinstall))))
(println (str "storage: " (if target-serial (str "unattended, serial " target-serial)
                              "INTERACTIVE -- a human picks the disk at the box")))
(println (str "ssh: " (if ssh-key (str "key installed for " username) "NONE -- headless unreachable")))
(println (str "console: " (if console-password
                            (str "password set for " username " (console login and sudo work)")
                            "NO PASSWORD -- console login and sudo are both impossible")))

(let [r (.spawnSync cp "docker"
                    (clj->js ["run" "--rm" "--platform" "linux/amd64"
                              "-v" (str staging ":/in")
                              "-v" (str (path/dirname (.resolve path iso)) ":/src:ro")
                              "-v" (str (path/join staging "out") ":/out")
                              "debian:stable-slim" "sh" "-c"
                              (str/replace script "SOURCE_ISO" (path/basename iso))])
                    #js {:encoding "utf8" :maxBuffer 64000000})]
  (print (str (.-stdout r)))
  (when-not (zero? (.-status r))
    (die "repack failed:" (str (.-stderr r)))))

(defn- digest-file
  ;; Chunked on purpose: these are 3.2 GB files and `readFileSync` would put two
  ;; of them in memory at once. This path had never run -- the build died at the
  ;; copy above it -- so the first time it ran would have been the first time
  ;; anyone found out.
  [p]
  (let [h (.createHash crypto "sha256")
        fd (fs/openSync p "r")
        buf (js/Buffer.alloc (* 8 1024 1024))]
    (try
      (loop []
        (let [n (fs/readSync fd buf 0 (.-length buf) nil)]
          (when (pos? n)
            (.update h (.subarray buf 0 n))
            (recur))))
      (finally (fs/closeSync fd)))
    (.digest h "hex")))

(let [result (path/join staging "out" "result.iso")]
  (when-not (fs/existsSync result) (die "the repack produced no ISO"))
  (fs/mkdirSync (path/dirname output) #js {:recursive true})
  ;; MOVE, do not copy. A copy needs the 3.2 GB twice at once and this filled
  ;; the disk -- the ISO was built and then thrown away by its own receipt step.
  ;; Across filesystems rename cannot work, so that case copies and then frees
  ;; the staging half immediately rather than at exit.
  (try
    (fs/renameSync result output)
    (catch :default _
      (fs/copyFileSync result output)
      (fs/rmSync result #js {:force true})))
  (let [bytes (.-size (fs/statSync output))
        digest (digest-file output)]
    (fs/writeFileSync
     (str output ".receipt.json")
     (str (js/JSON.stringify
           (clj->js {:schema "aiueos.node-autoinstall-iso.v1"
                     :disk {:bytes bytes :sha256 digest}
                     :source {:iso (path/basename iso)
                              :sha256 (digest-file iso)}
                     :node {:endpoint endpoint :hostname hostname :username username}
                     :storage (if target-serial {:unattended true :serial target-serial}
                                  {:unattended false :reason "no target named; a human picks the disk"})
                     :ssh {:key-installed (boolean ssh-key)}
                     :console {:password-set (boolean console-password)}})
           nil 2) "\n"))
    (println (str "iso     " output))
    (println (str "bytes   " bytes))
    (println (str "sha256  " digest))))
