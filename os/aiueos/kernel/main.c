#include <stdint.h>

struct aiueos_boot_info {
  uint64_t magic, version;
  void *memory_map; uint64_t memory_map_size, descriptor_size, descriptor_version;
  void *acpi_rsdp;
  uint64_t framebuffer_base, framebuffer_size;
  uint32_t framebuffer_width, framebuffer_height, framebuffer_stride, framebuffer_format;
  uint64_t initramfs_base, initramfs_size;
};

/* Bounded `newc` cpio validation. The archive was already bound to a
   compiled-in SHA-256 by the loader; this walk proves the structure: magic
   per entry, hex-only size fields, 4-byte alignment, in-bounds extents, a
   bounded entry count, and the TRAILER!!! terminator. Runs before the kernel
   replaces the firmware page tables, while the loader-pool buffer is still
   identity-mapped. */
static int initramfs_hex_field(const uint8_t *field, uint64_t *value) {
  uint64_t result = 0;
  for (uint32_t i = 0; i < 8; i++) {
    uint8_t c = field[i]; uint64_t digit;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else return 0;
    result = (result << 4) | digit;
  }
  *value = result;
  return 1;
}

/* Recovery materials copied out of the archive while the loader-pool buffer
   is still identity-mapped, sized by the same bound as catalog applications. */
static uint8_t initramfs_recovery_elf[12288];
static uint8_t initramfs_recovery_signature[256];
static uint64_t initramfs_recovery_elf_length;
static int initramfs_recovery_signature_present;

const uint8_t *aiueos_initramfs_recovery_elf(uint64_t *length) {
  if (length) *length = initramfs_recovery_elf_length;
  return initramfs_recovery_elf_length ? initramfs_recovery_elf : 0;
}
const uint8_t *aiueos_initramfs_recovery_signature(void) {
  return initramfs_recovery_signature_present ? initramfs_recovery_signature : 0;
}

static int initramfs_name_is(const uint8_t *name, uint64_t namesize, const char *wanted) {
  uint64_t length = 0;
  while (wanted[length]) length++;
  if (namesize != length + 1) return 0;
  for (uint64_t i = 0; i < length; i++)
    if (name[i] != (uint8_t)wanted[i]) return 0;
  return name[length] == 0;
}

static int initramfs_validate(uint64_t base, uint64_t size, uint64_t *files) {
  static const char trailer[11] = "TRAILER!!!";
  const uint8_t *archive = (const uint8_t *)(uintptr_t)base;
  uint64_t offset = 0, count = 0;
  if (!base || !size || size > 1024ULL * 1024ULL || (base & 3)) return 0;
  for (;;) {
    if (count > 64 || offset + 110 > size) return 0;
    const uint8_t *header = archive + offset;
    if (header[0] != '0' || header[1] != '7' || header[2] != '0' ||
        header[3] != '7' || header[4] != '0' || header[5] != '1') return 0;
    uint64_t filesize, namesize;
    if (!initramfs_hex_field(header + 54, &filesize) ||
        !initramfs_hex_field(header + 94, &namesize)) return 0;
    if (!namesize || namesize > 256 || filesize > size) return 0;
    uint64_t name_offset = offset + 110;
    if (name_offset + namesize > size) return 0;
    if (namesize == sizeof(trailer)) {
      int is_trailer = 1;
      for (uint32_t i = 0; i < sizeof(trailer); i++)
        if (archive[name_offset + i] != (uint8_t)trailer[i]) { is_trailer = 0; break; }
      if (is_trailer) { *files = count; return 1; }
    }
    uint64_t data_offset = (name_offset + namesize + 3) & ~3ULL;
    if (data_offset > size || filesize > size - data_offset) return 0;
    if (initramfs_name_is(archive + name_offset, namesize, "recovery/user-smoke.elf") &&
        filesize && filesize <= sizeof(initramfs_recovery_elf)) {
      for (uint64_t i = 0; i < filesize; i++)
        initramfs_recovery_elf[i] = archive[data_offset + i];
      initramfs_recovery_elf_length = filesize;
    }
    if (initramfs_name_is(archive + name_offset, namesize, "recovery/user-smoke.sig") &&
        filesize == sizeof(initramfs_recovery_signature)) {
      for (uint64_t i = 0; i < filesize; i++)
        initramfs_recovery_signature[i] = archive[data_offset + i];
      initramfs_recovery_signature_present = 1;
    }
    offset = (data_offset + filesize + 3) & ~3ULL;
    count++;
  }
}

struct __attribute__((packed)) idt_entry {
  uint16_t offset_low, selector;
  uint8_t ist, attributes;
  uint16_t offset_middle;
  uint32_t offset_high, reserved;
};
struct __attribute__((packed)) descriptor_pointer {
  uint16_t limit;
  uint64_t base;
};

extern void aiueos_load_gdt(void);
extern void aiueos_load_idt(const struct descriptor_pointer *pointer);
extern void aiueos_isr_invalid_opcode(void);
extern void aiueos_isr_page_fault(void);
extern void aiueos_isr_apic_timer(void);
extern void aiueos_isr_external_timer(void);
extern void aiueos_isr_virtio_rng(void);
extern void aiueos_isr_virtio_blk(void);
extern void aiueos_isr_syscall(void);
extern void aiueos_probe_write_protect(void);
extern void aiueos_probe_no_execute(void);
volatile uint64_t aiueos_page_fault_stage;
volatile uint64_t aiueos_page_fault_error;
extern int aiueos_paging_initialize(void);
extern int aiueos_framebuffer_initialize(const struct aiueos_boot_info *boot);
extern int aiueos_desktop_surface_ready(void);
extern int aiueos_desktop_surface_bind_scanout(uint32_t width, uint32_t height);
extern int aiueos_acpi_initialize(const void *rsdp);
extern int aiueos_dma_test_policy_allows_unisolated(void);
extern int aiueos_vtd_initialize(void);
extern int aiueos_vtd_translation_enabled(void);
extern uint32_t aiueos_vtd_error(void);
extern int aiueos_vtd_interrupt_remapping_enabled(void);
extern int aiueos_apic_timer_initialize(void);
extern volatile uint64_t aiueos_apic_timer_ticks;
extern int aiueos_physical_allocator_initialize(const struct aiueos_boot_info *boot);
extern void *aiueos_allocate_physical_page(void);
extern int aiueos_pci_enumerate(void);
extern int aiueos_catalog_policy_selftest_ok(void);
extern int aiueos_object_store_ready(void);
extern int aiueos_journal_ready(void);
extern int aiueos_journal_recovered(void);
extern uint32_t aiueos_journal_sequence(void);
extern uint32_t aiueos_journal_recovered_sequence(void);
extern uint32_t aiueos_journal_slot(void);
extern int aiueos_object_transaction_replayed(void);
extern uint32_t aiueos_object_transaction_sequence(void);
extern int aiueos_service_registry_ready(void);
extern int aiueos_service_registry_replayed(void);
extern int aiueos_recovered_service_registry_ready(void);
extern uint64_t aiueos_recovered_service_registry_state(unsigned service);
extern int aiueos_user_object_replay_evidence_ready(void);
extern int aiueos_virtio_net_ready(void);
extern int aiueos_ipv4_ready(void);
extern int aiueos_tcp_ready(void);
extern unsigned aiueos_tcp_stage(void);
extern int aiueos_dhcp_ready(void);
extern unsigned aiueos_dhcp_stage(void);
extern unsigned aiueos_dhcp_reason(void);
extern uint32_t aiueos_dhcp_address(void);
extern uint32_t aiueos_dhcp_mask(void);
extern uint32_t aiueos_dhcp_router(void);
extern uint32_t aiueos_dhcp_server(void);
extern uint32_t aiueos_dhcp_lease_seconds(void);
extern uint32_t aiueos_dhcp_dns(void);
extern int aiueos_dhcp_consumed(void);
extern int aiueos_dns_ready(void);
extern unsigned aiueos_dns_stage(void);
extern uint32_t aiueos_dns_a(void);
extern int aiueos_tcp_cloud_ready(void);
extern unsigned aiueos_tcp_cloud_stage(void);
extern int aiueos_tls_record_ready(void);
extern uint8_t aiueos_tls_record_type(void);
extern int aiueos_tls_handshake_ready(void);
extern int aiueos_http_cid_ready(void);
extern const char *aiueos_http_cid(void);
extern uint32_t aiueos_tls_stage(void);
extern uint32_t aiueos_tls_rx_buffered(void);
extern uint32_t aiueos_tls_app_len(void);
extern uint8_t aiueos_tls_last_record_type(void);
extern uint8_t aiueos_tls_last_inner_type(void);
extern int aiueos_tls_failed(void);
extern int aiueos_tls_finished_sent(void);
extern int aiueos_tls_http_sent(void);
extern uint32_t aiueos_tls_nst_count(void);
extern uint32_t aiueos_gpu_scanout_width(void);
extern uint32_t aiueos_gpu_scanout_height(void);
extern int aiueos_gpu_2d_create_ok(void);
extern int aiueos_gpu_2d_flush_ok(void);
extern void aiueos_scheduler_initialize(void);
extern int aiueos_scheduler_restore_service_registry(uint64_t state0, uint64_t state1);
extern int aiueos_scheduler_persistent_restore_evidence_ready(void);
extern int aiueos_scheduler_evidence_ready(void);
extern int aiueos_service_runtime_evidence_ready(void);
extern int aiueos_service_ipc_evidence_ready(void);
extern int aiueos_syscall_self_test(void);
extern int aiueos_capability_table_initialize(void);
extern uint16_t aiueos_capability_table_capacity(void);
extern int aiueos_dynamic_capability_evidence_ready(void);
extern int aiueos_capability_derivation_evidence_ready(void);
extern int aiueos_process_initialize(void);
extern void aiueos_process_enter(void);
extern int aiueos_process_result(void);
extern int aiueos_process_lifecycle_evidence_ready(void);
extern int aiueos_kotoba_service_ipc_evidence_ready(void);
extern int aiueos_catalog_lookup_rejection_evidence_ready(void);
extern int aiueos_syscall_transport_evidence_ready(void);
extern int aiueos_kotoba_runtime_evidence_ready(void);
extern int aiueos_address_space_self_test(void);
extern void aiueos_load_task_register(void);
extern int aiueos_smp_start_application_processor(void);
extern int aiueos_ioapic_route_legacy_timer(void);
extern volatile uint64_t aiueos_external_timer_ticks;
extern volatile uint64_t aiueos_virtio_rng_irq_count;
extern volatile uint64_t aiueos_virtio_blk_irq_count;
static struct idt_entry idt[256] __attribute__((aligned(16)));

static inline void debug_byte(uint8_t value) {
  __asm__ volatile("outb %0, $0xe9" : : "a"(value));
}
static inline void out8(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t in8(uint16_t port) {
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}
static void serial_init(void) {
  out8(0x3f8 + 1, 0x00);
  out8(0x3f8 + 3, 0x80);
  out8(0x3f8 + 0, 0x01);
  out8(0x3f8 + 1, 0x00);
  out8(0x3f8 + 3, 0x03);
  out8(0x3f8 + 2, 0xc7);
  out8(0x3f8 + 4, 0x0b);
}
static void serial_byte(uint8_t value) {
  uint32_t budget = 1000000;
  while (!(in8(0x3f8 + 5) & 0x20) && --budget) {}
  if (budget) out8(0x3f8, value);
}
static void serial_string(const char *text) {
  while (*text) serial_byte((uint8_t)*text++);
}
static void debug_string(const char *text) {
  while (*text) debug_byte((uint8_t)*text++);
}

/* The first numbers this kernel prints. Every marker before DHCP is a fixed
   string, because everything they report is either true or false; a lease is
   neither, and a marker that said "an address was configured" without saying
   which one would be unfalsifiable. Formatting is mechanism -- these decide
   nothing, and the values they render were decided by a Kotoba object. */
static void serial_decimal(uint32_t value) {
  char digits[10];
  unsigned count = 0;
  if (!value) { serial_byte('0'); return; }
  while (value) { digits[count++] = (char)('0' + (value % 10)); value /= 10; }
  while (count) serial_byte((uint8_t)digits[--count]);
}
static void serial_ipv4(uint32_t value) {
  for (unsigned shift = 24; ; shift -= 8) {
    serial_decimal((value >> shift) & 0xff);
    if (!shift) return;
    serial_byte('.');
  }
}
/* The object's reason code rendered as the clause it names. The mapping mirrors
   the table at the top of kotoba/dhcp-reply-valid.kotoba, which is where the
   codes are decided; this is rendering, and getting it wrong misnames a
   refusal without changing one. */
static const char *dhcp_reason_name(unsigned reason) {
  switch (reason) {
    case 0: return "admitted";
    case 1: return "frame-length";
    case 2: return "ipv4-envelope";
    case 3: return "udp-envelope";
    case 4: return "not-bootreply";
    case 5: return "foreign-transaction-id";
    case 6: return "foreign-hardware-address";
    case 7: return "no-magic-cookie";
    case 8: return "options-overrun";
    case 9: return "message-type";
    case 10: return "no-server-identifier";
    case 11: return "address-mask-inconsistent";
    case 12: return "lease-time";
    default: return "unknown";
  }
}
static const char *dhcp_stage_name(unsigned stage) {
  switch (stage) {
    case 1: return "tx-discover";
    case 2: return "no-admitted-offer";
    case 3: return "tx-request";
    case 4: return "no-admitted-ack";
    case 5: return "done";
    default: return "idle";
  }
}
__attribute__((noreturn)) static void qemu_exit(uint32_t value) {
  __asm__ volatile("outl %0, $0xf4" : : "a"(value));
  __asm__ volatile("cli");
  for (;;) __asm__ volatile("hlt");
}

/* Splitting the 64-bit handler address across the descriptor's three offset
 * fields is bit-packing whose failure mode is a well-formed gate pointing at
 * the WRONG ring-0 address -- silent and exploitable rather than a crash -- so
 * it lives in Kotoba. This wrapper keeps the C shape (vector, handler); the
 * object owns the sixteen bytes and the selector/ist/attributes domains. The
 * three constants below are passed rather than assumed: the object refuses
 * anything but this kernel's single DPL-0 64-bit code selector, no IST (the
 * TSS has none populated), and a present DPL-0 interrupt gate. */
extern uint64_t kotoba_aiueos_idt_gate_build(uint64_t handler, uint64_t selector,
                                             uint64_t ist, uint64_t attributes,
                                             void *out);

static void set_idt_gate(uint8_t vector, void (*handler)(void)) {
  /* A refusal writes nothing, so the entry would stay P=0 and every delivery
   * on that vector would #GP with the fault pointing at the IDT. Fail here
   * instead, where the vector is still known. */
  if (!kotoba_aiueos_idt_gate_build((uint64_t)(uintptr_t)handler, 0x08, 0, 0x8e,
                                    &idt[vector])) {
    debug_string("AIUEOS_KOTOBA_IDT_GATE_FAIL refused\n");
    serial_string("AIUEOS_KOTOBA_IDT_GATE_FAIL refused\r\n");
    qemu_exit(0x69);
  }
}

/* Set immediately before the deliberate end-of-boot ud2 probe. Any exception
   arriving before then is an unexpected fault and receives a best-effort
   durable crash receipt over the polled transport before termination. */
volatile int aiueos_final_probe_expected;

__attribute__((noreturn))
void aiueos_exception_dispatch(uint64_t vector) {
  if (vector == 6 && aiueos_final_probe_expected) {
    debug_string("AIUEOS_EXCEPTION_OK vector=6\n");
    serial_string("AIUEOS_EXCEPTION_OK vector=6 invalid-opcode\r\n");
    qemu_exit(0x30);
  } else {
    extern int aiueos_crash_receipt_write_from_fault(uint32_t);
    debug_string("AIUEOS_EXCEPTION_FAIL unexpected-vector\n");
    serial_string("AIUEOS_EXCEPTION_FAIL unexpected-vector vector=");
    serial_decimal((uint32_t)vector);
    serial_string("\r\n");
    if (aiueos_crash_receipt_write_from_fault((uint32_t)vector)) {
      debug_string("AIUEOS_FAULT_RECEIPT_OK polled try-lock written readback pending\n");
      serial_string("AIUEOS_FAULT_RECEIPT_OK polled try-lock written readback pending\r\n");
    } else {
      /* Best effort: the storage plane may not be up yet, or the queue may
         be held mid-operation by the faulted context. */
      serial_string("AIUEOS_FAULT_RECEIPT_SKIPPED storage-unavailable-or-busy\r\n");
    }
    qemu_exit(0x7d);
  }
  for (;;) __asm__ volatile("cli; hlt");
}

__attribute__((noreturn))
void aiueos_kernel_main(const struct aiueos_boot_info *boot) {
  serial_init();
  if (!boot || boot->magic != 0x414955454f53424fULL || boot->version != 2 ||
      !boot->memory_map || !boot->memory_map_size || !boot->descriptor_size) {
    debug_string("AIUEOS_KERNEL_FAIL boot-info\n");
    serial_string("AIUEOS_KERNEL_FAIL boot-info\r\n");
    qemu_exit(0x7e);
  } else {
    debug_string("AIUEOS_KERNEL_OK memory-map-v1\n");
    serial_string("AIUEOS_SERIAL_OK stack-v1 memory-map-v1\r\n");
    extern uint64_t kotoba_aiueos_probe(void);
    if (kotoba_aiueos_probe() != 42u) {
      debug_string("AIUEOS_KOTOBA_NATIVE_FAIL probe-result\n");
      serial_string("AIUEOS_KOTOBA_NATIVE_FAIL probe-result\r\n");
      qemu_exit(0x67);
    }
    debug_string("AIUEOS_KOTOBA_NATIVE_OK elf64-relocatable sysv-v1 result=42\n");
    serial_string("AIUEOS_KOTOBA_NATIVE_OK elf64-relocatable sysv-v1 result=42\r\n");
    extern uint64_t kotoba_aiueos_fnv1a(const uint8_t *, uint64_t);
    static const uint8_t fnv_vector[3] = {'a', 'b', 'c'};
    if ((uint32_t)kotoba_aiueos_fnv1a(fnv_vector, 3) != 0x1a47e90bU) {
      serial_string("AIUEOS_KOTOBA_FNV_FAIL known-vector\r\n");
      qemu_exit(0x6f);
    }
    serial_string("AIUEOS_KOTOBA_FNV_VECTOR_OK abc\r\n");
    uint64_t initramfs_files = 0;
    if (!initramfs_validate(boot->initramfs_base, boot->initramfs_size,
                            &initramfs_files) || initramfs_files != 3) {
      debug_string("AIUEOS_INITRAMFS_FAIL newc-structure\n");
      serial_string("AIUEOS_INITRAMFS_FAIL newc-structure\r\n");
      qemu_exit(0x68);
    }
    debug_string("AIUEOS_INITRAMFS_OK newc entries=3 sha256-admitted bounded\n");
    serial_string("AIUEOS_INITRAMFS_OK newc entries=3 sha256-admitted bounded\r\n");
    extern int aiueos_recovery_payload_admission(const uint8_t *, uint64_t, const uint8_t *);
    if (!initramfs_recovery_elf_length || !initramfs_recovery_signature_present ||
        !aiueos_recovery_payload_admission(initramfs_recovery_elf,
                                           initramfs_recovery_elf_length,
                                           initramfs_recovery_signature)) {
      debug_string("AIUEOS_INITRAMFS_RECOVERY_ADMISSION_FAIL rsa2048-policy\n");
      serial_string("AIUEOS_INITRAMFS_RECOVERY_ADMISSION_FAIL rsa2048-policy\r\n");
      qemu_exit(0x68);
    }
    debug_string("AIUEOS_INITRAMFS_RECOVERY_ADMISSION_OK elf digest=kotoba-sha256 signature=kotoba-rsa2048-pkcs1 policy=public-key\n");
    serial_string("AIUEOS_INITRAMFS_RECOVERY_ADMISSION_OK elf digest=kotoba-sha256 signature=kotoba-rsa2048-pkcs1 policy=public-key\r\n");
    extern uint64_t kotoba_aiueos_journal_record_build(void *, uint64_t, uint64_t);
    extern uint64_t kotoba_aiueos_journal_record_valid(const void *, uint64_t);
    static uint8_t journal_vector[512];
    for (uint32_t i = 0; i < sizeof(journal_vector); i++) journal_vector[i] = 0;
    if (!kotoba_aiueos_journal_record_build(journal_vector, sizeof(journal_vector), 1) ||
        !kotoba_aiueos_journal_record_valid(journal_vector, 64)) {
      serial_string("AIUEOS_KOTOBA_STORE_FAIL journal-vector\r\n");
      qemu_exit(0x6f);
    }
    serial_string("AIUEOS_KOTOBA_STORE_VECTOR_OK journal-sequence=1\r\n");
    aiueos_load_gdt();
    set_idt_gate(6, aiueos_isr_invalid_opcode);
    set_idt_gate(14, aiueos_isr_page_fault);
    set_idt_gate(32, aiueos_isr_apic_timer);
    set_idt_gate(33, aiueos_isr_external_timer);
    set_idt_gate(34, aiueos_isr_virtio_rng);
    set_idt_gate(35, aiueos_isr_virtio_blk);
    set_idt_gate(128, aiueos_isr_syscall);
    const struct descriptor_pointer idtr = {
      .limit = (uint16_t)(sizeof(idt) - 1),
      .base = (uint64_t)(uintptr_t)idt
    };
    aiueos_load_idt(&idtr);
    debug_string("AIUEOS_DESCRIPTOR_TABLES_OK gdt-v1 idt-v1\n");
    serial_string("AIUEOS_DESCRIPTOR_TABLES_OK gdt-v1 idt-v1\r\n");
    /* Quiet the legacy 8259 pair. ADR-0028 diagnosed this as a ~22% flaky boot
       on the Multiboot path -- SeaBIOS leaves the PIC live with master base
       0x08 and the 8254 ticking, so the first `sti` delivered IRQ0 as vector 8
       and it masqueraded as #DF -- and fixed it only there. This path has the
       same gap. Nothing here has ever programmed the 8259; the only masking on
       the whole path is the out8(0x21,0xff)/out8(0xa1,0xff) pair inside
       aiueos_ioapic_route_legacy_timer (ioapic.c:31), which does not run until
       main.c:692, well past the first `sti` at main.c:456, and which masks
       without remapping. This path is green because OVMF masks the PIC before
       handoff -- that is a property of the firmware, not of this kernel.

       Placed HERE, immediately after aiueos_load_idt, for the same reason the
       X25519 self-test below is placed after the kernel owns its IDT rather
       than beside the earlier self-tests: this is a Kotoba object, its prologue
       guards fuel with `ud2`, and a #UD raised while the FIRMWARE's handler is
       still installed produces an OVMF dump with no vector and no address.
       After the `lidt` it reaches set_idt_gate(6, aiueos_isr_invalid_opcode)
       and names itself. It is still 115 lines and one paging bring-up ahead of
       the first `sti`, which is the only ordering the PIC actually requires.

       0xe0/0xe8 rather than the multiboot C's old 0xf0/0xf8: the object refuses
       0xf8 because 0xf8+7 is 255, the Local APIC spurious vector this kernel
       programs in apic.c:42. 224..239 is clear of 32-35, 128 and 255. */
    extern uint64_t kotoba_aiueos_pic_disable(uint64_t, uint64_t);
    if (!kotoba_aiueos_pic_disable(0xe0, 0xe8)) {
      debug_string("AIUEOS_PIC_FAIL base-refused\n");
      serial_string("AIUEOS_PIC_FAIL base-refused\r\n");
      qemu_exit(0x7b);
    }
    debug_string("AIUEOS_PIC_OK remapped=0xe0/0xe8 masked=both\n");
    serial_string("AIUEOS_PIC_OK remapped=0xe0/0xe8 masked=both\r\n");
    if (!aiueos_paging_initialize()) {
      debug_string("AIUEOS_PAGING_FAIL ownership-or-wx\n");
      serial_string("AIUEOS_PAGING_FAIL ownership-or-wx\r\n");
      qemu_exit(0x7c);
    }
    debug_string("AIUEOS_PAGING_OK cr3-owned wx-v1 nx-wp\n");
    serial_string("AIUEOS_PAGING_OK cr3-owned wx-v1 nx-wp\r\n");
    /* Placed AFTER the kernel owns its own IDT and page tables, not beside the
       other known-vector self-tests earlier in boot. Measured: running it there
       faulted with the FIRMWARE's handler still installed, so the only output
       was OVMF's "Can't find image information" dump -- no vector, no address,
       nothing to act on. Here a fault reaches this kernel's own handler and
       reports its vector, which is the difference between a diagnosable failure
       and an opaque one.
       X25519 against RFC 7748 §5.2's base-point vector, run here rather than
       trusted from a transcription. The algorithm was checked by transcribing
       the Kotoba into Python and running the published vectors; that proves the
       ALGORITHM and says nothing about what the compiler emitted. This runs the
       emitted object on the target and compares all 32 bytes, which is the only
       evidence that covers the backend too.
       The comparison reuses kotoba_aiueos_digest_equal -- the same fixed-work
       32-byte compare the application catalog admits signatures with. */
    {
      extern uint64_t kotoba_aiueos_x25519(const uint8_t *, const uint8_t *,
                                           uint8_t *, uint8_t *);
      extern uint64_t kotoba_aiueos_digest_equal(const uint8_t *, const uint8_t *,
                                                 uint64_t);
      static const uint8_t x_scalar[32] = {
        0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
        0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a};
      static const uint8_t x_base[32] = {9};
      static const uint8_t x_expected[32] = {
        0x85,0x20,0xf0,0x09,0x89,0x30,0xa7,0x54,0x74,0x8b,0x7d,0xdc,0xb4,0x3e,0xf7,0x5a,
        0x0d,0xbf,0x3a,0x0d,0x26,0x38,0x1a,0xf4,0xeb,0xa4,0xa9,0x8e,0xaa,0x9b,0x4e,0x6a};
      static uint8_t x_output[32];
      static uint8_t x_workspace[646];
      if (!kotoba_aiueos_x25519(x_scalar, x_base, x_output, x_workspace) ||
          !kotoba_aiueos_digest_equal(x_output, x_expected, 32)) {
        serial_string("AIUEOS_X25519_FAIL rfc7748-base-point\r\n");
        qemu_exit(0x6f);
      }
      debug_string("AIUEOS_X25519_OK rfc7748-base-point 32-bytes\n");
      serial_string("AIUEOS_X25519_OK rfc7748-base-point 32-bytes\r\n");
    }
    {
      extern int aiueos_tls13_aes_selftest(void);
      if (!aiueos_tls13_aes_selftest()) {
        serial_string("AIUEOS_AES_GCM_FAIL nist-vectors\r\n");
        qemu_exit(0x6f);
      }
      serial_string("AIUEOS_AES_GCM_OK aes-128-gcm nist\r\n");
    }
    {
      extern int aiueos_tls13_hmac_selftest(void);
      if (!aiueos_tls13_hmac_selftest()) {
        serial_string("AIUEOS_HMAC_HKDF_FAIL rfc4231-rfc5869\r\n");
        qemu_exit(0x6f);
      }
      serial_string("AIUEOS_HMAC_HKDF_OK sha256 rfc4231-rfc5869\r\n");
    }
    {
      extern int aiueos_tls13_ecdsa_selftest(void);
      if (!aiueos_tls13_ecdsa_selftest()) {
        serial_string("AIUEOS_ECDSA_P256_FAIL rfc6979-sample\r\n");
        qemu_exit(0x6f);
      }
      serial_string("AIUEOS_ECDSA_P256_OK rfc6979-sample s+1-refused\r\n");
    }
    if (!aiueos_framebuffer_initialize(boot)) {
      debug_string("AIUEOS_FRAMEBUFFER_FAIL gop-contract\n");
      serial_string("AIUEOS_FRAMEBUFFER_FAIL gop-contract\r\n");
      qemu_exit(0x68);
    }
    debug_string("AIUEOS_FRAMEBUFFER_OK gop-owned retained-rectangles hash-verified\n");
    serial_string("AIUEOS_FRAMEBUFFER_OK gop-owned retained-rectangles hash-verified\r\n");
    if (!aiueos_desktop_surface_ready()) qemu_exit(0x68);
    debug_string("AIUEOS_DESKTOP_SURFACE_OK envelope-v1 opaque-handle full-damage hash-verified\n");
    serial_string("AIUEOS_DESKTOP_SURFACE_OK envelope-v1 opaque-handle full-damage hash-verified\r\n");
    if (!aiueos_physical_allocator_initialize(boot)) {
      debug_string("AIUEOS_PHYSICAL_ALLOCATOR_FAIL memory-map\n");
      serial_string("AIUEOS_PHYSICAL_ALLOCATOR_FAIL memory-map\r\n");
      qemu_exit(0x76);
    }
    void *physical_page_a = aiueos_allocate_physical_page();
    void *physical_page_b = aiueos_allocate_physical_page();
    if (!physical_page_a || !physical_page_b || physical_page_a == physical_page_b ||
        ((uintptr_t)physical_page_a & 4095) || ((uintptr_t)physical_page_b & 4095) ||
        *(const uint64_t *)physical_page_a || *(const uint64_t *)physical_page_b) {
      debug_string("AIUEOS_PHYSICAL_ALLOCATOR_FAIL allocation\n");
      serial_string("AIUEOS_PHYSICAL_ALLOCATOR_FAIL allocation\r\n");
      qemu_exit(0x75);
    }
    debug_string("AIUEOS_PHYSICAL_ALLOCATOR_OK pages=2 zeroed\n");
    serial_string("AIUEOS_PHYSICAL_ALLOCATOR_OK pages=2 zeroed\r\n");
    if (!aiueos_capability_table_initialize()) {
      serial_string("AIUEOS_CAPABILITY_TABLE_FAIL page-allocation\r\n");
      qemu_exit(0x75);
    }
    if (!aiueos_acpi_initialize(boot->acpi_rsdp)) {
      debug_string("AIUEOS_ACPI_FAIL rsdp-xsdt-madt\n");
      serial_string("AIUEOS_ACPI_FAIL rsdp-xsdt-madt\r\n");
      qemu_exit(0x78);
    }
    debug_string("AIUEOS_ACPI_OK rsdp-xsdt-madt cpu>=2\n");
    serial_string("AIUEOS_ACPI_OK rsdp-xsdt-madt cpu>=2\r\n");
    if (!aiueos_vtd_initialize()) {
      if (aiueos_vtd_error() == 3) serial_string("AIUEOS_VTD_STAGE_FAIL srtp\r\n");
      if (aiueos_vtd_error() == 4) serial_string("AIUEOS_VTD_STAGE_FAIL context-invalidate\r\n");
      if (aiueos_vtd_error() == 6) serial_string("AIUEOS_VTD_STAGE_FAIL iotlb-invalidate\r\n");
      if (aiueos_vtd_error() == 7) serial_string("AIUEOS_VTD_STAGE_FAIL translation-enable\r\n");
      if (aiueos_vtd_error() == 8) serial_string("AIUEOS_VTD_STAGE_FAIL interrupt-table-pointer\r\n");
      if (aiueos_vtd_error() == 9) serial_string("AIUEOS_VTD_STAGE_FAIL interrupt-remapping-enable\r\n");
      serial_string("AIUEOS_VTD_FAIL root-context-slpt\r\n");
      qemu_exit(0x74);
    }
    if (aiueos_vtd_translation_enabled()) {
      serial_string("AIUEOS_VTD_OK tes=1 root-context-slpt domain=1 aperture=128MiB\r\n");
      serial_string("AIUEOS_DMA_POLICY_OK dmar=validated dma=vtd-isolated\r\n");
    } else if (!aiueos_dma_test_policy_allows_unisolated()) {
      serial_string("AIUEOS_DMA_POLICY_OK dmar=validated dma=denied-until-vtd-enabled\r\n");
    } else {
      serial_string("AIUEOS_DMA_POLICY_OK dmar=absent test-only-unisolated\r\n");
    }
    if (!aiueos_apic_timer_initialize()) {
      debug_string("AIUEOS_APIC_FAIL initialization\n");
      serial_string("AIUEOS_APIC_FAIL initialization\r\n");
      qemu_exit(0x77);
    }
    if (!aiueos_smp_start_application_processor()) {
      debug_string("AIUEOS_SMP_FAIL ap-startup\n");
      serial_string("AIUEOS_SMP_FAIL ap-startup\r\n");
      qemu_exit(0x73);
    }
    debug_string("AIUEOS_SMP_OK cpus=2 init-sipi-v1\n");
    serial_string("AIUEOS_SMP_OK cpus=2 init-sipi-v1 per-cpu-stack\r\n");
    aiueos_scheduler_initialize();
    __asm__ volatile("sti");
    while (!aiueos_scheduler_evidence_ready()) __asm__ volatile("hlt");
    __asm__ volatile("cli");
    debug_string("AIUEOS_APIC_TIMER_OK vector=32 eoi-v1\n");
    serial_string("AIUEOS_APIC_TIMER_OK vector=32 eoi-v1\r\n");
    int pci_result = aiueos_pci_enumerate();
    if (!pci_result) {
      debug_string("AIUEOS_PCI_FAIL enumeration-or-virtio\n");
      serial_string("AIUEOS_PCI_FAIL enumeration-or-virtio\r\n");
      qemu_exit(0x74);
    }
    debug_string("AIUEOS_PCI_OK bounded-scan virtio-vendor=1af4\n");
    serial_string("AIUEOS_PCI_OK bounded-scan virtio-vendor=1af4\r\n");
    if (pci_result < 2) {
      debug_string("AIUEOS_VIRTIO_FAIL rng-queue\n");
      serial_string("AIUEOS_VIRTIO_FAIL rng-queue\r\n");
      qemu_exit(0x73);
    }
    debug_string("AIUEOS_VIRTIO_RNG_OK modern-pci caps-bounded dma=4pages completion=32\n");
    serial_string("AIUEOS_VIRTIO_RNG_OK modern-pci caps-bounded dma=4pages completion=32\r\n");
    if (aiueos_virtio_rng_irq_count != 1) {
      serial_string("AIUEOS_VIRTIO_RNG_MSIX_FAIL irq-count\r\n"); qemu_exit(0x6f);
    }
    debug_string("AIUEOS_VIRTIO_RNG_MSIX_OK vector=34 irq=1 table-pba-bounded\n");
    serial_string("AIUEOS_VIRTIO_RNG_MSIX_OK vector=34 irq=1 table-pba-bounded\r\n");
    if ((pci_result & 3) != 3) {
      debug_string("AIUEOS_VIRTIO_BLK_FAIL capacity-or-read\n");
      serial_string("AIUEOS_VIRTIO_BLK_FAIL capacity-or-read\r\n");
      qemu_exit(0x71);
    }
    debug_string("AIUEOS_VIRTIO_BLK_OK capacity-bounded sector=0 bytes=512 readonly\n");
    serial_string("AIUEOS_VIRTIO_BLK_OK capacity-bounded sector=0 bytes=512 readonly\r\n");
    if (aiueos_virtio_blk_irq_count < 5) {
      serial_string("AIUEOS_VIRTIO_BLK_MSIX_FAIL irq-count\r\n"); qemu_exit(0x6f);
    }
    debug_string("AIUEOS_VIRTIO_BLK_MSIX_OK vector=35 irq-completions-bounded table-pba-bounded\n");
    serial_string("AIUEOS_VIRTIO_BLK_MSIX_OK vector=35 irq-completions-bounded table-pba-bounded\r\n");
    if (aiueos_vtd_translation_enabled()) {
      if (!aiueos_vtd_interrupt_remapping_enabled()) qemu_exit(0x6f);
      serial_string("AIUEOS_VTD_IR_OK irta=256 source-validated vector=35 remappable-msix\r\n");
    }
    if (!aiueos_object_store_ready()) {
      debug_string("AIUEOS_OBJECT_STORE_FAIL superblock-or-object\n");
      serial_string("AIUEOS_OBJECT_STORE_FAIL superblock-or-object\r\n");
      qemu_exit(0x70);
    }
    debug_string("AIUEOS_OBJECT_STORE_OK aiuefs-v3 objects=3 catalog=2apps\n");
    serial_string("AIUEOS_OBJECT_STORE_OK aiuefs-v3 objects=3 catalog=2apps\r\n");
    debug_string("AIUEOS_KOTOBA_APP_ADMISSION_OK catalog=rsa2048 apps=2 digest=kotoba-sha256 signature=kotoba-rsa2048-pkcs1 policy=public-key\n");
    serial_string("AIUEOS_KOTOBA_APP_ADMISSION_OK catalog=rsa2048 apps=2 digest=kotoba-sha256 signature=kotoba-rsa2048-pkcs1 policy=public-key\r\n");
    {
      extern unsigned aiueos_object_store_restored_count(void);
      unsigned restored = aiueos_object_store_restored_count();
      if (restored == 1) {
        debug_string("AIUEOS_OBJECT_STORE_RESTORE_OK apps=1 source=initramfs catalog-digest-bound rsa2048 write-readback\n");
        serial_string("AIUEOS_OBJECT_STORE_RESTORE_OK apps=1 source=initramfs catalog-digest-bound rsa2048 write-readback\r\n");
      } else if (restored) {
        debug_string("AIUEOS_OBJECT_STORE_RESTORE_OK apps=2 source=initramfs catalog-digest-bound rsa2048 write-readback\n");
        serial_string("AIUEOS_OBJECT_STORE_RESTORE_OK apps=2 source=initramfs catalog-digest-bound rsa2048 write-readback\r\n");
      }
    }
    if (aiueos_catalog_policy_selftest_ok()) {
      serial_string("AIUEOS_KOTOBA_CATALOG_POLICY_SELFTEST_OK malformed=6\r\n");
    }
    if (!aiueos_journal_ready()) {
      debug_string("AIUEOS_JOURNAL_FAIL write-readback\n");
      serial_string("AIUEOS_JOURNAL_FAIL write-readback\r\n");
      qemu_exit(0x6f);
    }
    if (!aiueos_journal_sequence() || aiueos_journal_slot() < 1 || aiueos_journal_slot() > 2)
      qemu_exit(0x6f);
    debug_string("AIUEOS_JOURNAL_OK dual-slot committed append-readback\n");
    serial_string("AIUEOS_JOURNAL_OK dual-slot committed append-readback\r\n");
    if (aiueos_object_transaction_sequence() != aiueos_journal_sequence()) qemu_exit(0x6f);
    debug_string("AIUEOS_OBJECT_TXN_OK journal-first sector=3 apply-readback route=kotoba fixed-stack\n");
    serial_string("AIUEOS_OBJECT_TXN_OK journal-first sector=3 apply-readback route=kotoba fixed-stack\r\n");
    debug_string("AIUEOS_KOTOBA_JOURNAL_PLAN_OK latest-slot next-sequence rollback-preserved\n");
    serial_string("AIUEOS_KOTOBA_JOURNAL_PLAN_OK latest-slot next-sequence rollback-preserved\r\n");
    debug_string("AIUEOS_KOTOBA_FNV_OK bounded-load journal-object-validation\n");
    serial_string("AIUEOS_KOTOBA_FNV_OK bounded-load journal-object-validation\r\n");
    serial_string("AIUEOS_KOTOBA_RECORD_VALIDATION_OK journal transaction bounded-u32\r\n");
    serial_string("AIUEOS_KOTOBA_STORAGE_READ_VALIDATION_OK superblock mutable-object\r\n");
    serial_string("AIUEOS_KOTOBA_STORAGE_WRITE_OK journal mutable-object bounded-store\r\n");
    {
      /* The storage plane and interrupt-driven block transport are proven at
         this point, so a pending crash receipt is consumed and reported here. */
      extern int aiueos_crash_receipt_consume(uint32_t *, uint32_t *);
      uint32_t crash_reason = 0, crash_sequence = 0;
      int crash_found = aiueos_crash_receipt_consume(&crash_reason, &crash_sequence);
      if (crash_found) {
        if (crash_reason == 42u && crash_sequence) {
          debug_string("AIUEOS_CRASH_RECEIPT_OK reason=42 journal-context consumed readback\n");
          serial_string("AIUEOS_CRASH_RECEIPT_OK reason=42 journal-context consumed readback\r\n");
        } else if (crash_reason == 6u && crash_sequence) {
          debug_string("AIUEOS_CRASH_RECEIPT_OK reason=6 fault-context consumed readback\n");
          serial_string("AIUEOS_CRASH_RECEIPT_OK reason=6 fault-context consumed readback\r\n");
        } else {
          debug_string("AIUEOS_CRASH_RECEIPT_FAIL reason-or-sequence\n");
          serial_string("AIUEOS_CRASH_RECEIPT_FAIL reason-or-sequence\r\n");
          qemu_exit(0x5d);
        }
      }
#ifdef AIUEOS_FAULT_RECEIPT_SMOKE
      if (!crash_found) {
        /* Test-only synthetic fault: an unexpected invalid opcode before the
           end-of-boot probe is expected. The exception dispatcher must write
           the fault receipt over the polled transport and terminate. */
        serial_string("AIUEOS_FAULT_SMOKE synthetic unexpected-ud2\r\n");
        __asm__ volatile("ud2");
      }
#endif
#ifdef AIUEOS_CRASH_RECEIPT_SMOKE
      if (!crash_found) {
        /* Test-only synthetic panic from normal kernel context: persist a
           durable crash receipt, then terminate the way an unrecoverable
           fault would. The next boot must consume and report it before
           completing its evidence gate. */
        extern int aiueos_crash_receipt_write(uint32_t);
        serial_string("AIUEOS_PANIC synthetic reason=42\r\n");
        if (!aiueos_crash_receipt_write(42u)) {
          debug_string("AIUEOS_PANIC_RECEIPT_FAIL write-readback\n");
          serial_string("AIUEOS_PANIC_RECEIPT_FAIL write-readback\r\n");
          qemu_exit(0x5d);
        }
        debug_string("AIUEOS_PANIC_RECEIPT_OK synthetic reason=42 written readback pending\n");
        serial_string("AIUEOS_PANIC_RECEIPT_OK synthetic reason=42 written readback pending\r\n");
        qemu_exit(0x5c);
      }
#endif
    }
    if (!aiueos_service_registry_ready()) qemu_exit(0x6f);
    debug_string("AIUEOS_SERVICE_REGISTRY_OK journal-object ids=2 generation=2,1 restart=1,0 decoder=kotoba fixed-stack\n");
    serial_string("AIUEOS_SERVICE_REGISTRY_OK journal-object ids=2 generation=2,1 restart=1,0 decoder=kotoba fixed-stack\r\n");
    serial_string("AIUEOS_KOTOBA_PCI_PLANNER_OK cap extent msix-region\r\n");
    if (aiueos_journal_recovered()) {
      if (!aiueos_journal_recovered_sequence() ||
          aiueos_journal_sequence() != aiueos_journal_recovered_sequence() + 1) qemu_exit(0x6f);
      debug_string("AIUEOS_JOURNAL_RECOVERY_OK highest-valid selected alternate-slot-append\n");
      serial_string("AIUEOS_JOURNAL_RECOVERY_OK highest-valid selected alternate-slot-append\r\n");
      if (!aiueos_object_transaction_replayed()) qemu_exit(0x6f);
      if (!aiueos_service_registry_replayed()) qemu_exit(0x6f);
      if (!aiueos_recovered_service_registry_ready() ||
          !aiueos_scheduler_restore_service_registry(
            aiueos_recovered_service_registry_state(0),
            aiueos_recovered_service_registry_state(1))) qemu_exit(0x6f);
      __asm__ volatile("sti");
      while (!aiueos_service_runtime_evidence_ready()) __asm__ volatile("hlt");
      __asm__ volatile("cli");
      if (!aiueos_scheduler_persistent_restore_evidence_ready()) qemu_exit(0x6f);
      debug_string("AIUEOS_OBJECT_TXN_REPLAY_OK committed-redo idempotent-before-append\n");
      serial_string("AIUEOS_OBJECT_TXN_REPLAY_OK committed-redo idempotent-before-append\r\n");
      debug_string("AIUEOS_PERSISTENT_SERVICE_BOOTSTRAP_OK registry=replayed kotoba-spawn=2 generation=2,1\n");
      serial_string("AIUEOS_PERSISTENT_SERVICE_BOOTSTRAP_OK registry=replayed kotoba-spawn=2 generation=2,1\r\n");
      if (aiueos_user_object_replay_evidence_ready()) {
        debug_string("AIUEOS_KOTOBA_OBJECT_REPLAY_OK domains=4,5 journals=44-47 objects=42,43\n");
        serial_string("AIUEOS_KOTOBA_OBJECT_REPLAY_OK domains=4,5 journals=44-47 objects=42,43\r\n");
      }
    }
    /* The input result bit is set only after a validated event has been copied
       into the browser envelope; no second mutable readiness check is needed. */
    if (!(pci_result & 4)) {
      serial_string("AIUEOS_VIRTIO_INPUT_FAIL queue-or-envelope\r\n"); qemu_exit(0x6f);
    }
    debug_string("AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured synthetic-smoke\n");
    serial_string("AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured synthetic-smoke\r\n");
    debug_string("AIUEOS_DESKTOP_INPUT_OK envelope-v1 sequence=1 kind=key ime-neutral\n");
    serial_string("AIUEOS_DESKTOP_INPUT_OK envelope-v1 sequence=1 kind=key ime-neutral\r\n");
    if (!(pci_result & 8) || !aiueos_desktop_surface_bind_scanout(
          aiueos_gpu_scanout_width(),aiueos_gpu_scanout_height())) {
      serial_string("AIUEOS_VIRTIO_GPU_FAIL display-info-or-surface-binding\r\n"); qemu_exit(0x6f);
    }
    debug_string("AIUEOS_VIRTIO_GPU_OK modern-pci controlq display-info bounded\n");
    serial_string("AIUEOS_VIRTIO_GPU_OK modern-pci controlq display-info bounded\r\n");
    if (aiueos_gpu_2d_create_ok()) {
      debug_string("AIUEOS_VIRTIO_GPU_CREATE result=ok resource=1 format=2 w=32 h=32\n");
      serial_string("AIUEOS_VIRTIO_GPU_CREATE result=ok resource=1 format=2 w=32 h=32\r\n");
    } else {
      debug_string("AIUEOS_VIRTIO_GPU_CREATE result=absent\n");
      serial_string("AIUEOS_VIRTIO_GPU_CREATE result=absent\r\n");
    }
    if (aiueos_gpu_2d_flush_ok()) {
      debug_string("AIUEOS_VIRTIO_GPU_FLUSH result=ok resource=1\n");
      serial_string("AIUEOS_VIRTIO_GPU_FLUSH result=ok resource=1\r\n");
    } else {
      debug_string("AIUEOS_VIRTIO_GPU_FLUSH result=absent\n");
      serial_string("AIUEOS_VIRTIO_GPU_FLUSH result=absent\r\n");
    }
    debug_string("AIUEOS_BROWSER_DESKTOP_TRANSPORT_OK surface-v1 gpu-scanout-bound input-v1\n");
    serial_string("AIUEOS_BROWSER_DESKTOP_TRANSPORT_OK surface-v1 gpu-scanout-bound input-v1\r\n");
    /* The link layer is OPTIONAL: a boot with no NIC attached must stay green,
       so this reports presence rather than gating on it. When a NIC IS present
       the exchange has to have completed and been admitted, so an attached-but-
       broken device cannot pass as "no network". */
    if (aiueos_virtio_net_ready()) {
      debug_string("AIUEOS_VIRTIO_NET_OK modern-pci rx/tx arp-reply kotoba-admitted\n");
      serial_string("AIUEOS_VIRTIO_NET_OK modern-pci rx/tx arp-reply kotoba-admitted\r\n");
      /* IPv4 rides on the link layer, so it can only be reported where the link
         layer was: a boot with no NIC says nothing about it at all rather than
         reporting an absence it has no way to distinguish from a failure. */
      if (aiueos_ipv4_ready()) {
        debug_string("AIUEOS_IPV4_OK icmp-echo-reply kotoba-admitted\n");
        serial_string("AIUEOS_IPV4_OK icmp-echo-reply kotoba-admitted\r\n");
        /* TCP rides on IPv4 exactly as IPv4 rides on the link layer, so it is
           reported only where IPv4 succeeded: a boot whose echo never came back
           says nothing about TCP rather than reporting a failure it never
           reached. */
        if (aiueos_tcp_ready()) {
          debug_string("AIUEOS_TCP_OK handshake echo close kotoba-admitted\n");
          serial_string("AIUEOS_TCP_OK handshake echo close kotoba-admitted\r\n");
        } else {
          /* Not silent, for the reason AIUEOS_IPV4_FAIL is not, and not a boot
             failure either: whether a peer completes a connection is a property
             of the network. Which phase it stopped at, because unlike the echo
             this exchange has four admissions, and re-running a TCG boot under
             load to find out which costs many minutes. tx-* are build faults
             rather than network ones -- the segment was wrong before it left.
             The numbers mirror NET_TCP_STAGE_* in kernel/pci.c, which is where
             they are set; they are not shared through a header because this
             kernel has none, and every other cross-file contract here is an
             extern declaration written out the same way. */
          unsigned stage = aiueos_tcp_stage();
          if (stage == 5) serial_string("AIUEOS_TCP_FAIL tx-segment-checksum\r\n");
          else if (stage == 6) serial_string("AIUEOS_TCP_FAIL tx-not-completed\r\n");
          else if (stage == 2) serial_string("AIUEOS_TCP_FAIL no-admitted-echo\r\n");
          else if (stage == 3) serial_string("AIUEOS_TCP_FAIL no-admitted-fin-ack\r\n");
          else if (stage == 1) serial_string("AIUEOS_TCP_FAIL no-admitted-syn-ack\r\n");
          else serial_string("AIUEOS_TCP_FAIL no-peer-mac\r\n");
        }
      } else {
        /* Reached only with a NIC present and its link layer already OK, so
           this is a real IPv4 failure and says so. Staying silent would make a
           broken exchange indistinguishable from a build that never attempted
           one -- the same trap AIUEOS_VIRTIO_NET_ABSENT exists to avoid one
           layer down. It does not fail the boot: whether the peer answers ICMP
           at all is a property of the network, not of this OS. */
        serial_string("AIUEOS_IPV4_FAIL no-admitted-echo-reply\r\n");
      }
      /* Reported at link-layer level rather than under IPv4, because that is
         where it sits: DHCP is broadcast and needs neither the ARP cache nor
         anything ICMP proved. A machine that reaches here has an address it was
         GIVEN. DNS and cloud-TCP then send from that address (ADR-0081). */
      if (aiueos_dhcp_ready()) {
        serial_string("AIUEOS_DHCP_OK offer-ack kotoba-admitted address=");
        serial_ipv4(aiueos_dhcp_address());
        serial_string(" mask=");
        serial_ipv4(aiueos_dhcp_mask());
        serial_string(" router=");
        serial_ipv4(aiueos_dhcp_router());
        serial_string(" server=");
        serial_ipv4(aiueos_dhcp_server());
        serial_string(" lease=");
        serial_decimal(aiueos_dhcp_lease_seconds());
        serial_string("\r\n");
        if (aiueos_dhcp_consumed()) {
          serial_string("AIUEOS_DHCP_CONSUMED src=");
          serial_ipv4(aiueos_dhcp_address());
          serial_string(" dns=");
          serial_ipv4(aiueos_dhcp_dns() ? aiueos_dhcp_dns() : 0x0a000203U);
          serial_string("\r\n");
        } else {
          serial_string("AIUEOS_DHCP_CONSUMED result=absent leftover=:lease-not-consumed\r\n");
        }
        serial_string("AIUEOS_DNS_PROBE result=");
        if (aiueos_dns_ready()) {
          serial_string("ok name=kotobase.net a=");
          serial_ipv4(aiueos_dns_a());
          serial_string("\r\n");
        } else {
          serial_string("fail stage=");
          serial_decimal(aiueos_dns_stage());
          serial_string(" leftover=:dns-absent\r\n");
        }
        serial_string("AIUEOS_TCP_CLOUD_PROBE result=");
        if (aiueos_tcp_cloud_ready()) {
          serial_string("ok dst=");
          serial_ipv4(aiueos_dns_a());
          serial_string(" port=443\r\n");
        } else if (!aiueos_dns_ready()) {
          serial_string("skipped leftover=:dns-absent\r\n");
        } else {
          serial_string("fail stage=");
          serial_decimal(aiueos_tcp_cloud_stage());
          serial_string(" leftover=:tcp-cloud-absent\r\n");
        }
        serial_string("AIUEOS_TLS_PROBE result=");
        if (aiueos_tls_handshake_ready()) {
          serial_string("ok");
          if (!aiueos_http_cid_ready())
            serial_string(" leftover=:http-absent");
          serial_string("\r\n");
        } else if (aiueos_tls_record_ready()) {
          serial_string("record type=");
          serial_decimal(aiueos_tls_record_type());
          serial_string(" stage=");
          serial_decimal(aiueos_tls_stage());
          serial_string(" rx=");
          serial_decimal(aiueos_tls_rx_buffered());
          serial_string(" leftover=:tls-handshake-incomplete,:http-absent\r\n");
        } else {
          serial_string("absent leftover=:tls-absent,:http-absent\r\n");
        }
        {
          extern int aiueos_tls13_certverify_ok(void);
          extern uint16_t aiueos_tls13_certverify_scheme(void);
          serial_string("AIUEOS_CERTVERIFY_PROBE result=");
          if (aiueos_tls13_certverify_ok() &&
              aiueos_tls13_certverify_scheme() == 0x0403) {
            serial_string("ok scheme=ecdsa_secp256r1_sha256\r\n");
          } else if (aiueos_tls_handshake_ready() || aiueos_http_cid_ready()) {
            serial_string("hashed-only leftover=:cert-verify-hashed-only\r\n");
          } else {
            serial_string("absent leftover=:cert-verify-hashed-only\r\n");
          }
        }
        serial_string("AIUEOS_HTTP_PROBE result=");
        if (aiueos_http_cid_ready()) {
          serial_string("ok cid=");
          serial_string(aiueos_http_cid());
          serial_string("\r\n");
        } else {
          serial_string("absent leftover=:http-absent app=");
          serial_decimal(aiueos_tls_app_len());
          serial_string(" rec=");
          serial_decimal(aiueos_tls_last_record_type());
          serial_string(" inner=");
          serial_decimal(aiueos_tls_last_inner_type());
          serial_string(" fail=");
          serial_decimal((uint32_t)aiueos_tls_failed());
          serial_string(" fin=");
          serial_decimal((uint32_t)aiueos_tls_finished_sent());
          serial_string(" get=");
          serial_decimal((uint32_t)aiueos_tls_http_sent());
          serial_string(" nst=");
          serial_decimal(aiueos_tls_nst_count());
          serial_string("\r\n");
        }
        if (aiueos_http_cid_ready()) {
          serial_string("AIUEOS_BARE_METAL_P2 green leftover=[]\r\n");
        } else if (aiueos_tls_handshake_ready()) {
          serial_string("AIUEOS_BARE_METAL_P2 not-green leftover=:http-absent\r\n");
        } else if (aiueos_tls_record_ready()) {
          serial_string("AIUEOS_BARE_METAL_P2 not-green leftover=:tls-handshake-incomplete,:http-absent\r\n");
        } else {
          serial_string("AIUEOS_BARE_METAL_P2 not-green leftover=:tls-absent,:http-absent\r\n");
        }
      } else {
        /* Both halves, because they answer different questions: the STAGE says
           which of the two round trips did not complete, and the REASON is the
           clause of the admission that refused the candidate which got
           furthest. A run that refuses for a reason nobody broke is a run whose
           evidence is about something else, and printing only one of these
           would make that indistinguishable. */
        serial_string("AIUEOS_DHCP_FAIL ");
        serial_string(dhcp_stage_name(aiueos_dhcp_stage()));
        serial_string(" reason=");
        serial_decimal(aiueos_dhcp_reason());
        serial_byte(' ');
        serial_string(dhcp_reason_name(aiueos_dhcp_reason()));
        serial_string("\r\n");
      }
    } else {
      serial_string("AIUEOS_VIRTIO_NET_ABSENT no-nic-attached\r\n");
    }
    debug_string("AIUEOS_SCHEDULER_OK tasks=2 policy=round-robin preemption=apic-timer\n");
    serial_string("AIUEOS_SCHEDULER_OK tasks=2 policy=round-robin preemption=apic-timer\r\n");
    debug_string("AIUEOS_SCHEDULER_CR3_OK roots=3 private-pages=2 kernel-return\n");
    serial_string("AIUEOS_SCHEDULER_CR3_OK roots=3 private-pages=2 kernel-return\r\n");
    if (!aiueos_service_runtime_evidence_ready()) qemu_exit(0x6f);
    debug_string("AIUEOS_SERVICE_RUNTIME_OK services=2 descriptors=8 kotoba-policy spawn-restart-terminate task=generic generation=2 budget=bounded\n");
    serial_string("AIUEOS_SERVICE_RUNTIME_OK services=2 descriptors=8 kotoba-policy spawn-restart-terminate task=generic generation=2 budget=bounded\r\n");
    if (!aiueos_service_ipc_evidence_ready()) qemu_exit(0x6f);
    debug_string("AIUEOS_SERVICE_IPC_OK mailbox=bounded capability=owner-domain cross-cr3 sequence=1\n");
    serial_string("AIUEOS_SERVICE_IPC_OK mailbox=bounded capability=owner-domain cross-cr3 sequence=1\r\n");
    if (!aiueos_ioapic_route_legacy_timer()) {
      debug_string("AIUEOS_IOAPIC_FAIL route-legacy-timer\n");
      serial_string("AIUEOS_IOAPIC_FAIL route-legacy-timer\r\n");
      qemu_exit(0x72);
    }
    __asm__ volatile("sti");
    while (aiueos_external_timer_ticks == 0) __asm__ volatile("hlt");
    __asm__ volatile("cli");
    debug_string("AIUEOS_IOAPIC_OK pit-gsi vector=33 eoi-v1\n");
    serial_string("AIUEOS_IOAPIC_OK pit-gsi vector=33 eoi-v1\r\n");
    if (!aiueos_syscall_self_test()) {
      debug_string("AIUEOS_SYSCALL_FAIL abi-capability-pointer\n");
      serial_string("AIUEOS_SYSCALL_FAIL abi-capability-pointer\r\n");
      qemu_exit(0x73);
    }
    if (!aiueos_dynamic_capability_evidence_ready() ||
        !aiueos_capability_derivation_evidence_ready() ||
        aiueos_capability_table_capacity() < 256) qemu_exit(0x73);
    debug_string("AIUEOS_DYNAMIC_CAPABILITY_OK page-backed slots>=256 owner=3 reuse generation retirement\n");
    serial_string("AIUEOS_DYNAMIC_CAPABILITY_OK page-backed slots>=256 owner=3 reuse generation retirement\r\n");
    debug_string("AIUEOS_SYSCALL_OK int80-cpl0 abi-v1\n");
    serial_string("AIUEOS_SYSCALL_OK int80-cpl0 abi-v1\r\n");
    debug_string("AIUEOS_KOTOBA_SYSCALL_PLANNER_OK bootstrap user overflow\n");
    serial_string("AIUEOS_KOTOBA_SYSCALL_PLANNER_OK bootstrap user overflow\r\n");
    debug_string("AIUEOS_KOTOBA_COPY_IN_OK cpl0 hash bounded-256\n");
    serial_string("AIUEOS_KOTOBA_COPY_IN_OK cpl0 hash bounded-256\r\n");
    debug_string("AIUEOS_KOTOBA_CAPABILITY_OK table owner generation type rights revoke reissue derivation=multi-hop recursive-revoke\n");
    serial_string("AIUEOS_KOTOBA_CAPABILITY_OK table owner generation type rights revoke reissue derivation=multi-hop recursive-revoke\r\n");
    debug_string("AIUEOS_COPYIN_OK noncanonical-and-unmapped-denied\n");
    serial_string("AIUEOS_COPYIN_OK noncanonical-and-unmapped-denied\r\n");
    int process_init = aiueos_process_initialize();
    if (process_init != 1) {
      serial_string("AIUEOS_RING3_FAIL tss-or-mapping\r\n"); qemu_exit(0x72);
    }
    debug_string("AIUEOS_PROCESS_FOUNDATION_OK tss-descriptor user-wx guard-page\n");
    serial_string("AIUEOS_PROCESS_FOUNDATION_OK tss-descriptor user-wx guard-page\r\n");
    debug_string("AIUEOS_PROCESS_CREATE_OK descriptors=8 entry-argument-stack domain-address-space-task\n");
    serial_string("AIUEOS_PROCESS_CREATE_OK descriptors=8 entry-argument-stack domain-address-space-task\r\n");
    debug_string("AIUEOS_KOTOBA_ELF_PROCESS_OK source=catalog apps=2 et-exec segments=rx,rw result=42 domains=4,5\n");
    serial_string("AIUEOS_KOTOBA_ELF_PROCESS_OK source=catalog apps=2 et-exec segments=rx,rw result=42 domains=4,5\r\n");
    aiueos_load_task_register();
    aiueos_process_enter();
    if (!aiueos_process_result()) {
      debug_string("AIUEOS_RING3_FAIL syscall-results\n");
      serial_string("AIUEOS_RING3_FAIL syscall-results\r\n");
      qemu_exit(0x71);
    }
    if (!aiueos_syscall_transport_evidence_ready()) qemu_exit(0x71);
    if (!aiueos_kotoba_runtime_evidence_ready()) qemu_exit(0x71);
    debug_string("AIUEOS_KOTOBA_USER_RUNTIME_OK abi=v2 transport=syscall capabilities=2,3,4,5 object=service-registry,user-store service-ipc=mailbox domains=4,5 result=42\n");
    serial_string("AIUEOS_KOTOBA_USER_RUNTIME_OK abi=v2 transport=syscall capabilities=2,3,4,5 object=service-registry,user-store service-ipc=mailbox domains=4,5 result=42\r\n");
    debug_string("AIUEOS_KOTOBA_OBJECT_WRITE_OK domains=4,5 journals=44-47 objects=42,43 value=42 receipt=readback transaction=journal-first serializer=kotoba validator=kotoba decoder=kotoba materializer=kotoba fixed-stack\n");
    serial_string("AIUEOS_KOTOBA_OBJECT_WRITE_OK domains=4,5 journals=44-47 objects=42,43 value=42 receipt=readback transaction=journal-first serializer=kotoba validator=kotoba decoder=kotoba materializer=kotoba fixed-stack\r\n");
    if (!aiueos_kotoba_service_ipc_evidence_ready()) qemu_exit(0x71);
    debug_string("AIUEOS_KOTOBA_SERVICE_IPC_OK senders=4,5 recipients=service0,service1 payload=42 sequence=1 bounded=2 persistent-services=2\n");
    serial_string("AIUEOS_KOTOBA_SERVICE_IPC_OK senders=4,5 recipients=service0,service1 payload=42 sequence=1 bounded=2 persistent-services=2\r\n");
    debug_string("AIUEOS_RING3_OK processes=2 preemptive roots=2 domains=2,3 kernel-stacks=2 syscall-sysret\n");
    serial_string("AIUEOS_RING3_OK processes=2 preemptive roots=2 domains=2,3 kernel-stacks=2 syscall-sysret\r\n");
    debug_string("AIUEOS_SYSRET_OK star-lstar-fmask canonical-rip-rsp rflags-sanitized per-task-stack\n");
    serial_string("AIUEOS_SYSRET_OK star-lstar-fmask canonical-rip-rsp rflags-sanitized per-task-stack\r\n");
    debug_string("AIUEOS_CAPABILITY_TRANSFER_OK source=2 target=3 attenuated atomic-claim transferred-use owner-exit=descendants-revoked\n");
    serial_string("AIUEOS_CAPABILITY_TRANSFER_OK source=2 target=3 attenuated atomic-claim transferred-use owner-exit=descendants-revoked\r\n");
    if (!aiueos_process_lifecycle_evidence_ready()) qemu_exit(0x71);
    if (!aiueos_catalog_lookup_rejection_evidence_ready()) qemu_exit(0x71);
    debug_string("AIUEOS_APP_CATALOG_LOOKUP_OK ids=app/hello,app/worker unknown=denied extents=nonoverlap\n");
    serial_string("AIUEOS_APP_CATALOG_LOOKUP_OK ids=app/hello,app/worker unknown=denied extents=nonoverlap\r\n");
    debug_string("AIUEOS_PROCESS_REAP_OK tasks=4 services=2-persistent process-slots=8 task-slots=8 generations=reused owner-caps-revoked allocator-pages=24 stack-pages=reused zero-reused\n");
    serial_string("AIUEOS_PROCESS_REAP_OK tasks=4 services=2-persistent process-slots=8 task-slots=8 generations=reused owner-caps-revoked allocator-pages=24 stack-pages=reused zero-reused\r\n");
    debug_string("AIUEOS_USER_SYSCALL_OK valid-log copied-payload too-big stale-generation foreign-owner wrong-type no-rights invalid-pointer\n");
    serial_string("AIUEOS_USER_SYSCALL_OK valid-log copied-payload too-big stale-generation foreign-owner wrong-type no-rights invalid-pointer\r\n");
    if (!aiueos_address_space_self_test()) {
      debug_string("AIUEOS_ADDRESS_SPACE_FAIL cr3-or-isolation\n");
      serial_string("AIUEOS_ADDRESS_SPACE_FAIL cr3-or-isolation\r\n");
      qemu_exit(0x76);
    }
    debug_string("AIUEOS_ADDRESS_SPACE_OK processes=2 distinct-cr3 private-pages cross-access-fault\n");
    serial_string("AIUEOS_ADDRESS_SPACE_OK processes=2 distinct-cr3 private-pages cross-access-fault\r\n");
    aiueos_page_fault_stage = 1;
    aiueos_probe_write_protect();
    if (aiueos_page_fault_stage != 0x101 ||
        (aiueos_page_fault_error & 0x3) != 0x3) {
      debug_string("AIUEOS_PAGE_FAULT_FAIL write-protect\n");
      serial_string("AIUEOS_PAGE_FAULT_FAIL write-protect\r\n");
      qemu_exit(0x7a);
    }
    debug_string("AIUEOS_PAGE_FAULT_OK write-protect vector=14\n");
    serial_string("AIUEOS_PAGE_FAULT_OK write-protect vector=14\r\n");
    aiueos_page_fault_stage = 2;
    aiueos_probe_no_execute();
    if (aiueos_page_fault_stage != 0x102 ||
        (aiueos_page_fault_error & 0x11) != 0x11 ||
        (aiueos_page_fault_error & 0x2) != 0) {
      debug_string("AIUEOS_PAGE_FAULT_FAIL no-execute\n");
      serial_string("AIUEOS_PAGE_FAULT_FAIL no-execute\r\n");
      qemu_exit(0x79);
    }
    debug_string("AIUEOS_PAGE_FAULT_OK no-execute vector=14\n");
    serial_string("AIUEOS_PAGE_FAULT_OK no-execute vector=14\r\n");
    extern volatile int aiueos_final_probe_expected;
    aiueos_final_probe_expected = 1;
    __asm__ volatile("ud2");
  }
  for (;;) __asm__ volatile("cli; hlt");
}
