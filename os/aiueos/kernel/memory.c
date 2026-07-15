#include <stdint.h>
#include <stddef.h>

#define EFI_CONVENTIONAL_MEMORY 7U
#define PAGE_SIZE 4096ULL
#define IDENTITY_LIMIT 0x40000000ULL

struct aiueos_boot_info {
  uint64_t magic, version;
  void *memory_map; uint64_t memory_map_size, descriptor_size, descriptor_version;
  void *acpi_rsdp;
  uint64_t framebuffer_base, framebuffer_size;
  uint32_t framebuffer_width, framebuffer_height, framebuffer_stride, framebuffer_format;
};
struct efi_memory_descriptor_prefix {
  uint32_t type, padding;
  uint64_t physical_start, virtual_start, number_of_pages, attributes;
};
extern uint8_t aiueos_kernel_end[];

static uint64_t next_page;
static uint64_t remaining_pages;
static void *free_pages;
static volatile uint8_t allocator_lock;
static uint64_t allocator_reuse_count;
#define ALLOCATION_RECORDS 256
struct allocation_record { void *page; uint8_t active; };
static struct allocation_record allocation_records[ALLOCATION_RECORDS];

static void lock(void) { while (__atomic_test_and_set(&allocator_lock,__ATOMIC_ACQUIRE)) __asm__ volatile("pause"); }
static void unlock(void) { __atomic_clear(&allocator_lock,__ATOMIC_RELEASE); }
static void zero_page(void *page) {
  uint64_t *words=page;
  for (uint64_t i=0;i<PAGE_SIZE/sizeof(uint64_t);i++) words[i]=0;
}

int aiueos_physical_allocator_initialize(const struct aiueos_boot_info *boot) {
  if (!boot || !boot->memory_map ||
      boot->descriptor_size < sizeof(struct efi_memory_descriptor_prefix) ||
      boot->descriptor_size > 4096 ||
      boot->memory_map_size < boot->descriptor_size ||
      boot->memory_map_size % boot->descriptor_size != 0) return 0;

  uint64_t kernel_limit = ((uint64_t)(uintptr_t)aiueos_kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  uint8_t *cursor = boot->memory_map;
  uint8_t *end = cursor + boot->memory_map_size;
  uint64_t best_start = 0, best_pages = 0;
  while (cursor < end) {
    const struct efi_memory_descriptor_prefix *descriptor = (const void *)cursor;
    if (descriptor->type == EFI_CONVENTIONAL_MEMORY && descriptor->number_of_pages &&
        descriptor->physical_start < IDENTITY_LIMIT) {
      uint64_t start = descriptor->physical_start;
      uint64_t pages = descriptor->number_of_pages;
      if (start < kernel_limit) {
        uint64_t skipped = (kernel_limit - start + PAGE_SIZE - 1) / PAGE_SIZE;
        if (skipped >= pages) pages = 0;
        else { start += skipped * PAGE_SIZE; pages -= skipped; }
      }
      uint64_t limit_pages = (IDENTITY_LIMIT - start) / PAGE_SIZE;
      if (pages > limit_pages) pages = limit_pages;
      if (pages > best_pages) { best_start = start; best_pages = pages; }
    }
    cursor += boot->descriptor_size;
  }
  next_page = best_start;
  remaining_pages = best_pages;
  free_pages=0; allocator_lock=0; allocator_reuse_count=0;
  for (uint32_t i=0;i<ALLOCATION_RECORDS;i++) allocation_records[i]=(struct allocation_record){0,0};
  return next_page != 0 && remaining_pages >= 32;
}

void *aiueos_allocate_physical_page(void) {
  lock(); void *page=0;
  if (free_pages) {
    page=free_pages; free_pages=*(void **)free_pages; allocator_reuse_count++;
    for (uint32_t i=0;i<ALLOCATION_RECORDS;i++)
      if (allocation_records[i].page==page) { allocation_records[i].active=1; break; }
  } else if (remaining_pages && next_page && next_page<IDENTITY_LIMIT) {
    for (uint32_t i=0;i<ALLOCATION_RECORDS;i++) if (!allocation_records[i].page) {
      page=(void *)(uintptr_t)next_page; next_page+=PAGE_SIZE; remaining_pages--;
      allocation_records[i]=(struct allocation_record){page,1}; break;
    }
  }
  if (page) zero_page(page); unlock(); return page;
}

int aiueos_free_physical_page(void *page) {
  if (!page || ((uintptr_t)page&(PAGE_SIZE-1)) || (uintptr_t)page>=IDENTITY_LIMIT) return 0;
  lock();
  for (uint32_t i=0;i<ALLOCATION_RECORDS;i++) if (allocation_records[i].page==page) {
    if (!allocation_records[i].active) { unlock(); return 0; }
    allocation_records[i].active=0; zero_page(page); *(void **)page=free_pages; free_pages=page;
    unlock(); return 1;
  }
  unlock(); return 0;
}
uint64_t aiueos_physical_allocator_reuse_count(void) {
  lock(); uint64_t count=allocator_reuse_count; unlock(); return count;
}
