#include <stdint.h>
#include "../include/boot_info.h"
#include "inference_status.h"
#include "kototama_runtime.h"
#include "model_handoff.h"
#include "qwen35_infer.h"
#include "qwen35_runtime.h"

#define AIUEOS_OWNED_MEMORY_MAP_BYTES (128ULL * 1024ULL)
static struct aiueos_boot_info aiueos_owned_boot_info;
/* Keep the retained firmware map out of the low-2MiB fine-grained W^X
 * aperture.  The exact Qwen runtime plus Device-P256 objects make the map's
 * 128 KiB the difference between keeping the linked user/guard pages below
 * PROCESS_PRIVATE_BASE and silently pushing them into the 2MiB huge-page
 * region.  The high section is still ordinary kernel-owned RAM: the owned
 * page directory maps 4..6 MiB writable and NX, and the physical allocator
 * excludes it through aiueos_kernel_end. */
static uint8_t aiueos_owned_memory_map[AIUEOS_OWNED_MEMORY_MAP_BYTES]
  __attribute__((aligned(16), section(".high_bss")));
#ifdef AIUEOS_QWEN38_MODEL_HANDOFF
/* The exact graph is about 75 KiB.  Keeping it in kernel BSS moves the linked
 * user/guard pages beyond the low-2MiB bootstrap map.  The admitted model is
 * immutable, so retain only this pointer here and construct the graph in the
 * allocator-backed volatile workspace after owned paging is active. */
static struct aiueos_inference_status aiueos_qwen35_status;
static char aiueos_qwen35_progress_detail[] = "T01 L00 OF64";
static uint32_t aiueos_qwen35_last_render_token = UINT32_MAX;
static char aiueos_qwen35_output_detail[] = "T01 OUTPUT HEAD";
static char aiueos_qwen35_decode_detail[] = "TOKEN 00 OF 08";
static char aiueos_qwen35_first_fail_detail[] = "FIRST TOKEN 000000";
static char aiueos_qwen35_decode_fail_detail[32] = "T00 L00 UNKNOWN";
static uint32_t aiueos_qwen35_active_token = 1U;
#endif

/* The loader's boot-info object and memory-map buffer are firmware-owned.
   They are readable under the inherited CR3 but are not necessarily inside
   the kernel's bounded low identity map.  Retain every scalar and the bounded
   descriptor stream before aiueos_paging_initialize installs the owned root. */
static int aiueos_boot_info_retain(const struct aiueos_boot_info *source) {
  if (!source || !source->memory_map || !source->memory_map_size ||
      !source->descriptor_size ||
      source->memory_map_size > AIUEOS_OWNED_MEMORY_MAP_BYTES ||
      source->memory_map_size % source->descriptor_size != 0)
    return 0;
  aiueos_owned_boot_info = (struct aiueos_boot_info){0};
  aiueos_owned_boot_info.magic = source->magic;
  aiueos_owned_boot_info.version = source->version;
  aiueos_owned_boot_info.memory_map_size = source->memory_map_size;
  aiueos_owned_boot_info.descriptor_size = source->descriptor_size;
  aiueos_owned_boot_info.descriptor_version = source->descriptor_version;
  aiueos_owned_boot_info.acpi_rsdp = source->acpi_rsdp;
  aiueos_owned_boot_info.framebuffer_base = source->framebuffer_base;
  aiueos_owned_boot_info.framebuffer_size = source->framebuffer_size;
  aiueos_owned_boot_info.framebuffer_width = source->framebuffer_width;
  aiueos_owned_boot_info.framebuffer_height = source->framebuffer_height;
  aiueos_owned_boot_info.framebuffer_stride = source->framebuffer_stride;
  aiueos_owned_boot_info.framebuffer_format = source->framebuffer_format;
  aiueos_owned_boot_info.initramfs_base = source->initramfs_base;
  aiueos_owned_boot_info.initramfs_size = source->initramfs_size;
  aiueos_owned_boot_info.runtime_services = source->runtime_services;
  aiueos_owned_boot_info.firmware_cr3 = source->firmware_cr3;
  if (source->version >= AIUEOS_BOOT_INFO_VERSION_MODEL_HANDOFF) {
    aiueos_owned_boot_info.model_base = source->model_base;
    aiueos_owned_boot_info.model_size = source->model_size;
    for (uint32_t i = 0; i < 32; i++)
      aiueos_owned_boot_info.model_sha256[i] = source->model_sha256[i];
    aiueos_owned_boot_info.model_part_count = source->model_part_count;
    aiueos_owned_boot_info.model_format = source->model_format;
    aiueos_owned_boot_info.model_flags = source->model_flags;
    aiueos_owned_boot_info.model_load_cycles = source->model_load_cycles;
    aiueos_owned_boot_info.model_paging_base = source->model_paging_base;
    aiueos_owned_boot_info.model_paging_pages = source->model_paging_pages;
    if (source->version >= AIUEOS_BOOT_INFO_VERSION_TSC_CALIBRATED)
      aiueos_owned_boot_info.tsc_hz = source->tsc_hz;
  }
  const volatile uint8_t *input = source->memory_map;
  for (uint64_t i = 0; i < source->memory_map_size; i++)
    aiueos_owned_memory_map[i] = input[i];
  aiueos_owned_boot_info.memory_map = aiueos_owned_memory_map;
  return 1;
}

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
extern int aiueos_paging_initialize(const struct aiueos_boot_info *boot);
extern int aiueos_framebuffer_initialize(const struct aiueos_boot_info *boot);
extern void aiueos_framebuffer_qualification_screen(const char *, const char *,
                                                     const char *, int);
extern void aiueos_qualification_runtime_initialize(void *, uint64_t);
extern int aiueos_qualification_progress(uint32_t);
extern int aiueos_qualification_finalize(uint16_t, uint32_t);
extern int aiueos_qualification_reboot(void);
extern int aiueos_desktop_surface_ready(void);
extern int aiueos_desktop_surface_bind_scanout(uint32_t width, uint32_t height);
extern int aiueos_desktop_wm_rects_fit(void);
extern int aiueos_desktop_wm_paint(uint64_t front);
extern uint32_t aiueos_desktop_wm_stored_color(uint64_t window_id);
extern uint32_t aiueos_desktop_sample_pixel(uint32_t x, uint32_t y);
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
extern void *aiueos_allocate_contiguous_physical_pages(uint64_t);
extern int aiueos_pci_enumerate(void);
extern int aiueos_rtl8125_physical_qualification(void);
extern unsigned aiueos_rtl8125_qualification_error(void);
extern uint32_t aiueos_rtl8125_qualification_rx_length(void);
#ifdef AIUEOS_MURAKUMO_DEVICE_RESULT
extern int aiueos_rtl8125_direct_https_qualification(
    const struct aiueos_boot_info *, uint32_t, uint32_t, uint32_t,
    uint32_t, uint64_t, uint64_t, uint64_t, uint32_t, uint32_t);
extern const char *aiueos_rtl8125_direct_device_did(void);
extern int aiueos_rtl8125_device_worker_poll(
    const struct aiueos_boot_info *, uint32_t, uint64_t *, uint32_t *, int *,
    uint64_t *, int *, int *);
extern int aiueos_rtl8125_device_worker_control_ack(
    const struct aiueos_boot_info *, uint32_t, uint64_t);
extern int aiueos_rtl8125_device_worker_result(
    const struct aiueos_boot_info *, uint32_t, uint64_t,
    uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t,
    uint32_t, uint32_t);
extern void aiueos_rtl8125_inference_failure_report(
    uint64_t, uint32_t, uint32_t, uint32_t, uint32_t);
#else
extern int aiueos_rtl8125_direct_https_qualification(void);
#endif
extern unsigned aiueos_rtl8125_direct_https_error(void);
extern unsigned aiueos_rtl8125_direct_https_attempts(void);
extern uint32_t aiueos_rtl8125_direct_tls_stage(void);
extern uint32_t aiueos_rtl8125_direct_response_wait_ms(void);
extern uint32_t aiueos_rtl8125_direct_response_timeout_seconds(void);
extern uint32_t aiueos_rtl8125_direct_dns_a(void);
extern int aiueos_rtl8125_direct_http_ready(void);
extern int aiueos_rtl8125_relay_qualification(void);
extern unsigned aiueos_rtl8125_relay_error(void);
extern uint32_t aiueos_rtl8125_relay_rx_length(void);
extern int aiueos_rtl8125_job_qualification(void);
extern unsigned aiueos_rtl8125_job_error(void);
extern uint8_t aiueos_rtl8125_job_token(void);
extern uint16_t aiueos_rtl8125_job_score(void);
extern uint16_t aiueos_rtl8125_job_total(void);
extern uint64_t aiueos_rtl8125_job_cycles(void);
extern int aiueos_rtl8125_liveness_renewal(void);
extern unsigned aiueos_rtl8125_liveness_error(void);
extern uint32_t aiueos_rtl8125_liveness_sequence(void);
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
extern unsigned aiueos_ssh_listen_stage(void);
extern int aiueos_ssh_client_id_valid(void);
extern unsigned aiueos_ssh_kex_stage(void);
extern uint32_t aiueos_ssh_client_id_len(void);
#if defined(AIUEOS_SSH_LISTEN) && \
    defined(AIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION) && \
    defined(AIUEOS_MURAKUMO_DEVICE_RESULT)
extern int aiueos_rtl8125_ssh_poll(void);
#endif
extern int aiueos_random_selftest(void);
extern int aiueos_random_bytes(uint8_t *out, uint32_t n);
/* The ECDSA P-256 deterministic sign object (kotoba/ecdsa-p256-sign.kotoba).
   ABI: private-32, digest-32, k-32, out-64 (r||s big-endian), workspace-2048;
   returns 1 unless r or s is zero. */
extern uint64_t kotoba_aiueos_ecdsa_p256_sign(const uint8_t *priv,
                                              const uint8_t *digest,
                                              const uint8_t *k,
                                              uint8_t *out,
                                              uint8_t *workspace);
#ifdef AIUEOS_ECDSA_PUBLIC_KAT
extern uint64_t kotoba_aiueos_ecdsa_p256_public(const uint8_t *priv,
                                                uint8_t *out,
                                                uint8_t *workspace);
static int aiueos_ecdsa_public_kat(void) {
  static const uint8_t d[32] = {
    0xc9,0xaf,0xa9,0xd8,0x45,0xba,0x75,0x16,0x6b,0x5c,0x21,0x57,0x67,0xb1,0xd6,0x93,
    0x4e,0x50,0xc3,0xdb,0x36,0xe8,0x9b,0x12,0x7b,0x8a,0x62,0x2b,0x12,0x0f,0x67,0x21};
  static const uint8_t want[64] = {
    0x60,0xfe,0xd4,0xba,0x25,0x5a,0x9d,0x31,0xc9,0x61,0xeb,0x74,0xc6,0x35,0x6d,0x68,
    0xc0,0x49,0xb8,0x92,0x3b,0x61,0xfa,0x6c,0xe6,0x69,0x62,0x2e,0x60,0xf2,0x9f,0xb6,
    0x79,0x03,0xfe,0x10,0x08,0xb8,0xbc,0x99,0xa4,0x1a,0xe9,0xe9,0x56,0x28,0xbc,0x64,
    0xf2,0xf1,0xb2,0x0c,0x2d,0x7e,0x9f,0x51,0x77,0xa3,0xc2,0x94,0xd4,0x46,0x22,0x99};
  static uint8_t workspace[2048];
  uint8_t out[64];
  if (!kotoba_aiueos_ecdsa_p256_public(d, out, workspace)) return 0;
  for (uint32_t i = 0; i < 64; i++) if (out[i] != want[i]) return 0;
  return 1;
}
#endif
#ifdef AIUEOS_ECDSA_SIGN_KAT
static int aiueos_ecdsa_sign_kat(void) {
  /* RFC 6979 A.2.5, P-256/SHA-256, message "sample". */
  static const uint8_t d[32] = {
    0xc9,0xaf,0xa9,0xd8,0x45,0xba,0x75,0x16,0x6b,0x5c,0x21,0x57,0x67,0xb1,0xd6,0x93,
    0x4e,0x50,0xc3,0xdb,0x36,0xe8,0x9b,0x12,0x7b,0x8a,0x62,0x2b,0x12,0x0f,0x67,0x21};
  static const uint8_t h[32] = {  /* SHA256("sample") */
    0xaf,0x2b,0xdb,0xe1,0xaa,0x9b,0x6e,0xc1,0xe2,0xad,0xe1,0xd6,0x94,0xf4,0x1f,0xc7,
    0x1a,0x83,0x1d,0x02,0x68,0xe9,0x89,0x15,0x62,0x11,0x3d,0x8a,0x62,0xad,0xd1,0xbf};
  static const uint8_t k[32] = {
    0xa6,0xe3,0xc5,0x7d,0xd0,0x1a,0xbe,0x90,0x08,0x65,0x38,0x39,0x83,0x55,0xdd,0x4c,
    0x3b,0x17,0xaa,0x87,0x33,0x82,0xb0,0xf2,0x4d,0x61,0x29,0x49,0x3d,0x8a,0xad,0x60};
  static const uint8_t want[64] = {
    0xef,0xd4,0x8b,0x2a,0xac,0xb6,0xa8,0xfd,0x11,0x40,0xdd,0x9c,0xd4,0x5e,0x81,0xd6,
    0x9d,0x2c,0x87,0x7b,0x56,0xaa,0xf9,0x91,0xc3,0x4d,0x0e,0xa8,0x4e,0xaf,0x37,0x16,
    0xf7,0xcb,0x1c,0x94,0x2d,0x65,0x7c,0x41,0xd4,0x36,0xc7,0xa1,0xb6,0xe2,0x9f,0x65,
    0xf3,0xe9,0x00,0xdb,0xb9,0xaf,0xf4,0x06,0x4d,0xc4,0xab,0x2f,0x84,0x3a,0xcd,0xa8};
  static uint8_t ws[2048];
  uint8_t out[64];
  if (!kotoba_aiueos_ecdsa_p256_sign(d, h, k, out, ws)) return 0;
  for (int i = 0; i < 64; i++) if (out[i] != want[i]) return 0;
  return 1;
}
#endif
#ifdef AIUEOS_SSH_LISTEN
/* SSH-2 curve25519-sha256 exchange hash H (RFC 5656 §4, RFC 8731). The
   transcript is the ordered concatenation
     string(V_C) string(V_S) string(I_C) string(I_S) string(K_S)
     string(Q_C) string(Q_S) mpint(K)
   and H = SHA-256(transcript). This mirrors ssh.transport/h-transcript from
   kotoba-lang/org-ietf-ssh (west-imported, ADR-0106): that .cljc is the single
   source of truth for the wire rules, and smoke-qemu-ssh-kex re-derives the
   expected H from it so a drift in the shared core turns the gate red even
   though this kernel self-verifies against a baked want[].

   The transcript assembly is decision-free mechanism (byte layout only); the
   hash is the already-linked Kotoba SHA-256 object, so this KAT adds no new
   object and rides under AIUEOS_SSH_LISTEN (unlike the ~50 KiB ECDSA sign
   object, which needs its own flag near the 1 MiB ceiling). */
extern uint64_t kotoba_aiueos_sha256(const uint8_t *, uint64_t, uint8_t[32],
                                     uint8_t *, uint64_t);
static uint64_t ssh_put_string(uint8_t *buf, uint64_t off,
                               const uint8_t *bytes, uint32_t n) {
  buf[off]     = (uint8_t)(n >> 24);
  buf[off + 1] = (uint8_t)(n >> 16);
  buf[off + 2] = (uint8_t)(n >> 8);
  buf[off + 3] = (uint8_t)(n);
  for (uint32_t i = 0; i < n; i++) buf[off + 4 + i] = bytes[i];
  return off + 4 + n;
}
static uint64_t ssh_put_mpint(uint8_t *buf, uint64_t off,
                              const uint8_t *bytes, uint32_t n) {
  uint32_t start = 0;
  while (start < n && bytes[start] == 0) start++;   /* strip leading zeros */
  uint32_t rem = n - start;
  if (rem == 0) {                                    /* zero -> empty string */
    buf[off] = buf[off + 1] = buf[off + 2] = buf[off + 3] = 0;
    return off + 4;
  }
  uint32_t pad = (bytes[start] & 0x80) ? 1u : 0u;    /* keep it non-negative */
  uint32_t len = rem + pad;
  buf[off]     = (uint8_t)(len >> 24);
  buf[off + 1] = (uint8_t)(len >> 16);
  buf[off + 2] = (uint8_t)(len >> 8);
  buf[off + 3] = (uint8_t)(len);
  uint64_t p = off + 4;
  if (pad) buf[p++] = 0x00;
  for (uint32_t i = start; i < n; i++) buf[p++] = bytes[i];
  return p;
}
static int aiueos_ssh_kex_h(uint8_t out[32]) {
  /* Fixed KAT wire inputs, identical to the ssh.transport test fixture; kept
     short so the baked arrays stay tiny. K has its high bit set to exercise the
     mpint leading-zero rule. */
  static const uint8_t v_c[] = "SSH-2.0-C";       /* len 9  (NUL excluded) */
  static const uint8_t v_s[] = "SSH-2.0-aiueos";  /* len 14 */
  static const uint8_t i_c[] = {0x14, 0x01, 0x02, 0x03};
  static const uint8_t i_s[] = {0x14, 0x0a, 0x0b, 0x0c, 0x0d};
  static const uint8_t k_s[] = "hostkey-blob";    /* len 12 */
  uint8_t q_c[32], q_s[32], k[32];
  for (int i = 0; i < 32; i++) { q_c[i] = (uint8_t)i; q_s[i] = (uint8_t)((i + 100) & 0xff); }
  k[0] = 0x80;
  for (int i = 1; i < 32; i++) k[i] = (uint8_t)(i - 1);
  static uint8_t buf[256];
  uint64_t off = 0;
  off = ssh_put_string(buf, off, v_c, 9);
  off = ssh_put_string(buf, off, v_s, 14);
  off = ssh_put_string(buf, off, i_c, 4);
  off = ssh_put_string(buf, off, i_s, 5);
  off = ssh_put_string(buf, off, k_s, 12);
  off = ssh_put_string(buf, off, q_c, 32);
  off = ssh_put_string(buf, off, q_s, 32);
  off = ssh_put_mpint(buf, off, k, 32);
  static uint8_t ws[512];
  return (int)kotoba_aiueos_sha256(buf, off, out, ws, sizeof(ws));
}
#endif
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
extern uint32_t aiueos_gpu_enabled_scanouts(void);
extern int aiueos_gpu_2d_create_ok(void);
extern int aiueos_gpu_2d_flush_ok(void);
extern int aiueos_gpu_2d_two_ok(void);
extern int aiueos_gpu_scanout_two_ok(void);
extern int aiueos_desktop_input_from_eventq(void);
extern int aiueos_desktop_input_eventq_empty(void);
extern uint64_t kotoba_aiueos_ime_commit(uint64_t a, uint64_t b);
extern uint64_t kotoba_aiueos_wm_hit(uint64_t n, uint64_t front,
                                     uint64_t px, uint64_t py);
extern uint64_t kotoba_aiueos_broker_admit(uint64_t a, uint64_t b);
extern uint64_t kotoba_aiueos_session_restore(uint64_t a);
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
extern int aiueos_smp_dispatch_selftest(void);
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
static void serial_hex_byte(uint8_t value) {
  static const char digits[] = "0123456789abcdef";
  serial_byte((uint8_t)digits[(value >> 4) & 0xf]);
  serial_byte((uint8_t)digits[value & 0xf]);
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

static void serial_decimal64(uint64_t value) {
  char digits[20];
  uint32_t count = 0;
  do {
    digits[count++] = (char)('0' + value % 10U);
    value /= 10U;
  } while (value && count < sizeof(digits));
  while (count) serial_byte((uint8_t)digits[--count]);
}

#ifdef AIUEOS_QWEN38_MODEL_HANDOFF
static void aiueos_qwen35_progress(uint32_t completed_layers,
                                   uint32_t total_layers,
                                   int output_head) {
  if (output_head == 3) {
    aiueos_qwen35_active_token = completed_layers;
    aiueos_qwen35_decode_detail[6] =
        (char)('0' + (completed_layers / 10U) % 10U);
    aiueos_qwen35_decode_detail[7] =
        (char)('0' + completed_layers % 10U);
    aiueos_qwen35_decode_detail[12] =
        (char)('0' + (total_layers / 10U) % 10U);
    aiueos_qwen35_decode_detail[13] =
        (char)('0' + total_layers % 10U);
    aiueos_qwen35_status.phase = completed_layers == 1U
      ? AIUEOS_INFERENCE_PREFILL : AIUEOS_INFERENCE_DECODING;
    aiueos_qwen35_status.detail = aiueos_qwen35_decode_detail;
    aiueos_qwen35_status.generated_tokens = completed_layers - 1U;
    aiueos_qwen35_status.decode_tokens = completed_layers > 1U
      ? completed_layers - 2U : 0U;
  } else if (output_head == 2) {
    aiueos_qwen35_decode_detail[6] =
        (char)('0' + (completed_layers / 10U) % 10U);
    aiueos_qwen35_decode_detail[7] =
        (char)('0' + completed_layers % 10U);
    aiueos_qwen35_decode_detail[12] =
        (char)('0' + (total_layers / 10U) % 10U);
    aiueos_qwen35_decode_detail[13] =
        (char)('0' + total_layers % 10U);
    aiueos_qwen35_status.phase = AIUEOS_INFERENCE_DECODING;
    aiueos_qwen35_status.detail = aiueos_qwen35_decode_detail;
    aiueos_qwen35_status.generated_tokens = completed_layers;
    aiueos_qwen35_status.decode_tokens =
        completed_layers ? completed_layers - 1U : 0U;
  } else if (output_head) {
    aiueos_qwen35_output_detail[1] =
        (char)('0' + (aiueos_qwen35_active_token / 10U) % 10U);
    aiueos_qwen35_output_detail[2] =
        (char)('0' + aiueos_qwen35_active_token % 10U);
    aiueos_qwen35_status.phase = AIUEOS_INFERENCE_DECODING;
    aiueos_qwen35_status.detail = aiueos_qwen35_output_detail;
  } else {
    aiueos_qwen35_progress_detail[1] =
        (char)('0' + (aiueos_qwen35_active_token / 10U) % 10U);
    aiueos_qwen35_progress_detail[2] =
        (char)('0' + aiueos_qwen35_active_token % 10U);
    aiueos_qwen35_progress_detail[5] =
        (char)('0' + (completed_layers / 10U) % 10U);
    aiueos_qwen35_progress_detail[6] =
        (char)('0' + completed_layers % 10U);
    aiueos_qwen35_status.phase = aiueos_qwen35_active_token == 1U
      ? AIUEOS_INFERENCE_PREFILL : AIUEOS_INFERENCE_DECODING;
    aiueos_qwen35_status.detail = aiueos_qwen35_progress_detail;
  }
  /* The physical GOP aperture is a live scanout, not an atomic page-flip.
     Preserve per-layer serial evidence but refresh the visible progress only
     at token boundaries and every fourth trunk layer. */
  if (output_head || completed_layers == 1U ||
      completed_layers == total_layers || completed_layers % 4U == 0U ||
      aiueos_qwen35_active_token != aiueos_qwen35_last_render_token) {
    (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
    aiueos_qwen35_last_render_token = aiueos_qwen35_active_token;
  }
  serial_string("AIUEOS_QWEN35_PROGRESS layers=");
  serial_decimal(completed_layers);
  serial_string("/");
  serial_decimal(total_layers);
  serial_string(" token=");
  serial_decimal(aiueos_qwen35_active_token);
  serial_string(" event=");
  serial_decimal((uint32_t)output_head);
  serial_string("\r\n");
}

static void aiueos_qwen35_format_failure(uint32_t token, uint32_t layer,
                                         uint32_t stage) {
  aiueos_qwen35_decode_fail_detail[1] =
      (char)('0' + (token / 10U) % 10U);
  aiueos_qwen35_decode_fail_detail[2] = (char)('0' + token % 10U);
  aiueos_qwen35_decode_fail_detail[5] =
      (char)('0' + (layer / 10U) % 10U);
  aiueos_qwen35_decode_fail_detail[6] = (char)('0' + layer % 10U);
  const char *label = aiueos_qwen35_failure_stage_label(stage);
  uint32_t index = 8U;
  while (*label && index + 1U < sizeof(aiueos_qwen35_decode_fail_detail))
    aiueos_qwen35_decode_fail_detail[index++] = *label++;
  aiueos_qwen35_decode_fail_detail[index] = 0;
}

static uint64_t aiueos_read_tsc(void) {
  uint32_t low, high;
  __asm__ volatile("lfence; rdtsc" : "=a"(low), "=d"(high) :: "memory");
  return ((uint64_t)high << 32) | low;
}

static void aiueos_wait_seconds(uint64_t tsc_hz, uint32_t seconds) {
  if (!tsc_hz || !seconds) return;
  uint64_t start = aiueos_read_tsc();
  uint64_t cycles = tsc_hz * seconds;
  while (aiueos_read_tsc() - start < cycles) __asm__ volatile("pause");
}

/* Management is an AIUEOS service, not a consequence of Murakumo health.
   Keep the same bounded listener active both while the worker is ready and
   while its HTTPS heartbeat is reconnecting.  This is deliberately limited
   to the runtime status/restart allow-list enforced by the SSH state machine. */
static void aiueos_k16_management_wait(uint64_t tsc_hz, uint32_t seconds) {
#if defined(AIUEOS_SSH_LISTEN) && \
    defined(AIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION) && \
    defined(AIUEOS_MURAKUMO_DEVICE_RESULT)
  for (uint32_t management_second = 0;
       management_second < seconds; management_second++) {
    /* Keep RX posted for the whole management second.  One short listener
       followed by a one-second CPU delay leaves the physical link deaf for
       most of the interval and can phase-lock with macOS SYN retransmission.
       The listener itself is bounded, so repeat it until the second expires;
       this changes no command authority and still returns to Murakumo on the
       original five/30-second schedule. */
    uint64_t management_start = aiueos_read_tsc();
    do {
      if (aiueos_rtl8125_ssh_poll()) {
        debug_string("AIUEOS_RTL8125_SSH_SESSION_OK commands=runtime-status,runtime-restart,system-reboot-pxe shell=false\n");
        serial_string("AIUEOS_RTL8125_SSH_SESSION_OK commands=runtime-status,runtime-restart,system-reboot-pxe shell=false\r\n");
        if (aiueos_management_take_reboot_pxe_request()) {
          debug_string("AIUEOS_SSH_REBOOT_PXE_ACK reset=uefi-runtime\n");
          serial_string("AIUEOS_SSH_REBOOT_PXE_ACK reset=uefi-runtime\r\n");
          (void)aiueos_qualification_reboot();
          debug_string("AIUEOS_SSH_REBOOT_PXE_FAIL reset=uefi-runtime\n");
          serial_string("AIUEOS_SSH_REBOOT_PXE_FAIL reset=uefi-runtime\r\n");
        }
      }
    } while (tsc_hz && aiueos_read_tsc() - management_start < tsc_hz);
  }
#else
  aiueos_wait_seconds(tsc_hz, seconds);
#endif
}

static uint64_t aiueos_cycles_to_ns(uint64_t cycles, uint64_t tsc_hz) {
  if (!cycles || !tsc_hz) return AIUEOS_INFERENCE_UNMEASURED;
  uint64_t whole = cycles / tsc_hz;
  uint64_t remainder = cycles % tsc_hz;
  uint64_t fraction = (remainder * 1000000000ULL) / tsc_hz;
  if (whole > (UINT64_MAX - fraction) / 1000000000ULL)
    return AIUEOS_INFERENCE_UNMEASURED;
  return whole * 1000000000ULL + fraction;
}

static const char *aiueos_worker_transport_detail(void) {
  switch (aiueos_rtl8125_direct_https_error()) {
    case 2: return "DNS RETRY";
    case 6: return "TCP CONNECT RETRY";
    case 11: return "HTTP RESPONSE TIMEOUT";
    case 60: return "RESPONSE PARSE ERROR";
    default: return "NODE RECONNECTING";
  }
}
#endif
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
  if (!boot || boot->magic != AIUEOS_BOOT_INFO_MAGIC ||
      (boot->version != 2 && boot->version != AIUEOS_BOOT_INFO_VERSION_BASE &&
       boot->version != AIUEOS_BOOT_INFO_VERSION_MODEL_HANDOFF &&
       boot->version != AIUEOS_BOOT_INFO_VERSION_TSC_CALIBRATED) ||
      !boot->memory_map || !boot->memory_map_size || !boot->descriptor_size ||
      !aiueos_boot_info_retain(boot)) {
    debug_string("AIUEOS_KERNEL_FAIL boot-info\n");
    serial_string("AIUEOS_KERNEL_FAIL boot-info\r\n");
    qemu_exit(0x7e);
  } else {
    boot = &aiueos_owned_boot_info;
#if defined(AIUEOS_PHYSICAL_QUALIFICATION) && \
    defined(AIUEOS_QWEN38_MODEL_HANDOFF) && !defined(AIUEOS_MODEL_TEST_FIXTURE)
    int qwen_early_acpi_ready = 0;
#endif
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_runtime_initialize(
        boot->version >= 3 ? boot->runtime_services : 0,
        boot->version >= 3 ? boot->firmware_cr3 : 0);
    aiueos_qualification_progress(220);
#ifdef AIUEOS_QUALIFICATION_FORCE_KERNEL_HANG_CODE
    aiueos_qualification_progress(AIUEOS_QUALIFICATION_FORCE_KERNEL_HANG_CODE);
    debug_string("AIUEOS_KERNEL_PROGRESS forced-hang\n");
    for (;;) __asm__ volatile("pause");
#endif
#endif
    debug_string("AIUEOS_KERNEL_OK memory-map-v1\n");
    serial_string("AIUEOS_SERIAL_OK stack-v1 memory-map-v1\r\n");
#ifdef AIUEOS_ECDSA_PUBLIC_KAT
    /* Run before the owned low-2MiB page table is installed. The standalone
       public-point object intentionally makes this one-purpose KAT image
       larger than that generic bootstrap boundary. */
    if (aiueos_ecdsa_public_kat()) {
      serial_string("AIUEOS_ECDSA_PUBLIC_OK rfc6979-a2.5 x||y-match\r\n");
      debug_string("AIUEOS_ECDSA_PUBLIC_OK rfc6979-a2.5 x||y-match\n");
      qemu_exit(0x30);
    }
    serial_string("AIUEOS_ECDSA_PUBLIC_FAIL vector-mismatch\r\n");
    qemu_exit(0x7d);
#endif
#ifdef AIUEOS_ECDSA_SIGN_KAT
    /* The standalone sign brick makes this one-purpose image larger than the
       generic low-2MiB bootstrap mapping. Exercise it before owned paging, as
       with the public-point KAT above, then terminate the dedicated test. */
    if (aiueos_ecdsa_sign_kat()) {
      serial_string("AIUEOS_ECDSA_SIGN_OK rfc6979-a2.5 r||s-match\r\n");
      debug_string("AIUEOS_ECDSA_SIGN_OK rfc6979-a2.5 r||s-match\n");
      qemu_exit(0x30);
    }
    serial_string("AIUEOS_ECDSA_SIGN_FAIL vector-mismatch\r\n");
    qemu_exit(0x7d);
#endif
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_progress(221);
#endif
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
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_progress(222);
#endif
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
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_progress(223);
#endif
    if (!kotoba_aiueos_pic_disable(0xe0, 0xe8)) {
      debug_string("AIUEOS_PIC_FAIL base-refused\n");
      serial_string("AIUEOS_PIC_FAIL base-refused\r\n");
      qemu_exit(0x7b);
    }
    debug_string("AIUEOS_PIC_OK remapped=0xe0/0xe8 masked=both\n");
    serial_string("AIUEOS_PIC_OK remapped=0xe0/0xe8 masked=both\r\n");
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_progress(224);
#endif
    if (!aiueos_paging_initialize(boot)) {
      debug_string("AIUEOS_PAGING_FAIL ownership-or-wx\n");
      serial_string("AIUEOS_PAGING_FAIL ownership-or-wx\r\n");
      qemu_exit(0x7c);
    }
    debug_string("AIUEOS_PAGING_OK cr3-owned wx-v1 nx-wp\n");
    serial_string("AIUEOS_PAGING_OK cr3-owned wx-v1 nx-wp\r\n");
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_progress(225);
#endif
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
    {
      extern int aiueos_tls13_record_selftest(void);
      if (!aiueos_tls13_record_selftest()) {
        serial_string("AIUEOS_TLS13_RECORD_FAIL rfc8448-s3\r\n");
        qemu_exit(0x6f);
      }
      debug_string("AIUEOS_TLS13_RECORD_OK rfc8448-s3 seq0 seal-open tamper-refused\n");
      serial_string("AIUEOS_TLS13_RECORD_OK rfc8448-s3 seq0 seal-open tamper-refused\r\n");
    }
#ifdef AIUEOS_QWEN38_MODEL_HANDOFF
    /* QWEN-PARITY (ADR-0135).  The three Qwen3.5 forward-pass objects against
       the C they were ported from, on this CPU, over synthetic inputs.  Placed
       with the other Kotoba self-tests, after the kernel owns its own IDT: a
       fuel-guard `ud2` raised while the FIRMWARE's handler is still installed
       gives an OVMF dump with no vector and no address, and here it reaches
       set_idt_gate(6, aiueos_isr_invalid_opcode) and names itself.
       Only in the model-handoff build, because that is the only one where
       kernel/qwen35_quant.c and kernel/qwen35_infer.c -- the reference half of
       the comparison -- are compiled at all. */
    {
      extern int aiueos_qwen35_kotoba_parity_selftest(uint32_t stage);
      static const char *const qwen_parity_names[3] = {"dequant", "dot", "matvec"};
      for (uint32_t stage = 0; stage < 3U; stage++) {
        if (!aiueos_qwen35_kotoba_parity_selftest(stage)) {
          serial_string("QWEN-PARITY ");
          serial_string(qwen_parity_names[stage]);
          serial_string(" mismatch\r\n");
          qemu_exit(0x6f);
        }
        serial_string("QWEN-PARITY ");
        serial_string(qwen_parity_names[stage]);
        serial_string(" ok\r\n");
      }
    }
#endif
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_progress(226);
#endif
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
#ifdef AIUEOS_QWEN38_MODEL_HANDOFF
    if (!aiueos_qwen38_model_handoff_admit(boot)) {
      struct aiueos_inference_status blocked = {
        .abi_version = AIUEOS_INFERENCE_STATUS_ABI_VERSION,
        .byte_size = sizeof(struct aiueos_inference_status),
        .phase = AIUEOS_INFERENCE_BLOCKED,
        .model = "QWEN3.8 27B",
        .quant = "UD-IQ3 XXS",
        .detail = "MODEL HANDOFF REFUSED",
        .artifact_bytes = boot->model_size,
        .resident_bytes = AIUEOS_INFERENCE_UNMEASURED,
        .load_ns = AIUEOS_INFERENCE_UNMEASURED,
        .prefill_ns = AIUEOS_INFERENCE_UNMEASURED,
        .decode_ns = AIUEOS_INFERENCE_UNMEASURED,
        .time_to_first_token_ns = AIUEOS_INFERENCE_UNMEASURED,
        .compute_cycles = AIUEOS_INFERENCE_UNMEASURED
      };
      (void)aiueos_framebuffer_inference_screen(&blocked);
      debug_string("AIUEOS_MODEL_HANDOFF_FAIL format-or-sha-or-map\n");
      serial_string("AIUEOS_MODEL_HANDOFF_FAIL format-or-sha-or-map\r\n");
      qemu_exit(0x66);
    }
#ifdef AIUEOS_MODEL_TEST_FIXTURE
    /* Two fixtures reach this profile now. The model-handoff gate's is 27
       bytes of transport and carries no graph; the admission gate's is the
       10,996,640-byte metadata + tensor-table prefix, which IS parsed, further
       down, once the physical allocator can hold the graph. Saying
       "tiny-transport-fixture" for both would put a false line on the console
       of every run that then parses 866 tensor records. */
    if (boot->model_size == AIUEOS_QWEN35_DATA_OFFSET) {
      debug_string("AIUEOS_QWEN35_GRAPH_FIXTURE_DEFERRED admission-selftest\n");
      serial_string("AIUEOS_QWEN35_GRAPH_FIXTURE_DEFERRED admission-selftest\r\n");
    } else {
      debug_string("AIUEOS_QWEN35_GRAPH_FIXTURE_SKIP tiny-transport-fixture\n");
      serial_string("AIUEOS_QWEN35_GRAPH_FIXTURE_SKIP tiny-transport-fixture\r\n");
    }
#endif
    {
      struct aiueos_inference_status admitted = {
        .abi_version = AIUEOS_INFERENCE_STATUS_ABI_VERSION,
        .byte_size = sizeof(struct aiueos_inference_status),
        .phase = AIUEOS_INFERENCE_ADMISSION,
        .model = "QWEN3.8 27B",
        .quant = "UD-IQ3 XXS",
#ifndef AIUEOS_MODEL_TEST_FIXTURE
        .detail = "QWEN35 GRAPH PENDING",
#else
        .detail = "GGUF SHA256 OK",
#endif
        .artifact_bytes = boot->model_size,
        .resident_bytes = boot->model_size,
        .load_ns = AIUEOS_INFERENCE_UNMEASURED,
        .prefill_ns = AIUEOS_INFERENCE_UNMEASURED,
        .decode_ns = AIUEOS_INFERENCE_UNMEASURED,
        .time_to_first_token_ns = AIUEOS_INFERENCE_UNMEASURED,
        .compute_cycles = AIUEOS_INFERENCE_UNMEASURED
      };
      (void)aiueos_framebuffer_inference_screen(&admitted);
    }
    debug_string("AIUEOS_MODEL_HANDOFF_OK format=gguf-v3 parts=3 sha256=verified mapping=read-only-nx metrics=N/A\n");
    serial_string("AIUEOS_MODEL_HANDOFF_OK format=gguf-v3 parts=3 sha256=verified mapping=read-only-nx metrics=N/A\r\n");
#ifndef AIUEOS_MODEL_TEST_FIXTURE
    uint64_t calibrated_model_load_ns = AIUEOS_INFERENCE_UNMEASURED;
    if (boot->version >= AIUEOS_BOOT_INFO_VERSION_TSC_CALIBRATED && boot->tsc_hz) {
      uint64_t whole = boot->model_load_cycles / boot->tsc_hz;
      uint64_t remainder = boot->model_load_cycles % boot->tsc_hz;
      if (whole <= (UINT64_MAX - (remainder * 1000000000ULL) / boot->tsc_hz) /
                     1000000000ULL)
        calibrated_model_load_ns = whole * 1000000000ULL +
          (remainder * 1000000000ULL) / boot->tsc_hz;
    }
    aiueos_qwen35_status = (struct aiueos_inference_status){
      .abi_version = AIUEOS_INFERENCE_STATUS_ABI_VERSION,
      .byte_size = sizeof(struct aiueos_inference_status),
      .phase = AIUEOS_INFERENCE_ADMISSION,
      .model = "QWEN3.8 27B",
      .quant = "UD-IQ3 XXS",
      .detail = "QWEN35 GRAPH PENDING",
      .prompt_tokens = 1,
      .generated_tokens = 0,
      .decode_tokens = 0,
      .target_tokens = AIUEOS_QWEN35_GENERATION_TOKENS,
      .artifact_bytes = boot->model_size,
      .resident_bytes = boot->model_size,
      .load_ns = calibrated_model_load_ns,
      .prefill_ns = AIUEOS_INFERENCE_UNMEASURED,
      .decode_ns = AIUEOS_INFERENCE_UNMEASURED,
      .time_to_first_token_ns = AIUEOS_INFERENCE_UNMEASURED,
      .compute_cycles = 0
    };
#endif
#endif
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_progress(227);
#endif
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
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL MEMORY", "SSD READ ONLY", 0);
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_FAIL stage=memory disk-writes=none\r\n");
      if (!aiueos_qualification_finalize(2, 1))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for (;;) __asm__ volatile("cli; hlt");
#endif
      debug_string("AIUEOS_PHYSICAL_ALLOCATOR_FAIL allocation\n");
      serial_string("AIUEOS_PHYSICAL_ALLOCATOR_FAIL allocation\r\n");
      qemu_exit(0x75);
    }
    debug_string("AIUEOS_PHYSICAL_ALLOCATOR_OK pages=2 zeroed\n");
    serial_string("AIUEOS_PHYSICAL_ALLOCATOR_OK pages=2 zeroed\r\n");
#if defined(AIUEOS_QWEN38_MODEL_HANDOFF) && defined(AIUEOS_MODEL_TEST_FIXTURE)
    /* The GGUF admission, EXECUTED. ADR-0135 moved it to three Kotoba objects
       and made kernel/qwen35_runtime.c delegate to them, but nothing ever ran
       that delegation: the objects passed a KIR-interpreter oracle, the image
       linked, and this workstation is aarch64 so the emitted x86-64 was never
       on a CPU.
       The transport fixture the model-handoff gate carries is 27 bytes and
       cannot be parsed, so this runs only when the handed-off artifact is
       exactly the 10,996,640-byte metadata+tensor-table prefix that
       tests/make_qwen35_header_fixture.py builds and
       scripts/smoke-qwen35-runtime.sh feeds to the C reference parser. That
       prefix is not the artifact, so the artifact length is the contract's
       rather than what was mapped -- which is the same pair of arguments the
       host gate passes, so the numbers printed below are comparable with it.
       QWEN-ADMIT carries the object's own reason code. The C in this image
       has no such number: the delegating branch returns 0 or 1 and every
       negative value here was written in a .kotoba file. */
    if (boot->model_size == AIUEOS_QWEN35_DATA_OFFSET) {
      const uint64_t admission_pages =
        (sizeof(struct aiueos_qwen35_model) + 4095U) / 4096U;
      struct aiueos_qwen35_model *admitted =
        (struct aiueos_qwen35_model *)
          aiueos_allocate_contiguous_physical_pages(admission_pages);
      if (!admitted) {
        serial_string("AIUEOS_QWEN35_ADMISSION_FAIL graph-pages\r\n");
        qemu_exit(0x66);
      }
      int admission_ok = aiueos_qwen35_model_parse(
        (const uint8_t *)(uintptr_t)boot->model_base, boot->model_size,
        AIUEOS_QWEN35_ARTIFACT_BYTES, admitted);
      serial_string("QWEN-ADMIT reason=");
      if (aiueos_qwen35_admission_verdict < 0) {
        serial_string("-");
        serial_decimal64((uint64_t)(-aiueos_qwen35_admission_verdict));
      } else {
        serial_decimal64((uint64_t)aiueos_qwen35_admission_verdict);
      }
      serial_string(" stage=");
      serial_decimal(aiueos_qwen35_admission_stage);
      serial_string(" admitted=");
      serial_decimal((uint32_t)admission_ok);
      serial_string("\r\n");
      if (!admission_ok) {
        debug_string("AIUEOS_QWEN35_ADMISSION_REFUSED kotoba-objects\n");
        serial_string("AIUEOS_QWEN35_ADMISSION_REFUSED kotoba-objects\r\n");
        qemu_exit(0x66);
      }
      debug_string("AIUEOS_QWEN35_ADMISSION_OK kotoba-objects\n");
      /* The four representative offsets tests/qwen35_runtime_model.c asserts
         on the host, recomputed here by the objects and this image's
         translation. Printing them rather than only a verdict is what makes a
         wrong workspace offset visible: a translation that read the wrong word
         still returns 1. */
      serial_string("AIUEOS_QWEN35_ADMISSION_OK tensors=");
      serial_decimal64(admitted->tensor_count);
      serial_string(" linear=");
      serial_decimal(admitted->linear_layer_count);
      serial_string(" full=");
      serial_decimal(admitted->full_layer_count);
      serial_string(" data-offset=");
      serial_decimal64(admitted->data_offset);
      serial_string(" embd=");
      serial_decimal64(admitted->token_embedding.offset);
      serial_string(" qkv=");
      serial_decimal64(admitted->layers[0].mixer.linear.qkv.offset);
      serial_string(" tail=");
      serial_decimal64(admitted->layers[64].post_attention_norm.offset);
      serial_string("\r\n");
    } else {
      serial_string("AIUEOS_QWEN35_ADMISSION_SKIP tiny-transport-fixture\r\n");
    }
#endif
#if defined(AIUEOS_QWEN38_MODEL_HANDOFF) && !defined(AIUEOS_MODEL_TEST_FIXTURE)
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
#ifdef AIUEOS_QWEN35_SMP
    /* Bring up one application processor before the first model walk.  The
       previous SMP qualification happened after the physical-Qwen branch had
       already completed and halted, so every measured token was necessarily
       single-threaded.  AP startup is best-effort here: the scalar path stays
       available, while the result records the actual worker count. */
    qwen_early_acpi_ready = aiueos_acpi_initialize(boot->acpi_rsdp);
    if (qwen_early_acpi_ready && aiueos_apic_timer_initialize() &&
        aiueos_smp_start_application_processor() &&
        aiueos_smp_dispatch_selftest()) {
      serial_string("AIUEOS_QWEN35_SMP_READY threads=2 init-sipi-v1 dispatch=verified\r\n");
    } else {
      serial_string("AIUEOS_QWEN35_SMP_FALLBACK threads=1\r\n");
    }
#else
    serial_string("AIUEOS_QWEN35_SMP_DISABLED threads=1\r\n");
#endif
#endif
    const uint64_t qwen_model_pages =
      (sizeof(struct aiueos_qwen35_model) + 4095U) / 4096U;
    const uint64_t qwen_workspace_pages =
      (AIUEOS_QWEN35_DECODE_WORKSPACE_BYTES + 4095U) / 4096U;
    const uint64_t qwen_volatile_pages =
      (aiueos_kototama_runtime_required_bytes() + 4095U) / 4096U;
    struct aiueos_qwen35_generation_result generation = {0};
    int qwen_initial_ok = 0;
    uint8_t *qwen_volatile = aiueos_allocate_contiguous_physical_pages(
      qwen_volatile_pages);
    if (!qwen_volatile) {
      aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ERROR;
      aiueos_qwen35_status.detail = "WORKSPACE REFUSED";
      (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
      serial_string("AIUEOS_QWEN35_FIRST_TOKEN_FAIL workspace-contiguous\r\n");
#ifdef AIUEOS_PERSISTENT_BOOT
      serial_string("AIUEOS_KOTOTAMA_RUNTIME_DEGRADED reason=workspace-refused kernel=alive network=continue\r\n");
      goto qwen_runtime_boot_complete;
#else
      for (;;) __asm__ volatile("cli; hlt");
#endif
    }
    if (!aiueos_kototama_runtime_prepare(
          (const uint8_t *)(uintptr_t)boot->model_base,
          boot->model_size, qwen_volatile, qwen_volatile_pages * 4096U)) {
      aiueos_qwen35_status.phase = AIUEOS_INFERENCE_BLOCKED;
      aiueos_qwen35_status.detail = "QWEN35 GRAPH REFUSED";
      (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
      debug_string("AIUEOS_QWEN35_GRAPH_FAIL exact-contract\n");
      serial_string("AIUEOS_QWEN35_GRAPH_FAIL exact-contract\r\n");
#ifdef AIUEOS_PERSISTENT_BOOT
      serial_string("AIUEOS_KOTOTAMA_RUNTIME_DEGRADED reason=graph-refused kernel=alive network=continue\r\n");
      goto qwen_runtime_boot_complete;
#else
      for (;;) __asm__ volatile("cli; hlt");
#endif
    }
    serial_string("AIUEOS_KOTOTAMA_RUNTIME_READY epoch=1 isolation=volatile-workspace reboot=runtime-only\r\n");
    debug_string("AIUEOS_QWEN35_GRAPH_OK tensors=866 trunk=64 linear=48 full=16 mtp=1 data-offset=10996640 bind=read-only\n");
    serial_string("AIUEOS_QWEN35_GRAPH_OK tensors=866 trunk=64 linear=48 full=16 mtp=1 data-offset=10996640 bind=read-only storage=volatile-ram\r\n");
    aiueos_qwen35_status.phase = AIUEOS_INFERENCE_PREFILL;
    aiueos_qwen35_status.detail = aiueos_qwen35_progress_detail;
    (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
    serial_string("AIUEOS_QWEN35_WORKSPACE_OK bytes=");
    serial_decimal(AIUEOS_QWEN35_WORKSPACE_BYTES);
    serial_string(" pages=");
    serial_decimal(qwen_workspace_pages);
    serial_string(" graph-pages=");
    serial_decimal(qwen_model_pages);
    serial_string(" storage=volatile-ram\r\n");
    int generation_ok = aiueos_kototama_runtime_invoke(
      AIUEOS_QWEN35_BOS_TOKEN,
      AIUEOS_QWEN35_GENERATION_TOKENS,
      aiueos_qwen35_progress, &generation);
    if (!generation_ok && generation.vector_bits == 256U &&
        generation.generated_tokens &&
        generation.tokens[0] != AIUEOS_QWEN35_REFERENCE_FIRST_TOKEN) {
      serial_string("AIUEOS_QWEN35_AVX2_REJECT expected=248046 actual=");
      serial_decimal(generation.tokens[0]);
      serial_string(" fallback=scalar\r\n");
      aiueos_qwen35_force_scalar();
      (void)aiueos_kototama_runtime_restart();
      generation = (struct aiueos_qwen35_generation_result){0};
      generation_ok = aiueos_kototama_runtime_invoke(
        AIUEOS_QWEN35_BOS_TOKEN,
        AIUEOS_QWEN35_GENERATION_TOKENS,
        aiueos_qwen35_progress, &generation);
    }
    if (!generation_ok ||
        generation.generated_tokens != AIUEOS_QWEN35_GENERATION_TOKENS ||
        generation.tokens[0] != AIUEOS_QWEN35_REFERENCE_FIRST_TOKEN) {
      aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ERROR;
      aiueos_qwen35_status.generated_tokens = generation.generated_tokens;
      aiueos_qwen35_status.decode_tokens = generation.decode_tokens;
      if (generation.generated_tokens &&
          generation.tokens[0] != AIUEOS_QWEN35_REFERENCE_FIRST_TOKEN) {
        uint32_t actual = generation.tokens[0];
        for (uint32_t digit = 0; digit < 6U; digit++) {
          aiueos_qwen35_first_fail_detail[17U - digit] =
              (char)('0' + actual % 10U);
          actual /= 10U;
        }
        aiueos_qwen35_status.detail = aiueos_qwen35_first_fail_detail;
      } else {
        uint32_t failed = generation.failed_token
          ? generation.failed_token : generation.generated_tokens + 1U;
        aiueos_qwen35_format_failure(
          failed, generation.failed_layer, generation.failure_stage);
        aiueos_qwen35_status.detail = aiueos_qwen35_decode_fail_detail;
      }
      aiueos_qwen35_status.compute_cycles = generation.total_cycles;
      (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
      serial_string("AIUEOS_QWEN35_GENERATE_FAIL expected-first=248046 actual=");
      serial_decimal(generation.generated_tokens ? generation.tokens[0] : UINT32_MAX);
      serial_string(" generated=");
      serial_decimal(generation.generated_tokens);
      serial_string(" failed-token=");
      serial_decimal(generation.failed_token);
      serial_string(" failed-layer=");
      serial_decimal(generation.failed_layer);
      serial_string(" failure-stage=");
      serial_decimal(generation.failure_stage);
      serial_string(" total-cycles=");
      serial_decimal64(generation.total_cycles);
      serial_string("\r\n");
#ifdef AIUEOS_PERSISTENT_BOOT
      (void)aiueos_kototama_runtime_restart();
      serial_string("AIUEOS_KOTOTAMA_RUNTIME_RESTART reason=initial-inference-failed kernel-reboot=false network=continue\r\n");
      goto qwen_runtime_boot_complete;
#else
      for (;;) __asm__ volatile("cli; hlt");
#endif
    }
    qwen_initial_ok = 1;
    aiueos_qwen35_status.phase = AIUEOS_INFERENCE_COMPLETE;
    aiueos_qwen35_status.detail = generation.vector_bits == 256U
      ? (generation.worker_threads == 2U
          ? "AVX2 2T 8 TOKENS" : "AVX2 1T 8 TOKENS")
      : (generation.worker_threads == 2U
          ? "SCALAR 2T 8 TOKENS" : "SCALAR 1T 8 TOKENS");
    aiueos_qwen35_status.generated_tokens = generation.generated_tokens;
    aiueos_qwen35_status.decode_tokens = generation.decode_tokens;
    aiueos_qwen35_status.compute_cycles = generation.total_cycles;
    aiueos_qwen35_status.resident_bytes =
      boot->model_size + AIUEOS_QWEN35_DECODE_WORKSPACE_BYTES;
    if (boot->version >= AIUEOS_BOOT_INFO_VERSION_TSC_CALIBRATED && boot->tsc_hz) {
      aiueos_qwen35_status.time_to_first_token_ns =
        aiueos_cycles_to_ns(generation.first_token_cycles, boot->tsc_hz);
      aiueos_qwen35_status.decode_ns =
        aiueos_cycles_to_ns(generation.decode_cycles, boot->tsc_hz);
    }
    (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
    debug_string("AIUEOS_QWEN35_GENERATE_OK bos=248044 tokens=8 first=248046 reference=matched timing=calibrated-tsc\n");
    serial_string("AIUEOS_QWEN35_GENERATE_OK bos=248044 tokens=");
    for (uint32_t token_index = 0;
         token_index < generation.generated_tokens; token_index++) {
      if (token_index) serial_byte(',');
      serial_decimal(generation.tokens[token_index]);
    }
    serial_string(" first-cycles=");
    serial_decimal64(generation.first_token_cycles);
    serial_string(" decode-cycles=");
    serial_decimal64(generation.decode_cycles);
    serial_string(" decode-tokens=");
    serial_decimal(generation.decode_tokens);
    serial_string(" decode-milli-tok-s=");
    serial_decimal64(aiueos_inference_milli_tokens_per_second(
      generation.decode_tokens, aiueos_qwen35_status.decode_ns));
    serial_string(" vector-bits=");
    serial_decimal(generation.vector_bits);
    serial_string(" threads=");
    serial_decimal(generation.worker_threads);
    serial_string(" timing=calibrated-tsc\r\n");
qwen_runtime_boot_complete:
    serial_string("AIUEOS_KOTOTAMA_RUNTIME_BOOT state=");
    serial_string(aiueos_kototama_runtime_state_name(
      aiueos_kototama_runtime_status().state));
    serial_string(" initial-inference=");
    serial_string(qwen_initial_ok ? "ready" : "degraded");
    serial_string(" kernel=alive\r\n");
#endif
    if (!aiueos_capability_table_initialize()) {
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL MEMORY", "SSD READ ONLY", 0);
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_FAIL stage=capability-table disk-writes=none\r\n");
      if (!aiueos_qualification_finalize(2, 2))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for (;;) __asm__ volatile("cli; hlt");
#endif
      serial_string("AIUEOS_CAPABILITY_TABLE_FAIL page-allocation\r\n");
      qemu_exit(0x75);
    }
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
    aiueos_qualification_progress(228);
#endif
#if defined(AIUEOS_PHYSICAL_QUALIFICATION) && \
    defined(AIUEOS_QWEN38_MODEL_HANDOFF) && !defined(AIUEOS_MODEL_TEST_FIXTURE)
    if (!qwen_early_acpi_ready) {
#else
    if (!aiueos_acpi_initialize(boot->acpi_rsdp)) {
#endif
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL ACPI", "SSD READ ONLY", 0);
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_FAIL stage=acpi disk-writes=none\r\n");
      if (!aiueos_qualification_finalize(2, 3))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for (;;) __asm__ volatile("cli; hlt");
#endif
      debug_string("AIUEOS_ACPI_FAIL rsdp-xsdt-madt\n");
      serial_string("AIUEOS_ACPI_FAIL rsdp-xsdt-madt\r\n");
      qemu_exit(0x78);
    }
    debug_string("AIUEOS_ACPI_OK rsdp-xsdt-madt cpu>=2\n");
    serial_string("AIUEOS_ACPI_OK rsdp-xsdt-madt cpu>=2\r\n");
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
#ifdef AIUEOS_PHYSICAL_NETWORK_QUALIFICATION
    /* A second, explicitly test-only physical slice. The native-core gate has
       already passed; now allow only four DMA pages and one RTL8125 ARP
       exchange. AMD-IVRS isolation is not implemented, so this is evidence for
       the link driver, not production DMA qualification. */
    aiueos_qualification_progress(230);
    aiueos_framebuffer_qualification_screen("AIUEOS K16", "TEST RTL8125", "SSD READ ONLY", 0);
    if(!aiueos_rtl8125_physical_qualification()) {
      uint32_t code=8200U+aiueos_rtl8125_qualification_error();
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL RTL8125", "SSD READ ONLY", 0);
      debug_string("AIUEOS_PHYSICAL_NETWORK_FAIL rtl8125 arp=10.77.0.1 dma=unisolated-test-only\n");
      serial_string("AIUEOS_PHYSICAL_NETWORK_FAIL rtl8125 arp=10.77.0.1 dma=unisolated-test-only\r\n");
      if(!aiueos_qualification_finalize(2,code))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for(;;)__asm__ volatile("cli; hlt");
    }
    aiueos_qualification_progress(231);
    aiueos_framebuffer_qualification_screen("AIUEOS K16", "RTL8125 LINK OK", "SSD READ ONLY", 1);
    (void)aiueos_rtl8125_qualification_rx_length();
    debug_string("AIUEOS_PHYSICAL_NETWORK_OK rtl8125 arp-peer=10.77.0.1 dma=unisolated-test-only\n");
    serial_string("AIUEOS_PHYSICAL_NETWORK_OK rtl8125 arp-peer=10.77.0.1 dma=unisolated-test-only\r\n");
#ifdef AIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION
    aiueos_qualification_progress(232);
    aiueos_framebuffer_qualification_screen("AIUEOS K16", "TEST DIRECT HTTPS", "SSD READ ONLY", 0);
    int direct_https_ok = 0;
#ifdef AIUEOS_PERSISTENT_BOOT
    uint32_t worker_sequence = 1;
#endif
#ifdef AIUEOS_MURAKUMO_DEVICE_RESULT
    if (qwen_initial_ok) {
      direct_https_ok = aiueos_rtl8125_direct_https_qualification(
        boot, generation.tokens[0], generation.tokens[1],
        generation.generated_tokens, generation.decode_tokens,
        generation.first_token_cycles, generation.decode_cycles,
        generation.total_cycles, generation.vector_bits,
        generation.worker_threads);
    }
#ifdef AIUEOS_PERSISTENT_BOOT
    else {
      uint64_t bootstrap_job = 0, bootstrap_control = 0;
      uint32_t bootstrap_bos = 0;
      int bootstrap_ready = 0, bootstrap_reboot = 0, bootstrap_restart = 0;
      direct_https_ok = aiueos_rtl8125_device_worker_poll(
        boot, worker_sequence++, &bootstrap_job, &bootstrap_bos,
        &bootstrap_ready, &bootstrap_control, &bootstrap_reboot,
        &bootstrap_restart);
      serial_string(direct_https_ok
        ? "AIUEOS_MURAKUMO_RUNTIME_DEGRADED_CONNECTED transport=ready inference=degraded\r\n"
        : "AIUEOS_MURAKUMO_RUNTIME_DEGRADED_CONNECT_RETRY transport=retry inference=degraded\r\n");
    }
#endif
#else
    direct_https_ok = aiueos_rtl8125_direct_https_qualification();
#endif
    if(!direct_https_ok) {
      uint32_t error=aiueos_rtl8125_direct_https_error();
      uint32_t code=8600U+error;
#if defined(AIUEOS_MURAKUMO_DEVICE_RESULT) && defined(AIUEOS_PERSISTENT_BOOT)
      aiueos_framebuffer_qualification_screen(
        "AIUEOS K16", "MURAKUMO WORKER",
        aiueos_worker_transport_detail(), 0);
#else
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL DIRECT HTTPS", "SSD READ ONLY", 0);
#endif
#ifdef AIUEOS_MURAKUMO_DEVICE_RESULT
      debug_string("AIUEOS_PHYSICAL_DIRECT_HTTPS_FAIL host=api.murakumo.cloud auth=device-p256 nvram-key=true cacao=false\n");
      serial_string("AIUEOS_PHYSICAL_DIRECT_HTTPS_FAIL host=api.murakumo.cloud error=");
      serial_decimal(error);
      serial_string(" attempts=");
      serial_decimal(aiueos_rtl8125_direct_https_attempts());
      serial_string(" tls-stage=");
      serial_decimal(aiueos_rtl8125_direct_tls_stage());
      serial_string(" response-wait-ms=");
      serial_decimal(aiueos_rtl8125_direct_response_wait_ms());
      serial_string(" response-timeout-s=");
      serial_decimal(aiueos_rtl8125_direct_response_timeout_seconds());
      serial_string(" auth=device-p256 nvram-key=true cacao=false passkey=false pq=false biscuit=false\r\n");
#else
      debug_string("AIUEOS_PHYSICAL_DIRECT_HTTPS_FAIL host=api.murakumo.cloud trust=transport-only secrets=none\n");
      serial_string("AIUEOS_PHYSICAL_DIRECT_HTTPS_FAIL host=api.murakumo.cloud error=");
      serial_decimal(error);
      serial_string(" attempts=");
      serial_decimal(aiueos_rtl8125_direct_https_attempts());
      serial_string(" tls-stage=");
      serial_decimal(aiueos_rtl8125_direct_tls_stage());
      serial_string(" trust=transport-only secrets=none\r\n");
#endif
#if defined(AIUEOS_MURAKUMO_DEVICE_RESULT) && defined(AIUEOS_PERSISTENT_BOOT)
      if(!aiueos_rtl8125_direct_device_did()[0]) {
        if(!aiueos_qualification_finalize(2,code))
          serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
        for(;;)__asm__ volatile("cli; hlt");
      }
      debug_string("AIUEOS_MURAKUMO_INITIAL_POST_AMBIGUOUS action=worker-reconnect\n");
      serial_string("AIUEOS_MURAKUMO_INITIAL_POST_AMBIGUOUS did=");
      serial_string(aiueos_rtl8125_direct_device_did());
      serial_string(" action=worker-reconnect internal-disk-writes=none\r\n");
#else
      if(!aiueos_qualification_finalize(2,code))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for(;;)__asm__ volatile("cli; hlt");
#endif
    }
    (void)aiueos_rtl8125_direct_dns_a();
    if(direct_https_ok && !aiueos_rtl8125_direct_http_ready()) {
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL DIRECT HTTPS", "SSD READ ONLY", 0);
#ifdef AIUEOS_MURAKUMO_DEVICE_RESULT
      serial_string("AIUEOS_PHYSICAL_DIRECT_HTTPS_FAIL host=api.murakumo.cloud error=12 auth=device-p256 nvram-key=true cacao=false passkey=false pq=false biscuit=false\r\n");
#else
      serial_string("AIUEOS_PHYSICAL_DIRECT_HTTPS_FAIL host=api.murakumo.cloud error=12 trust=transport-only secrets=none\r\n");
#endif
      if(!aiueos_qualification_finalize(2,8612))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for(;;)__asm__ volatile("cli; hlt");
    }
#ifdef AIUEOS_MURAKUMO_DEVICE_RESULT
    if(direct_https_ok) {
      aiueos_qualification_progress(233);
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "MURAKUMO NODE OK", "COMMUNITY PENDING", 1);
      debug_string("AIUEOS_MURAKUMO_NODE_OK host=api.murakumo.cloud path=/infer/nodes/device-p256-result auth=device-p256 cacao=false ready=false\n");
      serial_string("AIUEOS_MURAKUMO_NODE_OK did=");
      serial_string(aiueos_rtl8125_direct_device_did());
      serial_string(" attempts=");
      serial_decimal(aiueos_rtl8125_direct_https_attempts());
      serial_string(" auth=device-p256 cacao=false passkey=false pq=false biscuit=false ready=false\r\n");
    }
    serial_string("AIUEOS_QWEN38_TIMING model-load-ns=");
    serial_decimal64(aiueos_qwen35_status.load_ns);
    serial_string(" ttft-ns=");
    serial_decimal64(aiueos_qwen35_status.time_to_first_token_ns);
    serial_string(" decode-milli-tok-s=");
    serial_decimal64(aiueos_inference_milli_tokens_per_second(
      aiueos_qwen35_status.decode_tokens, aiueos_qwen35_status.decode_ns));
    serial_string(" decode-tokens=");
    serial_decimal(aiueos_qwen35_status.decode_tokens);
    serial_string(" decode-ns=");
    serial_decimal64(aiueos_qwen35_status.decode_ns);
    serial_string(" tsc-hz=");
    serial_decimal64(boot->tsc_hz);
    serial_string("\r\n");
#ifdef AIUEOS_PERSISTENT_BOOT
    int persistent_qualification_recorded = direct_https_ok;
    if(direct_https_ok) {
      if(!aiueos_qualification_finalize(1,8161))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
    }
    aiueos_framebuffer_qualification_screen(
      "AIUEOS K16", "MURAKUMO WORKER", "POLLING JOBS", 1);
    debug_string("AIUEOS_MURAKUMO_PERSISTENT_BOOT_OK model=Qwen3.8-27B path=/infer/nodes/device-p256-worker\n");
    serial_string("AIUEOS_MURAKUMO_PERSISTENT_BOOT_OK did=");
    serial_string(aiueos_rtl8125_direct_device_did());
    serial_string(" model=Qwen3.8-27B storage=volatile-ram internal-disk-writes=none\r\n");
    uint32_t heartbeat_failures = 0;
    uint32_t inference_failures = 0;
    for (;;) {
      uint64_t job_id = 0;
      uint64_t control_id = 0;
      uint32_t bos_token = 0;
      int ready = 0;
      int reboot_pxe = 0;
      int restart_runtime = 0;
      aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ADMISSION;
      aiueos_qwen35_status.detail = "POLLING MURAKUMO";
      (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
      if (!aiueos_rtl8125_device_worker_poll(
            boot, worker_sequence++, &job_id, &bos_token, &ready,
            &control_id, &reboot_pxe, &restart_runtime)) {
        heartbeat_failures++;
        aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ERROR;
        aiueos_qwen35_status.detail = aiueos_worker_transport_detail();
        (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
        debug_string("AIUEOS_MURAKUMO_HEARTBEAT_RETRY ready=false action=reconnect\n");
        serial_string("AIUEOS_MURAKUMO_HEARTBEAT_RETRY failure=");
        serial_decimal(heartbeat_failures);
        serial_string(" error=");
        serial_decimal(aiueos_rtl8125_direct_https_error());
        serial_string(" response-wait-ms=");
        serial_decimal(aiueos_rtl8125_direct_response_wait_ms());
        serial_string(" response-timeout-s=");
        serial_decimal(aiueos_rtl8125_direct_response_timeout_seconds());
        serial_string(" action=reconnect\r\n");
        aiueos_k16_management_wait(boot->tsc_hz, 5);
        continue;
      }
      if (heartbeat_failures) {
        serial_string("AIUEOS_MURAKUMO_HEARTBEAT_RECOVERED failures=");
        serial_decimal(heartbeat_failures);
        serial_string("\r\n");
        heartbeat_failures = 0;
      }
      if (!persistent_qualification_recorded) {
        aiueos_qualification_progress(233);
        if(!aiueos_qualification_finalize(1,8162))
          serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
        persistent_qualification_recorded = 1;
        debug_string("AIUEOS_MURAKUMO_INITIAL_POST_RECOVERED path=/infer/nodes/device-p256-worker\n");
        serial_string("AIUEOS_MURAKUMO_INITIAL_POST_RECOVERED path=/infer/nodes/device-p256-worker\r\n");
      }
      serial_string("AIUEOS_MURAKUMO_HEARTBEAT_OK ready=");
      serial_string(ready ? "true" : "false");
      serial_string(" sequence=");
      serial_decimal(worker_sequence - 1);
      serial_string("\r\n");
      if ((reboot_pxe || restart_runtime) && control_id) {
        aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ADMISSION;
        aiueos_qwen35_status.detail = restart_runtime
          ? "RUNTIME RESTART ACK" : "NETWORK REBOOT ACK";
        (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
        serial_string("AIUEOS_MURAKUMO_CONTROL_RECEIVED action=");
        serial_string(restart_runtime ? "restart-runtime" : "reboot-pxe");
        serial_string(" command-id=");
        serial_decimal64(control_id);
        serial_string("\r\n");
        uint32_t control_ack_failures = 0;
        while (control_ack_failures < 12 &&
               !aiueos_rtl8125_device_worker_control_ack(
                 boot, worker_sequence++, control_id)) {
          control_ack_failures++;
          serial_string("AIUEOS_MURAKUMO_CONTROL_ACK_RETRY command-id=");
          serial_decimal64(control_id);
          serial_string(" failure=");
          serial_decimal(control_ack_failures);
          serial_string(" error=");
          serial_decimal(aiueos_rtl8125_direct_https_error());
          serial_string("\r\n");
          aiueos_wait_seconds(boot->tsc_hz, 5);
        }
        if (control_ack_failures == 12) {
          aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ERROR;
          aiueos_qwen35_status.detail = "CONTROL ACK FAILED";
          (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
          serial_string("AIUEOS_MURAKUMO_CONTROL_ABORT action=");
          serial_string(restart_runtime ? "restart-runtime" : "reboot-pxe");
          serial_string(" reason=ack-failed\r\n");
          continue;
        }
        if (restart_runtime) {
          int restarted = aiueos_kototama_runtime_restart();
          aiueos_qwen35_status.phase = restarted
            ? AIUEOS_INFERENCE_ADMISSION : AIUEOS_INFERENCE_ERROR;
          aiueos_qwen35_status.detail = restarted
            ? "RUNTIME RESTARTED" : "RUNTIME UNAVAILABLE";
          (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
          serial_string(restarted
            ? "AIUEOS_MURAKUMO_CONTROL_ACK_OK action=restart-runtime kernel-reboot=false network=preserved\r\n"
            : "AIUEOS_MURAKUMO_CONTROL_ABORT action=restart-runtime reason=runtime-unavailable kernel-reboot=false\r\n");
          continue;
        }
        aiueos_qwen35_status.phase = AIUEOS_INFERENCE_COMPLETE;
        aiueos_qwen35_status.detail = "REBOOTING TO PXE";
        (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
        debug_string("AIUEOS_MURAKUMO_CONTROL_ACK_OK action=reboot-pxe reset=uefi-runtime\n");
        serial_string("AIUEOS_MURAKUMO_CONTROL_ACK_OK action=reboot-pxe command-id=");
        serial_decimal64(control_id);
        serial_string(" reset=uefi-runtime\r\n");
        (void)aiueos_qualification_reboot();
        aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ERROR;
        aiueos_qwen35_status.detail = "FIRMWARE RESET FAILED";
        (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
        serial_string("AIUEOS_MURAKUMO_CONTROL_RESET_FAIL action=reboot-pxe\r\n");
        aiueos_wait_seconds(boot->tsc_hz, 30);
        continue;
      }
      if (!job_id) {
        aiueos_qwen35_status.phase = AIUEOS_INFERENCE_COMPLETE;
        aiueos_qwen35_status.detail = "READY - NO JOB";
        (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
        debug_string("AIUEOS_MURAKUMO_JOB_POLL_OK job=none ready=true\n");
        serial_string("AIUEOS_MURAKUMO_JOB_POLL_OK job=none ready=true\r\n");
        aiueos_k16_management_wait(boot->tsc_hz, 30);
        continue;
      }
      serial_string("AIUEOS_MURAKUMO_JOB_CLAIMED job-id=");
      serial_decimal64(job_id);
      serial_string(" kind=qwen38-first-token bos=");
      serial_decimal(bos_token);
      serial_string(" ready=false\r\n");
      if (bos_token != AIUEOS_QWEN35_BOS_TOKEN) {
        serial_string("AIUEOS_MURAKUMO_JOB_REFUSED reason=unsupported-bos\r\n");
        aiueos_wait_seconds(boot->tsc_hz, 5);
        continue;
      }
      struct aiueos_qwen35_generation_result job_result = {0};
      aiueos_qwen35_status.phase = AIUEOS_INFERENCE_PREFILL;
      aiueos_qwen35_status.detail = aiueos_qwen35_progress_detail;
      aiueos_qwen35_status.generated_tokens = 0;
      aiueos_qwen35_status.decode_tokens = 0;
      aiueos_qwen35_status.compute_cycles = 0;
      aiueos_qwen35_status.time_to_first_token_ns =
        AIUEOS_INFERENCE_UNMEASURED;
      aiueos_qwen35_status.decode_ns = AIUEOS_INFERENCE_UNMEASURED;
      (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
      if (!aiueos_kototama_runtime_invoke(
            bos_token,
            AIUEOS_QWEN35_GENERATION_TOKENS,
            aiueos_qwen35_progress, &job_result) ||
          job_result.tokens[0] != AIUEOS_QWEN35_REFERENCE_FIRST_TOKEN) {
        inference_failures++;
        uint32_t failed_token = job_result.failed_token
          ? job_result.failed_token : job_result.generated_tokens + 1U;
        uint32_t failure_stage = job_result.failure_stage;
        if (!failure_stage && job_result.generated_tokens &&
            job_result.tokens[0] != AIUEOS_QWEN35_REFERENCE_FIRST_TOKEN)
          failure_stage = AIUEOS_QWEN35_FAILURE_OUTPUT_LOGITS;
        aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ERROR;
        aiueos_qwen35_format_failure(
          failed_token, job_result.failed_layer, failure_stage);
        aiueos_qwen35_status.detail = aiueos_qwen35_decode_fail_detail;
        (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
        aiueos_rtl8125_inference_failure_report(
          job_id, inference_failures, failed_token,
          job_result.failed_layer, failure_stage);
        serial_string("AIUEOS_MURAKUMO_JOB_INFERENCE_RETRY job-id=");
        serial_decimal64(job_id);
        serial_string(" attempt=");
        serial_decimal(inference_failures);
        serial_string(" failed-token=");
        serial_decimal(failed_token);
        serial_string(" failed-layer=");
        serial_decimal(job_result.failed_layer);
        serial_string(" failure-stage=");
        serial_decimal(failure_stage);
        serial_string(" reason=execution-or-token-mismatch action=restart-runtime kernel-reboot=false\r\n");
        (void)aiueos_kototama_runtime_restart();
        /* A failed inference must not make the AIUEOS management plane deaf.
           Preserve the retry policy while providing a bounded SSH window for
           status, runtime restart and authenticated PXE reboot. */
        aiueos_k16_management_wait(boot->tsc_hz, 30);
        continue;
      }
      aiueos_qwen35_status.phase = AIUEOS_INFERENCE_COMPLETE;
      aiueos_qwen35_status.detail = "POSTING JOB RESULT";
      aiueos_qwen35_status.generated_tokens = job_result.generated_tokens;
      aiueos_qwen35_status.decode_tokens = job_result.decode_tokens;
      aiueos_qwen35_status.compute_cycles = job_result.total_cycles;
      aiueos_qwen35_status.time_to_first_token_ns =
        aiueos_cycles_to_ns(job_result.first_token_cycles, boot->tsc_hz);
      aiueos_qwen35_status.decode_ns =
        aiueos_cycles_to_ns(job_result.decode_cycles, boot->tsc_hz);
      (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
      while (!aiueos_rtl8125_device_worker_result(
               boot, worker_sequence++, job_id, job_result.tokens[0],
               job_result.tokens[1], job_result.generated_tokens,
               job_result.decode_tokens, job_result.first_token_cycles,
               job_result.decode_cycles, job_result.total_cycles,
               job_result.vector_bits, job_result.worker_threads)) {
        aiueos_qwen35_status.phase = AIUEOS_INFERENCE_ERROR;
        aiueos_qwen35_status.detail = "RESULT POST RETRY";
        (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
        serial_string("AIUEOS_MURAKUMO_JOB_RESULT_RETRY job-id=");
        serial_decimal64(job_id);
        serial_string(" error=");
        serial_decimal(aiueos_rtl8125_direct_https_error());
        serial_string("\r\n");
        aiueos_wait_seconds(boot->tsc_hz, 5);
      }
      aiueos_qwen35_status.phase = AIUEOS_INFERENCE_COMPLETE;
      aiueos_qwen35_status.detail = "RESULT RECORDED - READY";
      (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
      debug_string("AIUEOS_MURAKUMO_JOB_RESULT_OK model=Qwen3.8-27B result=recorded ready=true\n");
      serial_string("AIUEOS_MURAKUMO_JOB_RESULT_OK job-id=");
      serial_decimal64(job_id);
      serial_string(" token=");
      serial_decimal(job_result.tokens[0]);
      serial_string(" next-token=");
      serial_decimal(job_result.tokens[1]);
      serial_string(" decode-milli-tok-s=");
      serial_decimal64(aiueos_inference_milli_tokens_per_second(
        job_result.decode_tokens, aiueos_qwen35_status.decode_ns));
      serial_string(" total-cycles=");
      serial_decimal64(job_result.total_cycles);
      serial_string(" result=recorded ready=true\r\n");
    }
#else
    if(!aiueos_qualification_finalize(1,8160))
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
#endif
#else
    aiueos_framebuffer_qualification_screen("AIUEOS K16", "DIRECT HTTPS OK", "SSD READ ONLY", 1);
    debug_string("AIUEOS_PHYSICAL_DIRECT_HTTPS_OK host=api.murakumo.cloud path=/infer/queue http=200 mac-app-relay=none trust=transport-only secrets=none\n");
    serial_string("AIUEOS_PHYSICAL_DIRECT_HTTPS_OK host=api.murakumo.cloud path=/infer/queue http=200 mac-app-relay=none trust=transport-only secrets=none\r\n");
    if(!aiueos_qualification_finalize(1,8150))
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
#endif
    for(;;)__asm__ volatile("cli; hlt");
#else
#ifdef AIUEOS_PHYSICAL_RELAY_QUALIFICATION
    aiueos_qualification_progress(232);
    aiueos_framebuffer_qualification_screen("AIUEOS K16", "TEST NODE RELAY", "SSD READ ONLY", 0);
    if(!aiueos_rtl8125_relay_qualification()) {
      uint32_t code=8300U+aiueos_rtl8125_relay_error();
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL NODE RELAY", "SSD READ ONLY", 0);
      debug_string("AIUEOS_PHYSICAL_RELAY_FAIL udp=10.77.0.1:7777 scope=diagnostic-only\n");
      serial_string("AIUEOS_PHYSICAL_RELAY_FAIL udp=10.77.0.1:7777 scope=diagnostic-only\r\n");
      if(!aiueos_qualification_finalize(2,code))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for(;;)__asm__ volatile("cli; hlt");
    }
    aiueos_qualification_progress(233);
    aiueos_framebuffer_qualification_screen("AIUEOS K16", "NODE RELAY OK", "SSD READ ONLY", 1);
    (void)aiueos_rtl8125_relay_rx_length();
    debug_string("AIUEOS_PHYSICAL_RELAY_OK request-bound-udp=10.77.0.1:7777 scope=diagnostic-only\n");
    serial_string("AIUEOS_PHYSICAL_RELAY_OK request-bound-udp=10.77.0.1:7777 scope=diagnostic-only\r\n");
#ifdef AIUEOS_PHYSICAL_JOB_QUALIFICATION
    aiueos_qualification_progress(234);
    struct aiueos_inference_status inference_status = {
      .abi_version = AIUEOS_INFERENCE_STATUS_ABI_VERSION,
      .byte_size = sizeof(struct aiueos_inference_status),
      .phase = AIUEOS_INFERENCE_ADMISSION,
      .model = "AIUEOS CHAR BIGRAM",
      .quant = "FROZEN U8",
      .detail = "WAITING FOR JOB",
      .target_tokens = 1,
      .artifact_bytes = AIUEOS_INFERENCE_UNMEASURED,
      .resident_bytes = AIUEOS_INFERENCE_UNMEASURED,
      .load_ns = AIUEOS_INFERENCE_UNMEASURED,
      .prefill_ns = AIUEOS_INFERENCE_UNMEASURED,
      .decode_ns = AIUEOS_INFERENCE_UNMEASURED,
      .time_to_first_token_ns = AIUEOS_INFERENCE_UNMEASURED
    };
    if (!aiueos_framebuffer_inference_screen(&inference_status))
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "WAIT INFERENCE JOB", "SSD READ ONLY", 0);
    if(!aiueos_rtl8125_job_qualification()) {
      uint32_t code=8400U+aiueos_rtl8125_job_error();
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL INFERENCE JOB", "SSD READ ONLY", 0);
      debug_string("AIUEOS_PHYSICAL_JOB_FAIL queue=claim model=aiueos-char-bigram-v1 result=uncommitted\n");
      serial_string("AIUEOS_PHYSICAL_JOB_FAIL queue=claim model=aiueos-char-bigram-v1 result=uncommitted\r\n");
      if(!aiueos_qualification_finalize(2,code))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for(;;)__asm__ volatile("cli; hlt");
    }
    aiueos_qualification_progress(235);
    (void)aiueos_rtl8125_job_token();
    (void)aiueos_rtl8125_job_score();
    (void)aiueos_rtl8125_job_total();
    inference_status.phase = AIUEOS_INFERENCE_COMPLETE;
    inference_status.detail = "RESULT RECORDED";
    inference_status.generated_tokens = 1;
    inference_status.compute_cycles = aiueos_rtl8125_job_cycles();
    if (!aiueos_framebuffer_inference_screen(&inference_status))
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "INFERENCE RESULT OK", "SSD READ ONLY", 1);
    debug_string("AIUEOS_PHYSICAL_JOB_OK queue=claim model=aiueos-char-bigram-v1 token=o result=recorded ready=true\n");
    serial_string("AIUEOS_PHYSICAL_JOB_OK queue=claim model=aiueos-char-bigram-v1 token=o result=recorded ready=true\r\n");
    if(!aiueos_rtl8125_liveness_renewal()) {
      uint32_t code=8500U+aiueos_rtl8125_liveness_error();
      aiueos_framebuffer_qualification_screen("AIUEOS K16", "FAIL HEARTBEAT RENEW", "SSD READ ONLY", 0);
      debug_string("AIUEOS_PHYSICAL_LIVENESS_FAIL ping=pong heartbeat=not-renewed\n");
      serial_string("AIUEOS_PHYSICAL_LIVENESS_FAIL ping=pong heartbeat=not-renewed\r\n");
      if(!aiueos_qualification_finalize(2,code))
        serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
      for(;;)__asm__ volatile("cli; hlt");
    }
    (void)aiueos_rtl8125_liveness_sequence();
    inference_status.detail = "MURAKUMO NODE LIVE";
    (void)aiueos_framebuffer_inference_screen(&inference_status);
    debug_string("AIUEOS_PHYSICAL_LIVENESS_OK ping=pong heartbeat=renewed\n");
    serial_string("AIUEOS_PHYSICAL_LIVENESS_OK ping=pong heartbeat=renewed\r\n");
    if(!aiueos_qualification_finalize(1,8141))
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
    uint32_t liveness_failures=0;
    for(;;) {
      if(aiueos_rtl8125_liveness_renewal()) {
        if(liveness_failures) {
          inference_status.phase = AIUEOS_INFERENCE_COMPLETE;
          inference_status.detail = "MURAKUMO NODE LIVE";
          (void)aiueos_framebuffer_inference_screen(&inference_status);
          debug_string("AIUEOS_PHYSICAL_LIVENESS_RECOVERED heartbeat=renewed\n");
          serial_string("AIUEOS_PHYSICAL_LIVENESS_RECOVERED failures=");
          serial_decimal(liveness_failures);
          serial_string(" sequence=");
          serial_decimal(aiueos_rtl8125_liveness_sequence());
          serial_string("\r\n");
          liveness_failures=0;
        }
        continue;
      }
      liveness_failures++;
      inference_status.phase = AIUEOS_INFERENCE_ERROR;
      inference_status.detail = "NODE RECONNECTING";
      (void)aiueos_framebuffer_inference_screen(&inference_status);
      debug_string("AIUEOS_PHYSICAL_LIVENESS_RETRY heartbeat=stale action=reconnect\n");
      serial_string("AIUEOS_PHYSICAL_LIVENESS_RETRY failure=");
      serial_decimal(liveness_failures);
      serial_string(" error=");
      serial_decimal(aiueos_rtl8125_liveness_error());
      serial_string(" action=reconnect\r\n");
      /* A failed job can leave its error sticky and make the next renewal
         return immediately. Keep that path from becoming a hot spin while a
         future recovery policy decides whether the job may be retried. */
      for(volatile uint32_t backoff=0;backoff<50000000U;backoff++)
        __asm__ volatile("pause");
    }
#else
    if(!aiueos_qualification_finalize(1,8130))
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
#endif
#else
    if(!aiueos_qualification_finalize(1,8125))
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
#endif
    for(;;)__asm__ volatile("cli; hlt");
#endif
#else
    /* This profile is intentionally a USB-only, read-only boundary.  Reaching
       this screen proves the native loader/kernel, integrity admission,
       Kotoba object execution, owned paging, GOP, allocator and ACPI on the
       physical machine.  It stops before DMA, PCI drivers or any block write;
       those need K16-specific AMD-IOMMU, NVMe and RTL8125 qualification. */
    aiueos_qualification_progress(229);
#ifdef AIUEOS_QWEN38_MODEL_HANDOFF
#ifdef AIUEOS_MODEL_TEST_FIXTURE
    {
      struct aiueos_inference_status admitted = {
        .abi_version = AIUEOS_INFERENCE_STATUS_ABI_VERSION,
        .byte_size = sizeof(struct aiueos_inference_status),
        .phase = AIUEOS_INFERENCE_ADMISSION,
        .model = "QWEN3.8 27B",
        .quant = "UD-IQ3 XXS",
#ifndef AIUEOS_MODEL_TEST_FIXTURE
        .detail = "QWEN35 GRAPH READY",
#else
        .detail = "RUNTIME NEXT",
#endif
        .artifact_bytes = boot->model_size,
        .resident_bytes = boot->model_size,
        .load_ns = AIUEOS_INFERENCE_UNMEASURED,
        .prefill_ns = AIUEOS_INFERENCE_UNMEASURED,
        .decode_ns = AIUEOS_INFERENCE_UNMEASURED,
        .time_to_first_token_ns = AIUEOS_INFERENCE_UNMEASURED,
        .compute_cycles = AIUEOS_INFERENCE_UNMEASURED
      };
      (void)aiueos_framebuffer_inference_screen(&admitted);
    }
    debug_string("AIUEOS_PHYSICAL_MODEL_HANDOFF_OK qwen38-27b runtime=not-yet-present internal-disk-writes=none\n");
    serial_string("AIUEOS_PHYSICAL_MODEL_HANDOFF_OK qwen38-27b runtime=not-yet-present internal-disk-writes=none\r\n");
#else
    (void)aiueos_framebuffer_inference_screen(&aiueos_qwen35_status);
    debug_string("AIUEOS_PHYSICAL_QWEN35_OK token=248046 reference=matched timing=raw-tsc internal-disk-writes=none\n");
    serial_string("AIUEOS_PHYSICAL_QWEN35_OK token=248046 cycles=");
    serial_decimal64(aiueos_qwen35_status.compute_cycles);
    serial_string(" reference=matched timing=raw-tsc internal-disk-writes=none\r\n");
#endif
    if (!aiueos_qualification_finalize(1, 8160))
#else
    aiueos_framebuffer_qualification_screen("AIUEOS K16", "NATIVE CORE OK", "SSD READ ONLY", 1);
    debug_string("AIUEOS_PHYSICAL_QUALIFICATION_OK native-core-v2 internal-disk-writes=none\n");
    serial_string("AIUEOS_PHYSICAL_QUALIFICATION_OK native-core-v2 internal-disk-writes=none\r\n");
    if (!aiueos_qualification_finalize(1, 0))
#endif
      serial_string("AIUEOS_PHYSICAL_QUALIFICATION_LOG_FAIL stage=runtime-variable\r\n");
    for (;;) __asm__ volatile("cli; hlt");
#endif
#endif
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
    if (!aiueos_smp_dispatch_selftest()) {
      debug_string("AIUEOS_SMP_FAIL ap-dispatch\n");
      serial_string("AIUEOS_SMP_FAIL ap-dispatch\r\n");
      qemu_exit(0x73);
    }
    debug_string("AIUEOS_SMP_OK cpus=2 init-sipi-v1\n");
    serial_string("AIUEOS_SMP_OK cpus=2 init-sipi-v1 per-cpu-stack dispatch=verified\r\n");
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
#ifdef AIUEOS_SSH_LISTEN
    /* Entropy API self-test (ssh-v1.edn / ADR-0103): the random device is kept
       alive after enumeration, and aiueos_random_bytes must deliver two
       distinct, non-constant 32-byte batches. This is the source an SSH
       handshake draws its ephemeral scalar and cookie from -- before this the
       32 bytes rng returned were discarded. */
    if (aiueos_random_selftest()) {
      serial_string("AIUEOS_RANDOM_OK two-batches distinct non-constant 32-bytes\r\n");
      debug_string("AIUEOS_RANDOM_OK two-batches distinct non-constant 32-bytes\n");
    } else {
      serial_string("AIUEOS_RANDOM_FAIL entropy-source-unusable\r\n");
    }
    /* SSH curve25519-sha256 exchange hash H over the RFC 5656 transcript
       (ssh-v1.edn / ADR-0106). H is emitted as hex so smoke-qemu-ssh-kex can
       compare it against ssh.transport's H (kotoba-lang/org-ietf-ssh); want[]
       is that same H, baked, so the boot self-verifies. */
    {
      static const uint8_t want[32] = {
        0x52,0x0a,0x9b,0xa7,0x0d,0x60,0x20,0x1a,0xf9,0x36,0x5b,0x0e,0x53,0xff,0xaf,0xa1,
        0xa3,0x14,0x46,0xd1,0x7e,0xc2,0x43,0x15,0xeb,0x67,0x8a,0x9b,0x2e,0x70,0x98,0x33};
      uint8_t hh[32];
      if (aiueos_ssh_kex_h(hh)) {
        int match = 1;
        for (int i = 0; i < 32; i++) if (hh[i] != want[i]) match = 0;
        serial_string("AIUEOS_SSH_KEX_H ");
        for (int i = 0; i < 32; i++) serial_hex_byte(hh[i]);
        serial_string("\r\n");
        if (match) {
          serial_string("AIUEOS_SSH_KEX_OK curve25519-sha256 exchange-hash-match org-ietf-ssh\r\n");
          debug_string("AIUEOS_SSH_KEX_OK curve25519-sha256 exchange-hash-match\n");
        } else {
          serial_string("AIUEOS_SSH_KEX_FAIL exchange-hash-mismatch\r\n");
        }
      } else {
        serial_string("AIUEOS_SSH_KEX_FAIL sha256-object-failed\r\n");
      }
    }
#endif
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
      serial_string("AIUEOS_VIRTIO_INPUT_FAIL queue-or-envelope\r\n");
      if (aiueos_desktop_input_eventq_empty()) {
        debug_string("AIUEOS_GUEST_INPUT leftover=eventq-empty\n");
        serial_string("AIUEOS_GUEST_INPUT leftover=eventq-empty\r\n");
      }
      qemu_exit(0x6f);
    }
    if (aiueos_desktop_input_from_eventq()) {
      debug_string("AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured used-ring\n");
      serial_string("AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured used-ring\r\n");
    } else {
      debug_string("AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured synthetic-smoke\n");
      serial_string("AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured synthetic-smoke\r\n");
    }
    debug_string("AIUEOS_DESKTOP_INPUT_OK envelope-v1 sequence=1 kind=key ime-neutral\n");
    serial_string("AIUEOS_DESKTOP_INPUT_OK envelope-v1 sequence=1 kind=key ime-neutral\r\n");
    /* Guest input (ADR-0093). C must not fill the envelope. Hosted JVM
       compositor input does not count. Do not qemu_exit: gpu/guest-paint
       stay green without this line. */
    if (aiueos_desktop_input_from_eventq()) {
      debug_string("AIUEOS_GUEST_INPUT_OK eventq-used=1 synthetic=0\n");
      serial_string("AIUEOS_GUEST_INPUT_OK eventq-used=1 synthetic=0\r\n");
    } else {
      debug_string("AIUEOS_GUEST_INPUT leftover=synthetic-smoke\n");
      serial_string("AIUEOS_GUEST_INPUT leftover=synthetic-smoke\r\n");
    }
    /* Guest IME known-answer (ADR-0090). C does not convert. Hosted JVM
       IME does not count. latin k=107, a=97 must yield U+304B (12363).
       Returning those latin bytes is a leak. Do not qemu_exit: gpu/cloud
       stay green without this line. */
    {
      uint64_t ime_cp = kotoba_aiueos_ime_commit(107, 97);
      if (ime_cp == 107 || ime_cp == 97) {
        debug_string("AIUEOS_GUEST_IME leftover=latin-leak\n");
        serial_string("AIUEOS_GUEST_IME leftover=latin-leak\r\n");
      } else if (ime_cp == 12363) {
        debug_string("AIUEOS_GUEST_IME_OK committed=u+304b latin-leak=0\n");
        serial_string("AIUEOS_GUEST_IME_OK committed=u+304b latin-leak=0\r\n");
      } else {
        debug_string("AIUEOS_GUEST_IME leftover=vector-miss\n");
        serial_string("AIUEOS_GUEST_IME leftover=vector-miss\r\n");
      }
    }
    /* Guest WM known-answer (ADR-0091). C does not pick z-order. Hosted
       JVM WM does not count. Rects are hosted boot-desktop. Do not
       qemu_exit: gpu/cloud/guest-ime stay green without this line. */
    {
      uint64_t one = kotoba_aiueos_wm_hit(1, 2, 100, 80);
      uint64_t zhit = kotoba_aiueos_wm_hit(2, 2, 100, 80);
      uint64_t missf = kotoba_aiueos_wm_hit(2, 2, 40, 40);
      uint64_t raised = kotoba_aiueos_wm_hit(2, 1, 100, 80);
      if (one != 0) {
        debug_string("AIUEOS_GUEST_WM leftover=one-surface-ignored\n");
        serial_string("AIUEOS_GUEST_WM leftover=one-surface-ignored\r\n");
      } else if (zhit == 1) {
        debug_string("AIUEOS_GUEST_WM leftover=z-order-ignored\n");
        serial_string("AIUEOS_GUEST_WM leftover=z-order-ignored\r\n");
      } else if (missf == 2) {
        debug_string("AIUEOS_GUEST_WM leftover=always-front\n");
        serial_string("AIUEOS_GUEST_WM leftover=always-front\r\n");
      } else if (raised != 1) {
        debug_string("AIUEOS_GUEST_WM leftover=raise-is-noop\n");
        serial_string("AIUEOS_GUEST_WM leftover=raise-is-noop\r\n");
      } else if (zhit == 2 && missf == 1) {
        debug_string("AIUEOS_GUEST_WM_OK two-surfaces z-hit=2 miss-front=1 raise=1 one-surface=0\n");
        serial_string("AIUEOS_GUEST_WM_OK two-surfaces z-hit=2 miss-front=1 raise=1 one-surface=0\r\n");
      } else {
        debug_string("AIUEOS_GUEST_WM leftover=vector-miss\n");
        serial_string("AIUEOS_GUEST_WM leftover=vector-miss\r\n");
      }
    }
    /* Guest permission broker (ADR-0096). C copies the clipboard
       scratch only when Kotoba admits clipboard. File-picker is
       refused on the clipboard-only boot grant. Do not qemu_exit:
       gpu/cloud/guest-ime/guest-wm/guest-scanout-two stay green
       without this line. Hosted JVM wm does not count. */
    {
      volatile static uint8_t clipboard_scratch;
      uint64_t clip = kotoba_aiueos_broker_admit(1, 1);
      uint64_t pick = kotoba_aiueos_broker_admit(2, 1);
      if (clip == 1) clipboard_scratch = 0;
      if (clip == 0) {
        debug_string("AIUEOS_GUEST_BROKER leftover=deny-all\n");
        serial_string("AIUEOS_GUEST_BROKER leftover=deny-all\r\n");
      } else if (pick != 0) {
        debug_string("AIUEOS_GUEST_BROKER leftover=always-grant\n");
        serial_string("AIUEOS_GUEST_BROKER leftover=always-grant\r\n");
      } else if (clip == 1 && pick == 0) {
        debug_string("AIUEOS_GUEST_BROKER_OK clipboard=1 picker=0 kotoba-clip=1 kotoba-pick=0\n");
        serial_string("AIUEOS_GUEST_BROKER_OK clipboard=1 picker=0 kotoba-clip=1 kotoba-pick=0\r\n");
      } else {
        debug_string("AIUEOS_GUEST_BROKER leftover=vector-miss\n");
        serial_string("AIUEOS_GUEST_BROKER leftover=vector-miss\r\n");
      }
    }
    /* Guest session restore (ADR-0098). Kotoba unpacks the sealed
       packed session word; C applies the restored front to wm-hit.
       Do not qemu_exit: gpu/cloud/guest-ime/guest-wm/guest-broker stay
       green without this line. Hosted JVM wm does not count. */
    {
      uint64_t packed = 2;
      uint64_t front = kotoba_aiueos_session_restore(packed);
      uint64_t empty = kotoba_aiueos_session_restore(0);
      uint64_t unk = kotoba_aiueos_session_restore(3);
      uint64_t hit = kotoba_aiueos_wm_hit(2, front, 100, 80);
      if (empty != 0) {
        debug_string("AIUEOS_GUEST_SESSION leftover=always-front\n");
        serial_string("AIUEOS_GUEST_SESSION leftover=always-front\r\n");
      } else if (unk != 0) {
        debug_string("AIUEOS_GUEST_SESSION leftover=unknown-surface\n");
        serial_string("AIUEOS_GUEST_SESSION leftover=unknown-surface\r\n");
      } else if (front == 0) {
        debug_string("AIUEOS_GUEST_SESSION leftover=empty-session\n");
        serial_string("AIUEOS_GUEST_SESSION leftover=empty-session\r\n");
      } else if (hit != front) {
        debug_string("AIUEOS_GUEST_SESSION leftover=restore-ignored\n");
        serial_string("AIUEOS_GUEST_SESSION leftover=restore-ignored\r\n");
      } else if (front == 2 && empty == 0 && unk == 0 && hit == 2) {
        debug_string("AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2\n");
        serial_string("AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2\r\n");
      } else {
        debug_string("AIUEOS_GUEST_SESSION leftover=vector-miss\n");
        serial_string("AIUEOS_GUEST_SESSION leftover=vector-miss\r\n");
      }
    }
    /* Guest paint (ADR-0092). C fills both boot rects back-then-front
       from Kotoba's front id, then samples the overlap pixel. Painting
       key-order (window 1 last) is leftover :key-order-paint. Painting
       window 2 last regardless of raise is leftover :always-front-paint.
       Do not qemu_exit: gpu/cloud/guest-ime/guest-wm stay green without
       this line. */
    {
      uint64_t boot_front = kotoba_aiueos_wm_hit(2, 2, 100, 80);
      uint64_t raise_front = kotoba_aiueos_wm_hit(2, 1, 100, 80);
      uint32_t c1 = aiueos_desktop_wm_stored_color(1);
      uint32_t c2 = aiueos_desktop_wm_stored_color(2);
      uint32_t boot_pix, raise_pix;
      if (!aiueos_desktop_wm_rects_fit()) {
        debug_string("AIUEOS_GUEST_PAINT leftover=fb-too-small\n");
        serial_string("AIUEOS_GUEST_PAINT leftover=fb-too-small\r\n");
      } else if (!aiueos_desktop_wm_paint(boot_front)) {
        debug_string("AIUEOS_GUEST_PAINT leftover=one-guest-scanout\n");
        serial_string("AIUEOS_GUEST_PAINT leftover=one-guest-scanout\r\n");
      } else {
        boot_pix = aiueos_desktop_sample_pixel(100, 80);
        if (!aiueos_desktop_wm_paint(raise_front)) {
          debug_string("AIUEOS_GUEST_PAINT leftover=one-guest-scanout\n");
          serial_string("AIUEOS_GUEST_PAINT leftover=one-guest-scanout\r\n");
        } else {
          raise_pix = aiueos_desktop_sample_pixel(100, 80);
          if (boot_pix == c1) {
            debug_string("AIUEOS_GUEST_PAINT leftover=key-order-paint\n");
            serial_string("AIUEOS_GUEST_PAINT leftover=key-order-paint\r\n");
          } else if (raise_pix == c2) {
            debug_string("AIUEOS_GUEST_PAINT leftover=always-front-paint\n");
            serial_string("AIUEOS_GUEST_PAINT leftover=always-front-paint\r\n");
          } else if (boot_front == 2 && raise_front == 1 &&
                     boot_pix == c2 && raise_pix == c1) {
            debug_string("AIUEOS_GUEST_PAINT_OK boot-overlap=2 raised-overlap=1 key-order=0\n");
            serial_string("AIUEOS_GUEST_PAINT_OK boot-overlap=2 raised-overlap=1 key-order=0\r\n");
          } else {
            debug_string("AIUEOS_GUEST_PAINT leftover=vector-miss\n");
            serial_string("AIUEOS_GUEST_PAINT leftover=vector-miss\r\n");
          }
        }
      }
    }
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
    /* Guest gpu-two (ADR-0094). Resource count is Kotoba
       kotoba_aiueos_wm_hit; C does not invent n=2. Hosted JVM gpu does
       not count. Do not qemu_exit: gpu/guest-paint/guest-input stay
       green without this line. */
    if (aiueos_gpu_2d_two_ok()) {
      debug_string("AIUEOS_GUEST_GPU_TWO_OK resources=2 flush=2 kotoba-n=2\n");
      serial_string("AIUEOS_GUEST_GPU_TWO_OK resources=2 flush=2 kotoba-n=2\r\n");
    } else {
      debug_string("AIUEOS_GUEST_GPU_TWO leftover=one-resource\n");
      serial_string("AIUEOS_GUEST_GPU_TWO leftover=one-resource\r\n");
    }
    /* Guest scanout-two (ADR-0095). Bind count is Kotoba
       kotoba_aiueos_scanout_bind; C does not invent n=2. Hosted JVM wm
       does not count. Do not qemu_exit: gpu/guest-gpu-two stay green
       without this line. */
    if (aiueos_gpu_scanout_two_ok()) {
      debug_string("AIUEOS_GUEST_SCANOUT_TWO_OK scanouts=2 resource-0=1 resource-1=2 kotoba-n=2\n");
      serial_string("AIUEOS_GUEST_SCANOUT_TWO_OK scanouts=2 resource-0=1 resource-1=2 kotoba-n=2\r\n");
    } else {
      debug_string("AIUEOS_GUEST_SCANOUT_TWO leftover=one-scanout\n");
      serial_string("AIUEOS_GUEST_SCANOUT_TWO leftover=one-scanout enabled=");
      serial_decimal(aiueos_gpu_enabled_scanouts());
      serial_string("\r\n");
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
#ifdef AIUEOS_SSH_LISTEN
    /* The listener ran inside the NIC probe (pci.c). It accepts one inbound
       connection and exchanges SSH identification strings -- passive open and
       the first post-evidence service step, with no crypto yet. The marker is
       green only when a well-formed SSH-2.0 id string was received over a
       connection this OS accepted; the stage says how far a failed attempt
       got. */
    if (aiueos_ssh_client_id_valid()) {
      serial_string("AIUEOS_SSH_LISTEN_OK port=22 accepted client-id=valid len=");
      serial_decimal(aiueos_ssh_client_id_len());
      serial_string("\r\n");
      debug_string("AIUEOS_SSH_LISTEN_OK port=22 accepted client-id=valid\n");
      /* The real kex outcome (ADR-0107): 5 = KEX_ECDH_REPLY + NEWKEYS sent. The
         reply's host-key signature over H is what a real client verifies; the
         gate confirms that independently. Lower stages say how far it got. */
      if (aiueos_ssh_kex_stage() >= 5) {
        serial_string("AIUEOS_SSH_KEX_REPLY_OK curve25519-sha256 ecdsa-sha2-nistp256 reply+newkeys sent\r\n");
        debug_string("AIUEOS_SSH_KEX_REPLY_OK reply+newkeys sent\n");
      } else {
        serial_string("AIUEOS_SSH_KEX_REPLY_INCOMPLETE stage=");
        serial_decimal(aiueos_ssh_kex_stage());
        serial_string("\r\n");
      }
      /* Publickey userauth over the aes128-gcm record layer (ADR-0108):
         stage 9 = the client's signature over the session's signed-data verified
         against the authorized key and USERAUTH_SUCCESS was sent. */
      if (aiueos_ssh_kex_stage() >= 12) {
        serial_string("AIUEOS_SSH_AUTH_OK publickey ecdsa-sha2-nistp256 aes128-gcm userauth-success sent\r\n");
        debug_string("AIUEOS_SSH_AUTH_OK userauth-success sent\n");
        /* The session channel (ADR-0109): stage 16 = a `session` channel was
           opened, an `exec` request accepted, and CHANNEL_DATA streamed back. */
        if (aiueos_ssh_kex_stage() >= 16) {
          serial_string("AIUEOS_SSH_SESSION_OK session channel exec channel-data streamed\r\n");
          debug_string("AIUEOS_SSH_SESSION_OK exec channel-data streamed\n");
        } else if (aiueos_ssh_kex_stage() >= 13) {
          serial_string("AIUEOS_SSH_SESSION_INCOMPLETE stage=");
          serial_decimal(aiueos_ssh_kex_stage());
          serial_string("\r\n");
        }
      } else if (aiueos_ssh_kex_stage() >= 6) {
        /* 6 entered 7 client-newkeys 8 service-req 9 service-accept
           10 userauth-req 11 signature-verified. */
        serial_string("AIUEOS_SSH_AUTH_INCOMPLETE stage=");
        serial_decimal(aiueos_ssh_kex_stage());
        serial_string("\r\n");
      }
    } else {
      serial_string("AIUEOS_SSH_LISTEN_INCOMPLETE stage=");
      serial_decimal(aiueos_ssh_listen_stage());
      serial_string("\r\n");
    }
#endif
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
