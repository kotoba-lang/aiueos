/* 64-bit Multiboot landing. Proves the Multiboot boot path reaches long mode,
 * parses the bootloader-provided memory map, and runs the compiler-emitted
 * Kotoba probe object — the same object the UEFI path admits — before a
 * deterministic QEMU exit. This is the narrow first Multiboot vertical slice:
 * it does not stand up ACPI, virtio, GOP, or the full evidence gate, which the
 * UEFI path owns. */

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u
#define MULTIBOOT_INFO_MMAP 0x00000040u

struct multiboot_info {
  uint32_t flags;
  uint32_t mem_lower, mem_upper;
  uint32_t boot_device;
  uint32_t cmdline;
  uint32_t mods_count, mods_addr;
  uint32_t syms[4];
  uint32_t mmap_length, mmap_addr;
};

struct multiboot_mmap_entry {
  uint32_t size;
  uint64_t addr;
  uint64_t len;
  uint32_t type;
} __attribute__((packed));

extern uint64_t kotoba_aiueos_probe(void);

static inline void out8(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t in8(uint16_t port) {
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}
static void debug_string(const char *text) {
  while (*text) out8(0xe9, (uint8_t)*text++);
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
static void serial_string(const char *text) {
  while (*text) {
    uint32_t budget = 1000000;
    while (!(in8(0x3f8 + 5) & 0x20) && --budget) {}
    if (budget) out8(0x3f8, (uint8_t)*text++);
    else text++;
  }
}
__attribute__((noreturn)) static void qemu_exit(uint32_t value) {
  __asm__ volatile("outl %0, $0xf4" : : "a"(value));
  __asm__ volatile("cli");
  for (;;) __asm__ volatile("hlt");
}

void aiueos_multiboot_main(uint32_t magic, uint32_t info_addr) {
  serial_init();
  if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
    serial_string("AIUEOS_MULTIBOOT_FAIL magic\r\n");
    qemu_exit(0x7e);
  }
  const struct multiboot_info *info = (const struct multiboot_info *)(uintptr_t)info_addr;
  if (!(info->flags & MULTIBOOT_INFO_MMAP) || !info->mmap_length) {
    serial_string("AIUEOS_MULTIBOOT_FAIL mmap-absent\r\n");
    qemu_exit(0x7d);
  }
  /* Bounded walk of the variable-stride Multiboot memory map. Require at least
   * one usable (type 1) region and stay inside the advertised length. */
  uint32_t offset = 0, usable = 0;
  uint64_t usable_bytes = 0;
  while (offset + sizeof(struct multiboot_mmap_entry) <= info->mmap_length) {
    const struct multiboot_mmap_entry *entry =
      (const struct multiboot_mmap_entry *)(uintptr_t)(info->mmap_addr + offset);
    if (entry->size < 20 || entry->size > 4096) {
      serial_string("AIUEOS_MULTIBOOT_FAIL mmap-stride\r\n");
      qemu_exit(0x7c);
    }
    if (entry->type == 1 && entry->len) {
      usable++;
      usable_bytes += entry->len;
    }
    offset += entry->size + 4;  /* size field does not count itself */
  }
  if (!usable || usable_bytes < 0x100000ull) {
    serial_string("AIUEOS_MULTIBOOT_FAIL no-usable-memory\r\n");
    qemu_exit(0x7b);
  }
  debug_string("AIUEOS_MULTIBOOT_MMAP_OK\n");
  serial_string("AIUEOS_MULTIBOOT_MMAP_OK type1-regions bounded-walk\r\n");

  if (kotoba_aiueos_probe() != 42u) {
    serial_string("AIUEOS_MULTIBOOT_FAIL kotoba-probe\r\n");
    qemu_exit(0x67);
  }
  debug_string("AIUEOS_MULTIBOOT_KOTOBA_OK\n");
  serial_string("AIUEOS_MULTIBOOT_OK long-mode mmap-parsed kotoba-probe=42\r\n");
  /* isa-debug-exit maps value 0x2a to status (0x2a << 1) | 1 = 85. */
  qemu_exit(0x2a);
}
