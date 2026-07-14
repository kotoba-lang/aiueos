(ns aiueos.pid1
  "aiueos as `/init` -- PID 1 inside a Linux-kernel-booted initramfs/QEMU VM,
  per ADR-0011 (restoring/replacing the retired Rust `bin/aiueos.rs`'s
  `cmd_init`, ADR-0008). Detects the boot condition (`argv[0] == \"init\"` +
  `/etc/aiueos/boot.edn` present), loads the boot config, boots the system via
  `aiueos.launcher/up-command`, and then -- UNLIKE the old Rust placeholder,
  which just `std::thread::park()`ed forever with no PID-1 responsibilities at
  all -- actually does the things being PID 1 requires:

  - reaps zombies: every process whose parent dies gets reparented to PID 1,
    and PID 1 must `waitpid` them or they pile up as defunct zombies forever
    (there is no init above PID 1 to reap ITS zombies).
  - handles `SIGTERM`/`SIGINT` (`sun.misc.Signal` -- a public, exported-since-
    JDK9 API, no `--add-opens` needed) as a clean-shutdown request instead of
    the JVM's default (which would just die, leaving the kernel with no PID 1
    and typically panicking).
  - powers off / reboots via the real Linux `reboot(2)` syscall
    (`java.lang.foreign`, matching `aiueos.vfio`'s FFM pattern) instead of
    silently returning.

  JVM-only (`#?(:clj ...)` throughout, same reason as `aiueos.execute`/
  `aiueos.vfio`: FFM + file I/O)."
  (:require [clojure.string :as str]
            #?(:clj [clojure.edn :as edn])))

(def boot-edn-path "/etc/aiueos/boot.edn")

(defn pid1-argv0?
  "`argv0`'s basename is exactly `\"init\"` -- the condition the kernel's
  `rdinit=/init`/`init=/init` boot parameter establishes, mirroring the
  retired Rust `main`'s check."
  [argv0]
  (boolean (and argv0 (= "init" (last (str/split argv0 #"/"))))))

#?(:clj
   (defn running-as-pid1?
     "The kernel assigns PID 1 to whatever `/init`/`rdinit=` execs, regardless
     of what that process's own `argv[0]` looks like -- unlike the retired
     Rust binary (which WAS the exec target and so had a meaningful `argv[0]`
     to check), a JVM launched via a wrapper script/symlink named `/init`
     doesn't see `\"init\"` as its own `args`, so `ProcessHandle`'s actual PID
     is the reliable, invocation-shape-independent signal here."
     []
     (= 1 (.pid (java.lang.ProcessHandle/current)))))

#?(:clj
   (defn pid1-mode?
     "True when this process is running as PID 1 (or, for callers that do
     have a meaningful `argv0` to pass -- e.g. a future native-image binary
     where `argv[0]` IS this process's own -- `argv0` names `init`) AND a
     boot config is present at `boot-edn-path`. A process with nothing to
     boot isn't in PID-1 mode even if it happens to be PID 1 (matches the
     retired Rust: an `init`-named binary with no boot config wasn't in PID-1
     mode either)."
     ([] (pid1-mode? nil boot-edn-path))
     ([argv0] (pid1-mode? argv0 boot-edn-path))
     ([argv0 boot-path]
      (boolean (and (or (running-as-pid1?) (pid1-argv0? argv0))
                    (.exists (java.io.File. ^String boot-path)))))))

#?(:clj
   (defn load-boot-config
     "Parse `boot-path` as EDN: `{:aiueos/system <path> :aiueos/policy
     <path-or-nil>}`. Throws ex-info if `:aiueos/system` is missing -- nothing
     to boot is a configuration error, not a silent no-op."
     [boot-path]
     (let [config (edn/read-string (slurp boot-path))]
       (when-not (:aiueos/system config)
         (throw (ex-info (str boot-path ": missing :aiueos/system") {:boot-path boot-path :config config})))
       config)))

;; ---------------------------------------------------------------------------
;; libc bindings (waitpid/reboot) -- same FFM pattern as `aiueos.vfio`; see
;; that namespace's docstring for why `invokeWithArguments` (not
;; `invokeExact`) and why the delay (so requiring this namespace on a non-
;; Linux JVM, e.g. running this repo's tests on macOS, doesn't eagerly fail
;; resolving Linux-only behavior -- `reboot(2)`'s magic numbers are
;; Linux-specific even though the libc symbol itself resolves everywhere).

#?(:clj
   (do

     (import '[java.lang.foreign Arena Linker Linker$Option FunctionDescriptor
               ValueLayout MemoryLayout])

     (def ^:private linker (Linker/nativeLinker))
     (def ^:private lookup (.defaultLookup linker))
     (def ^:private no-linker-options (make-array Linker$Option 0))

     (defn- fdesc [result args] (FunctionDescriptor/of result (into-array MemoryLayout args)))

     (defn- lib-fn
       [name descriptor]
       (delay
        (.downcallHandle linker
                         (.orElseThrow (.find lookup name)
                                       #(ex-info (str "libc symbol not found: " name) {:name name}))
                         descriptor
                         no-linker-options)))

     (defn- invoke-h [handle-delay & args]
       (.invokeWithArguments ^java.lang.invoke.MethodHandle @handle-delay
                             ^"[Ljava.lang.Object;" (into-array Object args)))

     (def ^:private c-waitpid
       (lib-fn "waitpid" (fdesc ValueLayout/JAVA_INT [ValueLayout/JAVA_INT ValueLayout/ADDRESS ValueLayout/JAVA_INT])))
     (def ^:private c-reboot
       (lib-fn "reboot" (fdesc ValueLayout/JAVA_INT [ValueLayout/JAVA_INT])))

     (def wnohang 1)
     ;; linux/reboot.h magic command values (glibc's `reboot(int howto)`
     ;; wrapper takes these directly, not the raw 4-arg syscall's two magic
     ;; numbers -- those are internal to glibc's wrapper).
     (def linux-reboot-cmd-restart 0x01234567)
     (def linux-reboot-cmd-power-off 0x4321fb7)
     (def linux-reboot-cmd-halt 0xcdef0123)

     (defn reap-one-zombie!
       "One `waitpid(-1, &status, WNOHANG)` call. Returns the reaped child pid
       (a positive number), `0` if children exist but none have exited yet,
       or `nil` if this process currently has no children at all (`ECHILD`,
       errno 10 -- NOT an error worth throwing on: a freshly booted system
       with no components running yet has no children, and that is the
       normal steady state between spawns, not a fault)."
       [^Arena arena]
       (let [status-seg (.allocate arena ValueLayout/JAVA_INT)
             rc (long (invoke-h c-waitpid (int -1) status-seg (int wnohang)))]
         (cond
           (>= rc 0) rc
           :else nil)))

     (defn reap-all-zombies!
       "Drain every already-exited child in one pass (loops `reap-one-zombie!`
       until it returns `0` or `nil`). Returns the count reaped."
       [^Arena arena]
       (loop [n 0]
         (let [reaped (reap-one-zombie! arena)]
           (if (and reaped (pos? reaped))
             (recur (inc n))
             n))))

     (defn power-off! [] (invoke-h c-reboot (int linux-reboot-cmd-power-off)))
     (defn restart! [] (invoke-h c-reboot (int linux-reboot-cmd-restart)))
     (defn halt! [] (invoke-h c-reboot (int linux-reboot-cmd-halt)))))

;; ---------------------------------------------------------------------------
;; Signal handling

#?(:clj
   (defn install-shutdown-handler!
     "Install a `SIGTERM`/`SIGINT` handler that delivers to `shutdown-promise`
     (a fresh `(promise)` the caller owns) instead of the JVM's default
     behavior. Returns nil; call once at startup."
     [shutdown-promise]
     (let [handler (reify sun.misc.SignalHandler
                     (handle [_ _sig] (deliver shutdown-promise true)))]
       (sun.misc.Signal/handle (sun.misc.Signal. "TERM") handler)
       (sun.misc.Signal/handle (sun.misc.Signal. "INT") handler))
     nil))

;; ---------------------------------------------------------------------------
;; The PID-1 main loop

#?(:clj
   (defn boot!
     "The PID-1 body: boot the system named in `boot-config`'s `:aiueos/system`
     via `up-fn` (pass `aiueos.launcher/up-command` in production; a fake in
     tests), install the shutdown signal handler, then loop reaping zombies
     (every `reap-interval-ms`) until a shutdown signal arrives, at which
     point call `poweroff-fn` (`power-off!` in production) -- this function
     never returns in production (power-off ends the VM); it returns the
     shutdown reason keyword in tests where `poweroff-fn` is a fake that
     doesn't actually halt anything.

     `reap-interval-ms` defaults to 1000; a tender idling at 1Hz between
     zombie-reap passes is cheap and matches how a real init typically
     polls (Linux doesn't require SIGCHLD-driven reaping to avoid zombies
     piling up briefly, only to eventually reap them)."
     ([boot-config up-fn poweroff-fn arena]
      (boot! boot-config up-fn poweroff-fn arena 1000))
     ([boot-config up-fn poweroff-fn arena reap-interval-ms]
      (up-fn (:aiueos/system boot-config) (:aiueos/policy boot-config))
      (let [shutdown (promise)]
        (install-shutdown-handler! shutdown)
        (loop []
          (reap-all-zombies! arena)
          (if (realized? shutdown)
            (poweroff-fn)
            (do (Thread/sleep ^long reap-interval-ms)
                (recur))))))))
