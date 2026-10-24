# Native ValueRuntime private context v2

The old v1 domain/status offsets overlap the current sealed allocation and
firmware/PCI metadata. They are not an implementable native layout. The v2
contract reserves separate queue, CAS scratch, capability and domain arena
regions below the PCI profile's 64KiB stack floor. DMA pages and device BAR
ownership continue to come from the existing sealed allocation and catalog.

The native compiler exposes fixed-size image-owned constructors. No caller
address becomes an allocation. Four scheduler domain IDs bind permanently
to distinct 4KiB arenas on the bootstrap CPU. Registration refuses with
interrupts enabled; invalid IDs, absent bindings and capacity exhaustion do
not change the current domain. The scheduler initializes the arena before
publishing its scalar domain ID. There is no eviction or ID reuse.

Provider completion selects the arena of the queued request's domain, even
when another domain is current. Wrong route/domain/digest or an invalid
descriptor leaves the claimed queue entry intact. Queue contention/fullness
and ticket exhaustion refuse. SHA workspace accesses now use the actual
352-byte contract, narrowed explicitly from the caller-owned workspace.

`os/aiueos/native/value-context-v2-probe.kotoba` links the actual domain,
provider, queue, SHA and arena sources. Its twelve in-guest receipts cover
domain isolation, status publication, claim/completion, rejection retention,
queue capacity, locking and ticket exhaustion. The independent QEMU observer
reads the receipt, the private domain map, both arenas and queue state. This
is a CPL0 mechanism test with a bounded fuel budget, not exact fuel parity.

Typed live export admission and its actual guarded caller, owned user pages,
CPL3/SYSCALL, canonical value/token binding and full kernel runtime remain
required before native media or desktop/input can be qualified. A successful
integrity check does not prove canonical decoding or token identity.
