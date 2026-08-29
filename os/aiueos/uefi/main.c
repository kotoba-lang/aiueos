#include <stdint.h>
#include <stddef.h>

#define EFIAPI __attribute__((ms_abi))
#define SYSVABI __attribute__((sysv_abi))
#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL ((uint64_t)0x8000000000000005ULL)
#define EFI_INVALID_PARAMETER ((uint64_t)0x8000000000000002ULL)
#define EFI_BY_PROTOCOL 2
#define EFI_VARIABLE_NON_VOLATILE 1U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 2U
#define EFI_VARIABLE_RUNTIME_ACCESS 4U
#define PAGE_SIZE 4096ULL
#define KERNEL_BUFFER_SIZE (1024ULL * 1024ULL)
#define INITRAMFS_BUFFER_SIZE (1024ULL * 1024ULL)
#define MEMORY_MAP_BUFFER_SIZE (128ULL * 1024ULL)
#define EFI_ALLOCATE_MAX_ADDRESS 1U
#define EFI_LOADER_DATA 2U
#define ACPI_IDENTITY_LIMIT 0x40000000ULL
#define ACPI_RETAIN_RSDP_BYTES PAGE_SIZE
#define ACPI_RETAIN_ROOT_BYTES PAGE_SIZE
#define ACPI_RETAIN_TABLE_BYTES (64ULL * 1024ULL)
#define ACPI_RETAIN_BYTES (ACPI_RETAIN_RSDP_BYTES + ACPI_RETAIN_ROOT_BYTES + \
                           2ULL * ACPI_RETAIN_TABLE_BYTES)
#define ACPI_MAX_ROOT_ENTRIES 128U

typedef uint64_t efi_status;
typedef void *efi_handle;
typedef uint16_t char16;

struct efi_guid { uint32_t a; uint16_t b, c; uint8_t d[8]; };
struct efi_table_header { uint64_t signature; uint32_t revision, header_size, crc32, reserved; };
struct efi_simple_text_output;
typedef efi_status(EFIAPI *efi_output_string)(struct efi_simple_text_output *, const char16 *);
struct efi_simple_text_output {
  void *reset; efi_output_string output_string; void *rest[8];
};

typedef efi_status(EFIAPI *efi_allocate_pages)(uint32_t, uint32_t, uint64_t, uint64_t *);
typedef efi_status(EFIAPI *efi_get_memory_map)(uint64_t *, void *, uint64_t *, uint64_t *, uint32_t *);
typedef efi_status(EFIAPI *efi_allocate_pool)(uint32_t, uint64_t, void **);
typedef efi_status(EFIAPI *efi_free_pool)(void *);
typedef efi_status(EFIAPI *efi_handle_protocol)(efi_handle, const struct efi_guid *, void **);
typedef efi_status(EFIAPI *efi_locate_handle)(uint32_t, const struct efi_guid *, void *,
                                              uint64_t *, efi_handle *);
typedef efi_status(EFIAPI *efi_exit_boot_services)(efi_handle, uint64_t);
typedef efi_status(EFIAPI *efi_set_watchdog_timer)(uint64_t, uint64_t, uint64_t,
                                                   const char16 *);

struct efi_boot_services {
  struct efi_table_header header;
  void *raise_tpl, *restore_tpl;
  efi_allocate_pages allocate_pages;
  void *free_pages;
  efi_get_memory_map get_memory_map;
  efi_allocate_pool allocate_pool;
  efi_free_pool free_pool;
  void *create_event, *set_timer, *wait_for_event, *signal_event, *close_event, *check_event;
  void *install_protocol_interface, *reinstall_protocol_interface, *uninstall_protocol_interface;
  efi_handle_protocol handle_protocol;
  void *reserved, *register_protocol_notify;
  efi_locate_handle locate_handle;
  void *locate_device_path;
  void *install_configuration_table, *load_image, *start_image, *exit, *unload_image;
  efi_exit_boot_services exit_boot_services;
  void *get_next_monotonic_count, *stall;
  efi_set_watchdog_timer set_watchdog_timer;
};

typedef efi_status(EFIAPI *efi_set_variable)(const char16 *, const struct efi_guid *,
                                             uint32_t, uint64_t, void *);
typedef efi_status(EFIAPI *efi_get_variable)(const char16 *, const struct efi_guid *,
                                             uint32_t *, uint64_t *, void *);
struct efi_runtime_services {
  struct efi_table_header header;
  void *get_time, *set_time, *get_wakeup_time, *set_wakeup_time;
  void *set_virtual_address_map, *convert_pointer;
  efi_get_variable get_variable;
  void *get_next_variable_name;
  efi_set_variable set_variable;
  void *get_next_high_monotonic_count, *reset_system;
};
struct efi_system_table {
  struct efi_table_header header;
  char16 *firmware_vendor; uint32_t firmware_revision, padding;
  efi_handle console_in_handle; void *console_in;
  efi_handle console_out_handle; struct efi_simple_text_output *console_out;
  efi_handle standard_error_handle; struct efi_simple_text_output *standard_error;
  struct efi_runtime_services *runtime_services; struct efi_boot_services *boot_services;
  uint64_t number_of_table_entries; void *configuration_table;
};
struct efi_configuration_table { struct efi_guid vendor_guid; void *vendor_table; };

struct __attribute__((packed)) acpi_rsdp_v2 {
  char signature[8]; uint8_t checksum; char oem_id[6]; uint8_t revision;
  uint32_t rsdt_address, length; uint64_t xsdt_address;
  uint8_t extended_checksum, reserved[3];
};
struct __attribute__((packed)) acpi_sdt_header {
  char signature[4]; uint32_t length; uint8_t revision, checksum;
  char oem_id[6], oem_table_id[8];
  uint32_t oem_revision, creator_id, creator_revision;
};

struct efi_loaded_image {
  uint32_t revision, padding; efi_handle parent_handle;
  struct efi_system_table *system_table; efi_handle device_handle;
  void *file_path, *reserved; uint32_t load_options_size, padding2;
  void *load_options, *image_base; uint64_t image_size;
  uint32_t image_code_type, image_data_type; void *unload;
};

struct efi_file;
typedef efi_status(EFIAPI *efi_file_open)(struct efi_file *, struct efi_file **, const char16 *, uint64_t, uint64_t);
typedef efi_status(EFIAPI *efi_file_close)(struct efi_file *);
typedef efi_status(EFIAPI *efi_file_read)(struct efi_file *, uint64_t *, void *);
struct efi_file {
  uint64_t revision; efi_file_open open; efi_file_close close;
  void *delete_file; efi_file_read read; void *write, *get_position, *set_position;
  void *get_info, *set_info, *flush;
};
struct efi_simple_file_system {
  uint64_t revision;
  efi_status(EFIAPI *open_volume)(struct efi_simple_file_system *, struct efi_file **);
};

struct elf64_header {
  uint8_t ident[16]; uint16_t type, machine; uint32_t version;
  uint64_t entry, phoff, shoff; uint32_t flags; uint16_t ehsize, phentsize, phnum;
  uint16_t shentsize, shnum, shstrndx;
};
struct elf64_program_header {
  uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

struct aiueos_boot_info {
  uint64_t magic, version;
  void *memory_map; uint64_t memory_map_size, descriptor_size, descriptor_version;
  void *acpi_rsdp;
  uint64_t framebuffer_base, framebuffer_size;
  uint32_t framebuffer_width, framebuffer_height, framebuffer_stride, framebuffer_format;
  uint64_t initramfs_base, initramfs_size;
  void *runtime_services;
  uint64_t firmware_cr3;
};
struct aiueos_qualification_record {
  uint32_t magic;
  uint16_t version, state;
  uint32_t code, reserved;
};
typedef void(SYSVABI *kernel_entry)(const struct aiueos_boot_info *);
extern const uint8_t aiueos_expected_kernel_sha256[32];
extern const uint8_t aiueos_expected_initramfs_sha256[32];

static const struct efi_guid loaded_image_guid =
  {0x5b1b31a1, 0x9562, 0x11d2, {0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const struct efi_guid simple_fs_guid =
  {0x964e5b22, 0x6459, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const struct efi_guid acpi20_guid =
  {0x8868e871, 0xe4f1, 0x11d3, {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
static const struct efi_guid graphics_output_guid =
  {0x9042a9de, 0x23dc, 0x4a38, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static const struct efi_guid qualification_guid =
  {0x73953a72,0x6627,0x4b62,{0x9a,0x9c,0x10,0x38,0xd9,0x20,0x9a,0x16}};
static const struct efi_guid global_variable_guid =
  {0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};
static const char16 qualification_name[] = u"AIUEOSQualificationResult";
static const char16 boot_current_name[] = u"BootCurrent";
static const char16 boot_next_name[] = u"BootNext";

#ifdef AIUEOS_EMBEDDED_RELEASE
extern const uint8_t aiueos_embedded_kernel_start[];
extern const uint8_t aiueos_embedded_kernel_end[];
extern const uint8_t aiueos_embedded_initramfs_start[];
extern const uint8_t aiueos_embedded_initramfs_end[];
#endif

struct efi_graphics_output_mode_info {
  uint32_t version, horizontal_resolution, vertical_resolution, pixel_format;
  uint32_t pixel_information[4], pixels_per_scan_line;
};
struct efi_graphics_output_mode {
  uint32_t max_mode, mode; struct efi_graphics_output_mode_info *info;
  uint64_t size_of_info, framebuffer_base, framebuffer_size;
};
struct efi_graphics_output_protocol {
  void *query_mode, *set_mode, *blt; struct efi_graphics_output_mode *mode;
};

static struct efi_graphics_output_protocol *find_graphics_output(
    struct efi_system_table *system, struct efi_boot_services *bs,
    uint8_t *used_protocol_scan) {
  struct efi_graphics_output_protocol *gop=0;
#ifndef AIUEOS_GOP_FORCE_PROTOCOL_SCAN
  if (system->console_out_handle &&
      bs->handle_protocol(system->console_out_handle,&graphics_output_guid,
                          (void **)&gop)==EFI_SUCCESS && gop)
    return gop;
#endif
  efi_handle handles[32];uint64_t bytes=sizeof(handles);
  if (bs->locate_handle(EFI_BY_PROTOCOL,&graphics_output_guid,0,&bytes,handles)!=EFI_SUCCESS)
    return 0;
  uint64_t count=bytes/sizeof(efi_handle);
  for (uint64_t i=0;i<count;i++) {
    gop=0;
    if (bs->handle_protocol(handles[i],&graphics_output_guid,(void **)&gop)==EFI_SUCCESS && gop) {
      *used_protocol_scan=1;
      return gop;
    }
  }
  return 0;
}

static int guid_equal(const struct efi_guid *a, const struct efi_guid *b) {
  const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
  for (uint64_t i = 0; i < sizeof(*a); i++) if (x[i] != y[i]) return 0;
  return 1;
}

static uint64_t read_cr3(void) {
  uint64_t value;
  __asm__ volatile("mov %%cr3, %0" : "=r"(value));
  return value;
}

static void copy_bytes(void *to, const void *from, uint64_t size) {
  uint8_t *d = to; const uint8_t *s = from; while (size--) *d++ = *s++;
}
static void zero_bytes(void *to, uint64_t size) { uint8_t *d = to; while (size--) *d++ = 0; }

static int acpi_bytes_equal(const char *left, const char *right,
                            uint64_t bytes) {
  while (bytes--) if (*left++ != *right++) return 0;
  return 1;
}

static int acpi_checksum_ok(const void *table, uint64_t bytes) {
  if (!table || !bytes || bytes > ACPI_RETAIN_TABLE_BYTES) return 0;
  const uint8_t *input=table;
  uint8_t sum=0;
  while (bytes--) sum=(uint8_t)(sum+*input++);
  return sum==0;
}

static uint8_t acpi_checksum_value(const void *table, uint64_t bytes) {
  const uint8_t *input=table;
  uint8_t sum=0;
  while (bytes--) sum=(uint8_t)(sum+*input++);
  return (uint8_t)(0U-sum);
}

static int acpi_source_address_sane(uint64_t address, uint64_t bytes) {
  return address && bytes && address < (1ULL << 52) &&
         address <= (1ULL << 52) - bytes;
}

/* Firmware can place ACPI reclaim memory well above the kernel's intentionally
   small bootstrap identity map (the physical K16 places it around 0x92f00000).
   Before ExitBootServices, validate the source graph and relocate only the two
   tables this kernel consumes into LoaderData below 1 GiB.  The synthetic XSDT
   is checksummed again and contains only APIC plus optional DMAR, so the kernel
   still runs its Kotoba admission over every table it is asked to trust. */
static void *retain_acpi_graph(struct efi_boot_services *bs,
                               const void *rsdp_pointer) {
  if (!bs || !bs->allocate_pages || !rsdp_pointer) return 0;
  const struct acpi_rsdp_v2 *source_rsdp=rsdp_pointer;
  if (!acpi_bytes_equal(source_rsdp->signature,"RSD PTR ",8) ||
      source_rsdp->revision<2 || source_rsdp->length<sizeof(*source_rsdp) ||
      source_rsdp->length>ACPI_RETAIN_RSDP_BYTES ||
      !acpi_checksum_ok(source_rsdp,20) ||
      !acpi_checksum_ok(source_rsdp,source_rsdp->length) ||
      !acpi_source_address_sane(source_rsdp->xsdt_address,
                                sizeof(struct acpi_sdt_header))) return 0;

  const struct acpi_sdt_header *source_root=
    (const void *)(uintptr_t)source_rsdp->xsdt_address;
  if (!acpi_bytes_equal(source_root->signature,"XSDT",4) ||
      source_root->length<sizeof(*source_root) ||
      source_root->length>ACPI_RETAIN_ROOT_BYTES ||
      (source_root->length-sizeof(*source_root))%8 ||
      !acpi_checksum_ok(source_root,source_root->length)) return 0;
  uint32_t entries=(source_root->length-sizeof(*source_root))/8;
  if (!entries || entries>ACPI_MAX_ROOT_ENTRIES) return 0;

  const struct acpi_sdt_header *source_madt=0,*source_dmar=0;
  const uint8_t *entry_bytes=(const uint8_t *)source_root+sizeof(*source_root);
  for (uint32_t index=0;index<entries;index++) {
    uint64_t address=*(const uint64_t *)(const void *)(entry_bytes+(uint64_t)index*8);
    if (!acpi_source_address_sane(address,sizeof(struct acpi_sdt_header))) return 0;
    const struct acpi_sdt_header *candidate=(const void *)(uintptr_t)address;
    if (!acpi_bytes_equal(candidate->signature,"APIC",4) &&
        !acpi_bytes_equal(candidate->signature,"DMAR",4)) continue;
    if (candidate->length<sizeof(*candidate) ||
        candidate->length>ACPI_RETAIN_TABLE_BYTES ||
        !acpi_source_address_sane(address,candidate->length) ||
        !acpi_checksum_ok(candidate,candidate->length)) return 0;
    if (acpi_bytes_equal(candidate->signature,"APIC",4)) {
      if (source_madt) return 0;
      source_madt=candidate;
    } else {
      if (source_dmar) return 0;
      source_dmar=candidate;
    }
  }
  if (!source_madt) return 0;

  uint64_t pages=(ACPI_RETAIN_BYTES+PAGE_SIZE-1)/PAGE_SIZE;
  uint64_t base=ACPI_IDENTITY_LIMIT-1;
  if (bs->allocate_pages(EFI_ALLOCATE_MAX_ADDRESS,EFI_LOADER_DATA,pages,&base)
        != EFI_SUCCESS || !base || base>=ACPI_IDENTITY_LIMIT ||
      base>ACPI_IDENTITY_LIMIT-pages*PAGE_SIZE) return 0;
  zero_bytes((void *)(uintptr_t)base,pages*PAGE_SIZE);

  struct acpi_rsdp_v2 *retained_rsdp=(void *)(uintptr_t)base;
  struct acpi_sdt_header *retained_root=
    (void *)(uintptr_t)(base+ACPI_RETAIN_RSDP_BYTES);
  struct acpi_sdt_header *retained_madt=
    (void *)(uintptr_t)(base+ACPI_RETAIN_RSDP_BYTES+ACPI_RETAIN_ROOT_BYTES);
  struct acpi_sdt_header *retained_dmar=
    (void *)(uintptr_t)(base+ACPI_RETAIN_RSDP_BYTES+ACPI_RETAIN_ROOT_BYTES+
                        ACPI_RETAIN_TABLE_BYTES);
  copy_bytes(retained_rsdp,source_rsdp,source_rsdp->length);
  copy_bytes(retained_root,source_root,sizeof(*retained_root));
  copy_bytes(retained_madt,source_madt,source_madt->length);
  if (source_dmar) copy_bytes(retained_dmar,source_dmar,source_dmar->length);

  uint64_t *retained_entries=(void *)((uint8_t *)retained_root+
                                      sizeof(*retained_root));
  retained_entries[0]=(uint64_t)(uintptr_t)retained_madt;
  uint32_t retained_entry_count=1;
  if (source_dmar)
    retained_entries[retained_entry_count++]=(uint64_t)(uintptr_t)retained_dmar;
  retained_root->length=sizeof(*retained_root)+retained_entry_count*8U;
  retained_root->checksum=0;
  retained_root->checksum=acpi_checksum_value(retained_root,retained_root->length);

  retained_rsdp->rsdt_address=0;
  retained_rsdp->xsdt_address=(uint64_t)(uintptr_t)retained_root;
  retained_rsdp->checksum=0;
  retained_rsdp->extended_checksum=0;
  retained_rsdp->checksum=acpi_checksum_value(retained_rsdp,20);
  retained_rsdp->extended_checksum=
    acpi_checksum_value(retained_rsdp,retained_rsdp->length);
  return retained_rsdp;
}

static uint32_t rotate_right(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}
static void sha256(const uint8_t *input, uint64_t size, uint8_t output[32]) {
  static const uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
  };
  uint32_t state[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                       0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  uint8_t block[64]; uint64_t offset = 0, total_bits = size * 8;
  for (;;) {
    uint64_t remaining = size - offset;
    uint64_t take = remaining > 64 ? 64 : remaining;
    for (uint64_t i = 0; i < take; i++) block[i] = input[offset + i];
    offset += take;
    if (take < 64) {
      block[take++] = 0x80;
      if (take > 56) {
        while (take < 64) block[take++] = 0;
      } else {
        while (take < 56) block[take++] = 0;
        for (int i = 7; i >= 0; i--) block[take++] = (uint8_t)(total_bits >> (i * 8));
      }
    }
    uint32_t words[64];
    for (uint32_t i = 0; i < 16; i++) words[i] = ((uint32_t)block[i*4] << 24) |
      ((uint32_t)block[i*4+1] << 16) | ((uint32_t)block[i*4+2] << 8) | block[i*4+3];
    for (uint32_t i = 16; i < 64; i++) {
      uint32_t s0 = rotate_right(words[i-15],7) ^ rotate_right(words[i-15],18) ^ (words[i-15] >> 3);
      uint32_t s1 = rotate_right(words[i-2],17) ^ rotate_right(words[i-2],19) ^ (words[i-2] >> 10);
      words[i] = words[i-16] + s0 + words[i-7] + s1;
    }
    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
    for (uint32_t i = 0; i < 64; i++) {
      uint32_t s1=rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);
      uint32_t choice=(e&f)^((~e)&g), t1=h+s1+choice+constants[i]+words[i];
      uint32_t s0=rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22);
      uint32_t majority=(a&b)^(a&c)^(b&c), t2=s0+majority;
      h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    if (remaining < 64) {
      if (take == 64 && block[56] != (uint8_t)(total_bits >> 56) && remaining >= 56) {
        for (uint32_t i=0;i<56;i++) block[i]=0;
        for (int i=7;i>=0;i--) block[56+(7-i)]=(uint8_t)(total_bits>>(i*8));
        uint32_t w2[64];
        for(uint32_t i=0;i<16;i++) w2[i]=((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|((uint32_t)block[i*4+2]<<8)|block[i*4+3];
        for(uint32_t i=16;i<64;i++){uint32_t x=rotate_right(w2[i-15],7)^rotate_right(w2[i-15],18)^(w2[i-15]>>3);uint32_t y=rotate_right(w2[i-2],17)^rotate_right(w2[i-2],19)^(w2[i-2]>>10);w2[i]=w2[i-16]+x+w2[i-7]+y;}
        a=state[0];b=state[1];c=state[2];d=state[3];e=state[4];f=state[5];g=state[6];h=state[7];
        for(uint32_t i=0;i<64;i++){uint32_t x=rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);uint32_t t1=h+x+((e&f)^((~e)&g))+constants[i]+w2[i];uint32_t t2=(rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
      }
      break;
    }
  }
  for(uint32_t i=0;i<8;i++){output[i*4]=(uint8_t)(state[i]>>24);output[i*4+1]=(uint8_t)(state[i]>>16);output[i*4+2]=(uint8_t)(state[i]>>8);output[i*4+3]=(uint8_t)state[i];}
}
static inline void debug_byte(uint8_t value) { __asm__ volatile("outb %0, $0xe9" : : "a"(value)); }
static void debug_string(const char *text) { while (*text) debug_byte((uint8_t)*text++); }
static inline void fail_exit(void) { __asm__ volatile("outl %0, $0xf4" : : "a"(0x7f)); }
static struct efi_simple_text_output *loader_console;
static struct efi_runtime_services *loader_runtime;

#ifndef AIUEOS_QUALIFICATION_LOADER_WATCHDOG_SECONDS
#define AIUEOS_QUALIFICATION_LOADER_WATCHDOG_SECONDS 90ULL
#endif

static void console_ascii(const char *text) {
  if (!loader_console || !loader_console->output_string) return;
  char16 wide[192];
  uint64_t i=0;
  while (text[i] && i+1<sizeof(wide)/sizeof(wide[0])) {
    wide[i]=(uint8_t)text[i];i++;
  }
  wide[i]=0;
  loader_console->output_string(loader_console,wide);
}

static void console_decimal(uint32_t value) {
  char text[16];uint32_t digits=0;
  do { text[digits++]=(char)('0'+value%10);value/=10; } while (value && digits<15);
  for (uint32_t i=0;i<digits/2;i++) {
    char tmp=text[i];text[i]=text[digits-1-i];text[digits-1-i]=tmp;
  }
  text[digits]=0;console_ascii(text);
}

static int persist_loader_record(uint16_t state, uint32_t code) {
  if (!loader_runtime || !loader_runtime->set_variable) return 0;
  struct aiueos_qualification_record record={0x514b3241U,2,state,code,0};
  return loader_runtime->set_variable(
      qualification_name,&qualification_guid,
      EFI_VARIABLE_NON_VOLATILE|EFI_VARIABLE_BOOTSERVICE_ACCESS|
        EFI_VARIABLE_RUNTIME_ACCESS,
      sizeof(record),&record)==EFI_SUCCESS;
}

static int persist_loader_failure(uint32_t code) {
  return persist_loader_record(2,code);
}

static int prepare_netboot_qualification_return(void) {
#ifdef AIUEOS_NETBOOT_QUALIFICATION
  if (!loader_runtime || !loader_runtime->get_variable ||
      !loader_runtime->set_variable) return 0;
  uint16_t boot_current=0;
  uint32_t attributes=0;
  uint64_t bytes=sizeof(boot_current);
  if (loader_runtime->get_variable(
          boot_current_name,&global_variable_guid,&attributes,&bytes,
          &boot_current)!=EFI_SUCCESS || bytes!=sizeof(boot_current)) return 0;
  attributes=EFI_VARIABLE_NON_VOLATILE|EFI_VARIABLE_BOOTSERVICE_ACCESS|
             EFI_VARIABLE_RUNTIME_ACCESS;
  if (loader_runtime->set_variable(
          boot_next_name,&global_variable_guid,attributes,sizeof(boot_current),
          &boot_current)!=EFI_SUCCESS) return 0;
  struct aiueos_qualification_record pending={0x514b3241U,2,0,0,0};
  if (loader_runtime->set_variable(
          qualification_name,&qualification_guid,attributes,sizeof(pending),
          &pending)!=EFI_SUCCESS) {
    loader_runtime->set_variable(
        boot_next_name,&global_variable_guid,0,0,0);
    return 0;
  }
  debug_string("AIUEOS_NETBOOT_RETURN_ARMED bootnext=current result=pending\n");
#endif
  return 1;
}

static void progress(uint32_t code, const char *message) {
  debug_string(message);debug_string(" code=");
  char digits[16];uint32_t count=0,value=code;
  do { digits[count++]=(char)('0'+value%10);value/=10; } while (value && count<15);
  while (count) debug_byte((uint8_t)digits[--count]);
  debug_byte('\n');
  console_ascii(message);console_ascii(" code=");console_decimal(code);console_ascii("\r\n");
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
  if (!persist_loader_record(0,code)) {
    debug_string("AIUEOS_LOADER_PROGRESS_PERSIST_FAIL\n");
    console_ascii("Progress could not be persisted.\r\n");
  }
#endif
}

static efi_status fail(uint32_t code, const char *message) {
  debug_string(message);debug_string(" code=");
  char digits[16];uint32_t count=0,value=code;
  do { digits[count++]=(char)('0'+value%10);value/=10; } while (value && count<15);
  while (count) debug_byte((uint8_t)digits[--count]);
  debug_byte('\n');
  console_ascii("\r\n");console_ascii(message);console_ascii(" code=");
  console_decimal(code);console_ascii("\r\n");
  if (persist_loader_failure(code)) {
    debug_string("AIUEOS_LOADER_FAILURE_RESULT_PERSISTED\n");
    console_ascii("Failure result persisted; returning to USB collector.\r\n");
  } else {
    debug_string("AIUEOS_LOADER_FAILURE_RESULT_PERSIST_FAIL\n");
    console_ascii("Failure result could not be persisted.\r\n");
  }
#ifndef AIUEOS_PHYSICAL_QUALIFICATION
  fail_exit();
#endif
  return EFI_INVALID_PARAMETER;
}

static efi_status read_verified_file(struct efi_file *root, const char16 *path,
                                     const char *kind_open, const char *kind_read,
                                     const char *kind_sha,
                                     const uint8_t expected_sha256[32],
                                     uint8_t *buffer, uint64_t buffer_size,
                                     uint64_t *size) {
  struct efi_file *file = 0;
  if (root->open(root, &file, path, 1, 0) != EFI_SUCCESS || !file) {
    debug_string(kind_open); return EFI_INVALID_PARAMETER;
  }
  *size = buffer_size;
  if (file->read(file, size, buffer) != EFI_SUCCESS) {
    file->close(file);
    debug_string(kind_read); return EFI_INVALID_PARAMETER;
  }
  file->close(file);
  uint8_t digest[32];
  sha256(buffer, *size, digest);
  for (uint32_t i = 0; i < 32; i++) {
    if (digest[i] != expected_sha256[i]) {
      debug_string(kind_sha); return EFI_INVALID_PARAMETER;
    }
  }
  return EFI_SUCCESS;
}

/* One admission candidate: open the volume on this device and require both
   the kernel and the initramfs to match their compiled-in SHA-256 digests.
   Emits the failure marker but does not terminate, so the caller can fall
   back to another volume. */
static efi_status read_verified_kernel(struct efi_boot_services *bs, efi_handle device,
                                       const char16 *kernel_path,
                                       const char16 *initramfs_path,
                                       uint8_t *kernel_file, uint64_t *kernel_size,
                                       uint8_t *initramfs_file, uint64_t *initramfs_size) {
  struct efi_simple_file_system *fs = 0;
  struct efi_file *root = 0;
  if (bs->handle_protocol(device, &simple_fs_guid, (void **)&fs) != EFI_SUCCESS || !fs) {
    debug_string("AIUEOS_LOADER_FAIL filesystem\n"); return EFI_INVALID_PARAMETER;
  }
  if (fs->open_volume(fs, &root) != EFI_SUCCESS || !root) {
    debug_string("AIUEOS_LOADER_FAIL volume\n"); return EFI_INVALID_PARAMETER;
  }
  efi_status status = read_verified_file(root, kernel_path,
      "AIUEOS_LOADER_FAIL kernel-open\n", "AIUEOS_LOADER_FAIL kernel-read\n",
      "AIUEOS_LOADER_FAIL kernel-sha256\n",
      aiueos_expected_kernel_sha256, kernel_file, KERNEL_BUFFER_SIZE, kernel_size);
  if (status == EFI_SUCCESS)
    status = read_verified_file(root, initramfs_path,
        "AIUEOS_LOADER_FAIL initramfs-open\n", "AIUEOS_LOADER_FAIL initramfs-read\n",
        "AIUEOS_LOADER_FAIL initramfs-sha256\n",
        aiueos_expected_initramfs_sha256, initramfs_file, INITRAMFS_BUFFER_SIZE,
        initramfs_size);
  root->close(root);
  return status;
}

#ifdef AIUEOS_EMBEDDED_RELEASE
static efi_status read_verified_embedded(uint8_t *kernel_file,
                                         uint64_t *kernel_size,
                                         uint8_t *initramfs_file,
                                         uint64_t *initramfs_size) {
  uint64_t embedded_kernel_size=
    (uint64_t)(aiueos_embedded_kernel_end-aiueos_embedded_kernel_start);
  uint64_t embedded_initramfs_size=
    (uint64_t)(aiueos_embedded_initramfs_end-aiueos_embedded_initramfs_start);
  if (!embedded_kernel_size || embedded_kernel_size>KERNEL_BUFFER_SIZE ||
      !embedded_initramfs_size || embedded_initramfs_size>INITRAMFS_BUFFER_SIZE)
    return EFI_INVALID_PARAMETER;
  copy_bytes(kernel_file,aiueos_embedded_kernel_start,embedded_kernel_size);
  copy_bytes(initramfs_file,aiueos_embedded_initramfs_start,
             embedded_initramfs_size);
  uint8_t digest[32];
  sha256(kernel_file,embedded_kernel_size,digest);
  for (uint32_t i=0;i<32;i++)
    if (digest[i]!=aiueos_expected_kernel_sha256[i]) return EFI_INVALID_PARAMETER;
  sha256(initramfs_file,embedded_initramfs_size,digest);
  for (uint32_t i=0;i<32;i++)
    if (digest[i]!=aiueos_expected_initramfs_sha256[i]) return EFI_INVALID_PARAMETER;
  *kernel_size=embedded_kernel_size;
  *initramfs_size=embedded_initramfs_size;
  debug_string("AIUEOS_NETBOOT_EMBEDDED_OK kernel+initramfs sha256-v1\n");
  console_ascii("AIUEOS PXE payload admitted.\r\n");
  return EFI_SUCCESS;
}
#endif

efi_status EFIAPI efi_main(efi_handle image, struct efi_system_table *system) {
  static const char16 console_message[] = u"AIUEOS_LOADER_OK loading kernel.elf\r\n";
  static const char16 kernel_path[] = u"\\EFI\\AIUEOS\\KERNEL.ELF";
  static const char16 initramfs_path[] = u"\\EFI\\AIUEOS\\INITRD.IMG";
  struct efi_boot_services *bs;
  struct efi_loaded_image *loaded = 0;
  uint8_t *kernel_file = 0, *initramfs_file = 0;
  void *memory_map = 0;
  uint64_t kernel_size = KERNEL_BUFFER_SIZE;
  uint64_t initramfs_size = INITRAMFS_BUFFER_SIZE;
  uint64_t memory_map_size, map_key, descriptor_size;
  uint32_t descriptor_version;
  struct aiueos_boot_info info;
  struct efi_graphics_output_protocol *gop = 0;

  loader_console=system?system->console_out:0;
  loader_runtime=system?system->runtime_services:0;
  if (!system || !(bs = system->boot_services)) return fail(101,"AIUEOS_LOADER_FAIL system-table");
  if (!prepare_netboot_qualification_return())
    return fail(119,"AIUEOS_LOADER_FAIL netboot-return-arm");
  if (system->console_out && system->console_out->output_string)
    system->console_out->output_string(system->console_out, console_message);
  debug_string("AIUEOS_LOADER_OK\n");
#ifdef AIUEOS_PHYSICAL_QUALIFICATION
#ifdef AIUEOS_PERSISTENT_BOOT
  if (!bs->set_watchdog_timer ||
      bs->set_watchdog_timer(0,0,0,0)!=EFI_SUCCESS)
    return fail(120,"AIUEOS_LOADER_FAIL watchdog-disable");
  debug_string("AIUEOS_LOADER_WATCHDOG_DISABLED persistent-native\n");
  console_ascii("AIUEOS loader watchdog disabled for persistent native boot.\r\n");
#else
  if (bs->set_watchdog_timer &&
      bs->set_watchdog_timer(AIUEOS_QUALIFICATION_LOADER_WATCHDOG_SECONDS,
                             0xA106,0,0)==EFI_SUCCESS) {
    debug_string("AIUEOS_LOADER_WATCHDOG_ARMED\n");
    console_ascii("AIUEOS loader watchdog armed.\r\n");
  }
#endif
#endif
#ifdef AIUEOS_QUALIFICATION_FORCE_LOADER_HANG_CODE
  progress(AIUEOS_QUALIFICATION_FORCE_LOADER_HANG_CODE,
           "AIUEOS_LOADER_PROGRESS forced-hang");
  for (;;) __asm__ volatile("pause");
#endif
#ifdef AIUEOS_QUALIFICATION_FORCE_LOADER_FAILURE_CODE
  return fail(AIUEOS_QUALIFICATION_FORCE_LOADER_FAILURE_CODE,
              "AIUEOS_LOADER_FAIL forced-test");
#endif

  progress(201,"AIUEOS_LOADER_PROGRESS loaded-image-protocol");
  if (bs->handle_protocol(image, &loaded_image_guid, (void **)&loaded) != EFI_SUCCESS || !loaded)
    return fail(102,"AIUEOS_LOADER_FAIL loaded-image");
  progress(202,"AIUEOS_LOADER_PROGRESS kernel-buffer");
  if (bs->allocate_pool(2, KERNEL_BUFFER_SIZE, (void **)&kernel_file) != EFI_SUCCESS)
    return fail(103,"AIUEOS_LOADER_FAIL kernel-buffer");
  progress(203,"AIUEOS_LOADER_PROGRESS initramfs-buffer");
  if (bs->allocate_pool(2, INITRAMFS_BUFFER_SIZE, (void **)&initramfs_file) != EFI_SUCCESS)
    return fail(104,"AIUEOS_LOADER_FAIL initramfs-buffer");
  progress(204,"AIUEOS_LOADER_PROGRESS kernel-admission");
#ifdef AIUEOS_EMBEDDED_RELEASE
  efi_status admitted=read_verified_embedded(
      kernel_file,&kernel_size,initramfs_file,&initramfs_size);
  if (admitted!=EFI_SUCCESS)
    return fail(105,"AIUEOS_LOADER_FAIL embedded-admission");
#else
  efi_status admitted = read_verified_kernel(bs, loaded->device_handle, kernel_path,
                                             initramfs_path, kernel_file, &kernel_size,
                                             initramfs_file, &initramfs_size);
  if (admitted != EFI_SUCCESS) {
    /* Bounded recovery: every other filesystem volume may carry the same
       kernel path; admission still requires the identical compiled-in
       SHA-256, so a fallback can only load the expected kernel bytes. */
    efi_handle handles[16];
    uint64_t handle_bytes = sizeof(handles);
    if (bs->locate_handle(2, &simple_fs_guid, 0, &handle_bytes, handles) == EFI_SUCCESS) {
      uint64_t count = handle_bytes / sizeof(efi_handle);
      for (uint64_t i = 0; i < count && admitted != EFI_SUCCESS; i++) {
        if (handles[i] == loaded->device_handle) continue;
        admitted = read_verified_kernel(bs, handles[i], kernel_path,
                                        initramfs_path, kernel_file, &kernel_size,
                                        initramfs_file, &initramfs_size);
      }
    }
    if (admitted != EFI_SUCCESS)
      return fail(105,"AIUEOS_LOADER_FAIL kernel-admission-exhausted");
    debug_string("AIUEOS_LOADER_RECOVERY_OK kernel-from-alternate-volume sha256-v1\n");
  }
#endif
  debug_string("AIUEOS_LOADER_INTEGRITY_OK sha256-v1\n");

  progress(205,"AIUEOS_LOADER_PROGRESS elf-validation");
  if (kernel_size < sizeof(struct elf64_header)) return fail(106,"AIUEOS_LOADER_FAIL elf-size");
  struct elf64_header *elf = (struct elf64_header *)kernel_file;
  if (elf->ident[0] != 0x7f || elf->ident[1] != 'E' || elf->ident[2] != 'L' ||
      elf->ident[3] != 'F' || elf->ident[4] != 2 || elf->machine != 62 ||
      elf->phentsize != sizeof(struct elf64_program_header))
    return fail(107,"AIUEOS_LOADER_FAIL elf-header");
  if (elf->phoff > kernel_size || elf->phnum > 32 ||
      elf->phoff + (uint64_t)elf->phnum * elf->phentsize > kernel_size)
    return fail(108,"AIUEOS_LOADER_FAIL elf-program-table");

  struct elf64_program_header *ph = (void *)(kernel_file + elf->phoff);
  uint8_t entry_is_executable = 0;
  for (uint16_t i = 0; i < elf->phnum; i++) {
    if (ph[i].type != 1) continue;
    if (ph[i].filesz > ph[i].memsz || ph[i].offset > kernel_size ||
        ph[i].filesz > kernel_size - ph[i].offset ||
        ph[i].paddr < 0x100000 || ph[i].paddr > UINT64_MAX - ph[i].memsz ||
        (ph[i].paddr & (PAGE_SIZE - 1)) != 0)
      return fail(109,"AIUEOS_LOADER_FAIL elf-segment");
    if ((ph[i].flags & 1) && elf->entry >= ph[i].paddr &&
        elf->entry - ph[i].paddr < ph[i].memsz) entry_is_executable = 1;
    progress(206,"AIUEOS_LOADER_PROGRESS segment-allocation");
    uint64_t address = ph[i].paddr;
    uint64_t pages = (ph[i].memsz + PAGE_SIZE - 1) / PAGE_SIZE;
    if (!pages || bs->allocate_pages(2, 2, pages, &address) != EFI_SUCCESS || address != ph[i].paddr)
      return fail(110,"AIUEOS_LOADER_FAIL segment-allocation");
    copy_bytes((void *)(uintptr_t)address, kernel_file + ph[i].offset, ph[i].filesz);
    zero_bytes((void *)(uintptr_t)(address + ph[i].filesz), ph[i].memsz - ph[i].filesz);
  }
  if (!entry_is_executable) return fail(111,"AIUEOS_LOADER_FAIL elf-entry");

  progress(207,"AIUEOS_LOADER_PROGRESS memory-map-buffer");
  if (bs->allocate_pool(2, MEMORY_MAP_BUFFER_SIZE, &memory_map) != EFI_SUCCESS)
    return fail(112,"AIUEOS_LOADER_FAIL map-buffer");
  info.magic = 0x414955454f53424fULL; info.version = 3;
  info.initramfs_base = (uint64_t)(uintptr_t)initramfs_file;
  info.initramfs_size = initramfs_size;
  if (!initramfs_size) return fail(114,"AIUEOS_LOADER_FAIL initramfs-empty");
  info.acpi_rsdp = 0;
  const void *firmware_acpi_rsdp=0;
  struct efi_configuration_table *tables = system->configuration_table;
  for (uint64_t i = 0; i < system->number_of_table_entries; i++) {
    if (guid_equal(&tables[i].vendor_guid, &acpi20_guid)) {
      firmware_acpi_rsdp = tables[i].vendor_table;
      break;
    }
  }
  if (!firmware_acpi_rsdp) return fail(115,"AIUEOS_LOADER_FAIL acpi-rsdp");
  info.acpi_rsdp=retain_acpi_graph(bs,firmware_acpi_rsdp);
  if (!info.acpi_rsdp) return fail(115,"AIUEOS_LOADER_FAIL acpi-retain");
  debug_string("AIUEOS_ACPI_RETAIN_OK source=firmware copy=low-identity tables=APIC+DMAR\n");

  progress(208,"AIUEOS_LOADER_PROGRESS gop-discovery");
  uint8_t gop_used_protocol_scan=0;
  gop=find_graphics_output(system,bs,&gop_used_protocol_scan);
  if (!gop || !gop->mode ||
      !gop->mode->info || !gop->mode->framebuffer_base ||
      !gop->mode->framebuffer_size)
    return fail(116,"AIUEOS_LOADER_FAIL gop");
  struct efi_graphics_output_mode_info *gop_info = gop->mode->info;
  if (!gop_info->horizontal_resolution || !gop_info->vertical_resolution ||
      gop_info->pixels_per_scan_line < gop_info->horizontal_resolution ||
      (gop_info->pixel_format != 0 && gop_info->pixel_format != 1) ||
      (uint64_t)gop_info->pixels_per_scan_line * gop_info->vertical_resolution >
        gop->mode->framebuffer_size / 4)
    return fail(117,"AIUEOS_LOADER_FAIL gop-mode");
  info.framebuffer_base = gop->mode->framebuffer_base;
  info.framebuffer_size = gop->mode->framebuffer_size;
  info.framebuffer_width = gop_info->horizontal_resolution;
  info.framebuffer_height = gop_info->vertical_resolution;
  info.framebuffer_stride = gop_info->pixels_per_scan_line;
  info.framebuffer_format = gop_info->pixel_format;
  info.runtime_services = system->runtime_services;
  info.firmware_cr3 = read_cr3();
  if (gop_used_protocol_scan)
    debug_string("AIUEOS_GOP_DISCOVERY_OK source=protocol-scan\n");
  debug_string("AIUEOS_GOP_HANDOFF_OK framebuffer-v1\n");

  progress(209,"AIUEOS_LOADER_PROGRESS exit-boot-services");
  memory_map_size = MEMORY_MAP_BUFFER_SIZE;
  efi_status status = bs->get_memory_map(&memory_map_size, memory_map, &map_key,
                                         &descriptor_size, &descriptor_version);
  if (status != EFI_SUCCESS) return fail(113,"AIUEOS_LOADER_FAIL memory-map");
  info.memory_map = memory_map; info.memory_map_size = memory_map_size;
  info.descriptor_size = descriptor_size; info.descriptor_version = descriptor_version;
  status = bs->exit_boot_services(image, map_key);
  if (status != EFI_SUCCESS) return fail(118,"AIUEOS_LOADER_FAIL exit-boot-services");
  debug_string("AIUEOS_LOADER_PROGRESS kernel-entry-call code=211\n");
  persist_loader_record(0,211);
  ((kernel_entry)(uintptr_t)elf->entry)(&info);
  for (;;) __asm__ volatile("hlt");
}
