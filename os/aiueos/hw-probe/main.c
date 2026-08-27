#include <stdint.h>
#include <stddef.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL ((uint64_t)0x8000000000000005ULL)
#define EFI_NOT_FOUND ((uint64_t)0x8000000000000014ULL)
#define EFI_INVALID_PARAMETER ((uint64_t)0x8000000000000002ULL)
#define EFI_BY_PROTOCOL 2
#define EFI_BOOT_SERVICES_DATA 4
#define EFI_FILE_MODE_READ 1ULL
#define EFI_FILE_MODE_WRITE 2ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
#define EFI_VARIABLE_NON_VOLATILE 1U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 2U
#define EFI_VARIABLE_RUNTIME_ACCESS 4U
#define MAX_MAP_BYTES (256ULL * 1024ULL)
#define MAX_PCI_HANDLES 256
#define MAX_SERIAL_HANDLES 4
#define MAX_BLOCK_HANDLES 32
#define MAX_NATIVE_CORE_EFI (2ULL * 1024ULL * 1024ULL)
#define PROBE_LOG_CAPACITY (64ULL * 1024ULL)
#ifndef AIUEOS_HW_PROBE_DELAY_US
#define AIUEOS_HW_PROBE_DELAY_US 30000000ULL
#endif

typedef uint64_t efi_status;
typedef void *efi_handle;
typedef uint16_t char16;

struct efi_guid { uint32_t a; uint16_t b, c; uint8_t d[8]; };
struct efi_table_header { uint64_t signature; uint32_t revision, header_size, crc32, reserved; };

struct efi_simple_text_output;
typedef efi_status(EFIAPI *efi_output_string)(struct efi_simple_text_output *, const char16 *);
struct efi_simple_text_output { void *reset; efi_output_string output_string; void *rest[8]; };

typedef efi_status(EFIAPI *efi_get_memory_map)(uint64_t *, void *, uint64_t *, uint64_t *, uint32_t *);
typedef efi_status(EFIAPI *efi_allocate_pool)(uint32_t, uint64_t, void **);
typedef efi_status(EFIAPI *efi_free_pool)(void *);
typedef efi_status(EFIAPI *efi_handle_protocol)(efi_handle, const struct efi_guid *, void **);
typedef efi_status(EFIAPI *efi_locate_handle)(uint32_t, const struct efi_guid *, void *, uint64_t *, efi_handle *);
typedef efi_status(EFIAPI *efi_stall)(uint64_t);
typedef efi_status(EFIAPI *efi_load_image)(uint8_t, efi_handle, void *, void *,
                                           uint64_t, efi_handle *);
typedef efi_status(EFIAPI *efi_start_image)(efi_handle, uint64_t *, char16 **);
typedef efi_status(EFIAPI *efi_get_variable)(const char16 *, const struct efi_guid *,
                                             uint32_t *, uint64_t *, void *);
typedef efi_status(EFIAPI *efi_set_variable)(const char16 *, const struct efi_guid *,
                                             uint32_t, uint64_t, void *);

struct efi_boot_services {
  struct efi_table_header header;
  void *raise_tpl, *restore_tpl, *allocate_pages, *free_pages;
  efi_get_memory_map get_memory_map;
  efi_allocate_pool allocate_pool;
  efi_free_pool free_pool;
  void *create_event, *set_timer, *wait_for_event, *signal_event, *close_event, *check_event;
  void *install_protocol_interface, *reinstall_protocol_interface, *uninstall_protocol_interface;
  efi_handle_protocol handle_protocol;
  void *reserved, *register_protocol_notify;
  efi_locate_handle locate_handle;
  void *locate_device_path, *install_configuration_table;
  efi_load_image load_image;
  efi_start_image start_image;
  void *exit, *unload_image, *exit_boot_services, *get_next_monotonic_count;
  efi_stall stall;
};

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

struct efi_loaded_image {
  uint32_t revision, padding; efi_handle parent_handle;
  struct efi_system_table *system_table; efi_handle device_handle;
  void *file_path, *reserved; uint32_t load_options_size, padding2;
  void *load_options, *image_base; uint64_t image_size;
  uint32_t image_code_type, image_data_type; void *unload;
};

struct efi_file;
typedef efi_status(EFIAPI *efi_file_open)(struct efi_file *, struct efi_file **,
                                          const char16 *, uint64_t, uint64_t);
typedef efi_status(EFIAPI *efi_file_close)(struct efi_file *);
typedef efi_status(EFIAPI *efi_file_read)(struct efi_file *, uint64_t *, void *);
typedef efi_status(EFIAPI *efi_file_write)(struct efi_file *, uint64_t *, void *);
typedef efi_status(EFIAPI *efi_file_set_position)(struct efi_file *, uint64_t);
typedef efi_status(EFIAPI *efi_file_flush)(struct efi_file *);
struct efi_file {
  uint64_t revision; efi_file_open open; efi_file_close close;
  void *delete_file; efi_file_read read; efi_file_write write;
  void *get_position; efi_file_set_position set_position;
  void *get_info, *set_info; efi_file_flush flush;
};
struct efi_simple_file_system {
  uint64_t revision;
  efi_status(EFIAPI *open_volume)(struct efi_simple_file_system *, struct efi_file **);
};

struct efi_memory_descriptor {
  uint32_t type, padding;
  uint64_t physical_start, virtual_start, number_of_pages, attribute;
};

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

struct efi_pci_io_protocol;
typedef efi_status(EFIAPI *efi_pci_config_access)(struct efi_pci_io_protocol *, uint32_t,
                                                  uint32_t, uint64_t, void *);
struct efi_pci_access { efi_pci_config_access read, write; };
typedef efi_status(EFIAPI *efi_pci_get_location)(struct efi_pci_io_protocol *,
                                                 uint64_t *, uint64_t *, uint64_t *, uint64_t *);
struct efi_pci_io_protocol {
  void *poll_mem, *poll_io;
  void *mem_read, *mem_write, *io_read, *io_write;
  struct efi_pci_access pci;
  void *copy_mem, *map, *unmap, *allocate_buffer, *free_buffer, *flush;
  efi_pci_get_location get_location;
  void *attributes, *get_bar_attributes, *set_bar_attributes;
  uint64_t rom_size; void *rom_image;
};

struct efi_serial_io_protocol;
typedef efi_status(EFIAPI *efi_serial_write)(struct efi_serial_io_protocol *, uint64_t *, void *);
struct efi_serial_io_protocol {
  uint32_t revision, padding;
  void *reset, *set_attributes, *set_control, *get_control;
  efi_serial_write write;
  void *read, *mode;
};

struct efi_block_io_media {
  uint32_t media_id;
  uint8_t removable_media, media_present, logical_partition, read_only,
          write_caching;
  uint8_t padding[3];
  uint32_t block_size, io_align;
  uint64_t last_block;
};
struct efi_block_io_protocol {
  uint64_t revision;
  struct efi_block_io_media *media;
  void *reset, *read_blocks, *write_blocks, *flush_blocks;
};

struct aiueos_qualification_record {
  uint32_t magic;
  uint16_t version, state;
  uint32_t code, reserved;
};

static const struct efi_guid gop_guid =
  {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static const struct efi_guid acpi20_guid =
  {0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
static const struct efi_guid acpi10_guid =
  {0xeb9d2d30,0x2d88,0x11d3,{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static const struct efi_guid pci_io_guid =
  {0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
static const struct efi_guid serial_io_guid =
  {0xbb25cf6f,0xf1d4,0x11d2,{0x9a,0x0c,0x00,0x90,0x27,0x3f,0xc1,0xfd}};
static const struct efi_guid loaded_image_guid =
  {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const struct efi_guid simple_fs_guid =
  {0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const struct efi_guid block_io_guid =
  {0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const struct efi_guid efi_global_variable_guid =
  {0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};
static const struct efi_guid qualification_guid =
  {0x73953a72,0x6627,0x4b62,{0x9a,0x9c,0x10,0x38,0xd9,0x20,0x9a,0x16}};
static const char16 qualification_name[] = u"AIUEOSQualificationResult";
static const char16 boot_current_name[] = u"BootCurrent";
static const char16 boot_next_name[] = u"BootNext";

static struct efi_simple_text_output *console;
static struct efi_serial_io_protocol *serial;
static char probe_log[PROBE_LOG_CAPACITY];
static uint64_t probe_log_size;

static uint64_t ascii_length(const char *s) { uint64_t n=0; while (s[n]) n++; return n; }
static void serial_ascii(const char *s) {
  if (serial && serial->write) { uint64_t size=ascii_length(s); serial->write(serial,&size,(void *)s); }
}

static void console_ascii(const char *s) {
  char16 buf[96];
  while (*s && console && console->output_string) {
    uint32_t n = 0;
    while (*s && n < 95) buf[n++] = (uint8_t)*s++;
    buf[n] = 0;
    console->output_string(console, buf);
  }
}

static char *append_ascii(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *append_dec(char *p, uint64_t value) {
  char reverse[24]; uint32_t n = 0;
  do { reverse[n++] = (char)('0' + value % 10); value /= 10; } while (value && n < sizeof(reverse));
  while (n) *p++ = reverse[--n];
  return p;
}
static char *append_hex(char *p, uint64_t value, uint32_t digits) {
  static const char hex[] = "0123456789abcdef";
  for (uint32_t i = digits; i; i--) *p++ = hex[(value >> ((i - 1) * 4)) & 15];
  return p;
}
static void log_ascii(const char *line) {
  while (*line && probe_log_size + 1 < sizeof(probe_log))
    probe_log[probe_log_size++] = *line++;
  probe_log[probe_log_size] = 0;
}
static void emit(const char *line) {
  log_ascii(line);
  console_ascii(line);
  serial_ascii(line);
}
static void emit_value(const char *marker, uint64_t value) {
  char line[128], *p = append_ascii(line, marker); p = append_dec(p, value);
  *p++='\r'; *p++='\n'; *p=0; emit(line);
}

static int guid_equal(const struct efi_guid *a, const struct efi_guid *b) {
  const uint8_t *x=(const uint8_t *)a, *y=(const uint8_t *)b;
  for (uint32_t i=0;i<sizeof(*a);i++) if (x[i] != y[i]) return 0;
  return 1;
}
static uint64_t add_saturated(uint64_t a, uint64_t b) { return UINT64_MAX-a < b ? UINT64_MAX : a+b; }

static efi_status write_self_file(efi_handle image, struct efi_system_table *system,
                                  const char16 *path, void *payload,
                                  uint64_t payload_size) {
  struct efi_loaded_image *loaded = 0;
  struct efi_simple_file_system *fs = 0;
  struct efi_file *root = 0, *file = 0;
  struct efi_boot_services *bs = system->boot_services;
  if (bs->handle_protocol(image,&loaded_image_guid,(void **)&loaded)!=EFI_SUCCESS || !loaded ||
      bs->handle_protocol(loaded->device_handle,&simple_fs_guid,(void **)&fs)!=EFI_SUCCESS || !fs ||
      fs->open_volume(fs,&root)!=EFI_SUCCESS || !root ||
      root->open(root,&file,path,
                 EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0)!=EFI_SUCCESS ||
      !file || !file->write) {
    if (file) file->close(file);
    if (root) root->close(root);
    return EFI_INVALID_PARAMETER;
  }
  if (file->set_position && file->set_position(file,0)!=EFI_SUCCESS) {
    file->close(file);root->close(root);return EFI_INVALID_PARAMETER;
  }
  uint64_t written=payload_size;
  efi_status status=file->write(file,&written,payload);
  if (status==EFI_SUCCESS && written==payload_size && file->flush)
    status=file->flush(file);
  file->close(file);root->close(root);
  return status==EFI_SUCCESS && written==payload_size ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

static int read_qualification_record(struct efi_runtime_services *runtime,
                                     struct aiueos_qualification_record *record) {
  if (!runtime || !runtime->get_variable || !record) return 0;
  uint64_t size=sizeof(*record);uint32_t attributes=0;
  if (runtime->get_variable(qualification_name,&qualification_guid,&attributes,
                            &size,record)!=EFI_SUCCESS || size!=sizeof(*record)) return 0;
  return record->magic==0x514b3241U && record->version==2 && record->state<=2;
}

static int prepare_one_shot_return(struct efi_runtime_services *runtime) {
  if (!runtime || !runtime->get_variable || !runtime->set_variable) return 0;
  uint16_t boot_current=0;uint64_t size=sizeof(boot_current);uint32_t attributes=0;
  if (runtime->get_variable(boot_current_name,&efi_global_variable_guid,&attributes,
                            &size,&boot_current)!=EFI_SUCCESS || size!=sizeof(boot_current))
    return 0;
  uint32_t variable_attributes=EFI_VARIABLE_NON_VOLATILE|
    EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS;
  if (runtime->set_variable(boot_next_name,&efi_global_variable_guid,variable_attributes,
                            sizeof(boot_current),&boot_current)!=EFI_SUCCESS) return 0;
  struct aiueos_qualification_record pending={0x514b3241U,2,0,0,0};
  if (runtime->set_variable(qualification_name,&qualification_guid,variable_attributes,
                            sizeof(pending),&pending)!=EFI_SUCCESS) {
    runtime->set_variable(boot_next_name,&efi_global_variable_guid,0,0,0);
    return 0;
  }
  return 1;
}

static const char *result_state(uint16_t state) {
  if (state==1) return "success";
  if (state==2) return "failure";
  return "incomplete";
}

static int collect_terminal_result(efi_handle image, struct efi_system_table *system) {
  struct aiueos_qualification_record record;
  struct efi_runtime_services *runtime=system->runtime_services;
  if (!read_qualification_record(runtime,&record)) return 0;
  static char result[1024];
  char *p=append_ascii(result,"AIUEOS_K16_RESULT_V2\r\nstate=");
  p=append_ascii(p,result_state(record.state));p=append_ascii(p,"\r\ncode=");
  p=append_dec(p,record.code);
  p=append_ascii(p,"\r\ninternal_ssd_writes=none\r\nusb_log_writes=self-only\r\n");
  p=append_ascii(p,"AIUEOS_PHYSICAL_QUALIFICATION_RESULT_SAVED state=");
  p=append_ascii(p,result_state(record.state));p=append_ascii(p,"\r\n");*p=0;
  static const char16 result_path[]=u"\\EFI\\AIUEOS\\RESULT.LOG";
  if (write_self_file(image,system,result_path,result,sizeof(result))!=EFI_SUCCESS) {
    emit("AIUEOS_PHYSICAL_QUALIFICATION_RESULT_SAVE_FAIL usb-self-write\r\n");
    return -1;
  }
  if (runtime->set_variable(qualification_name,&qualification_guid,0,0,0)!=EFI_SUCCESS) {
    emit("AIUEOS_PHYSICAL_QUALIFICATION_RESULT_CLEAR_FAIL runtime-variable\r\n");
    return -1;
  }
  p=append_ascii(p,"qualification_variable_cleared=yes\r\n");*p=0;
  if (write_self_file(image,system,result_path,result,sizeof(result))!=EFI_SUCCESS) {
    emit("AIUEOS_PHYSICAL_QUALIFICATION_RESULT_FINALIZE_FAIL usb-self-write\r\n");
    return -1;
  }
  emit("AIUEOS_PHYSICAL_QUALIFICATION_RESULT_SAVED state=");
  emit(result_state(record.state));emit(" internal-ssd-writes=none\r\n");
  emit("RESULT SAVED. REMOVE USB AND CONNECT IT TO THE MAC.\r\n");
  if (system->boot_services->stall)
    for (;;) system->boot_services->stall(60000000ULL);
  return 1;
}

static void report_cpu(void) {
  uint32_t eax=0,ebx=0,ecx=0,edx=0;
  char vendor[13], line[160], *p;
  __asm__ volatile("cpuid" : "=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx) : "a"(0),"c"(0));
  *(uint32_t *)(void *)(vendor+0)=ebx;
  *(uint32_t *)(void *)(vendor+4)=edx;
  *(uint32_t *)(void *)(vendor+8)=ecx;
  vendor[12]=0;
  __asm__ volatile("cpuid" : "=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx) : "a"(1),"c"(0));
  uint32_t family=(eax>>8)&15, model=(eax>>4)&15;
  if (family==15) family += (eax>>20)&255;
  if (family==6 || family==15) model += ((eax>>16)&15)<<4;
  p=append_ascii(line,"AIUEOS_HW_PROBE_CPU vendor=");p=append_ascii(p,vendor);
  p=append_ascii(p," family=");p=append_dec(p,family);
  p=append_ascii(p," model=");p=append_dec(p,model);
  *p++='\r';*p++='\n';*p=0;emit(line);
}

static void report_firmware(struct efi_system_table *system) {
  emit("AIUEOS_HW_PROBE_FIRMWARE vendor=");
  if (system->firmware_vendor) {
    console->output_string(console, system->firmware_vendor);
    serial_ascii("present");
  } else emit("absent");
  emit(" revision=");
  char line[32], *p=append_dec(line, system->firmware_revision); *p++='\r';*p++='\n';*p=0;emit(line);
}

static void report_gop(struct efi_system_table *system, struct efi_boot_services *bs) {
  struct efi_graphics_output_protocol *gop=0;
  if (bs->handle_protocol(system->console_out_handle, &gop_guid, (void **)&gop) != EFI_SUCCESS ||
      !gop || !gop->mode || !gop->mode->info) {
    emit("AIUEOS_HW_PROBE_GOP capability=absent\r\n"); return;
  }
  struct efi_graphics_output_mode_info *info=gop->mode->info;
  char line[160], *p=append_ascii(line,"AIUEOS_HW_PROBE_GOP capability=present width=");
  p=append_dec(p,info->horizontal_resolution); p=append_ascii(p," height=");
  p=append_dec(p,info->vertical_resolution); p=append_ascii(p," stride=");
  p=append_dec(p,info->pixels_per_scan_line); *p++='\r';*p++='\n';*p=0;emit(line);

  if ((info->pixel_format == 0 || info->pixel_format == 1) && gop->mode->framebuffer_base &&
      info->pixels_per_scan_line >= info->horizontal_resolution && gop->mode->framebuffer_size >= 4) {
    uint64_t pixels=gop->mode->framebuffer_size/4;
    uint64_t rows=info->vertical_resolution < 6 ? info->vertical_resolution : 6;
    uint64_t wanted=(uint64_t)info->pixels_per_scan_line*rows;
    if (wanted < pixels) pixels=wanted;
    volatile uint32_t *fb=(volatile uint32_t *)(uintptr_t)gop->mode->framebuffer_base;
    for (uint64_t i=0;i<pixels;i++) fb[i]=(info->pixel_format==0)?0x002060d0U:0x00d06020U;
    emit("AIUEOS_HW_PROBE_GOP_VISIBLE stripe=drawn\r\n");
  } else emit("AIUEOS_HW_PROBE_GOP_VISIBLE stripe=unsupported-mode\r\n");
}

static void report_memory(struct efi_boot_services *bs) {
  uint64_t bytes=0,key=0,descriptor_size=0; uint32_t descriptor_version=0;
  efi_status status=bs->get_memory_map(&bytes,0,&key,&descriptor_size,&descriptor_version);
  if (status != EFI_BUFFER_TOO_SMALL || !descriptor_size || bytes > MAX_MAP_BYTES) {
    emit("AIUEOS_HW_PROBE_MEMORY capability=absent reason=bounded-map\r\n"); return;
  }
  if (descriptor_size > MAX_MAP_BYTES/8) {
    emit("AIUEOS_HW_PROBE_MEMORY capability=absent reason=bounded-map\r\n"); return;
  }
  bytes=add_saturated(bytes,descriptor_size*8);
  if (bytes > MAX_MAP_BYTES) { emit("AIUEOS_HW_PROBE_MEMORY capability=absent reason=bounded-map\r\n"); return; }
  void *map=0;
  if (bs->allocate_pool(EFI_BOOT_SERVICES_DATA,bytes,&map) != EFI_SUCCESS || !map) {
    emit("AIUEOS_HW_PROBE_MEMORY capability=absent reason=allocation\r\n"); return;
  }
  uint64_t actual=bytes;
  status=bs->get_memory_map(&actual,map,&key,&descriptor_size,&descriptor_version);
  if (status != EFI_SUCCESS || actual > bytes || !descriptor_size ||
      descriptor_size < sizeof(struct efi_memory_descriptor)) {
    emit("AIUEOS_HW_PROBE_MEMORY capability=absent reason=read\r\n"); bs->free_pool(map); return;
  }
  uint64_t count=actual/descriptor_size,total_pages=0,conventional_pages=0;
  for (uint64_t i=0;i<count;i++) {
    struct efi_memory_descriptor *d=(void *)((uint8_t *)map+i*descriptor_size);
    total_pages=add_saturated(total_pages,d->number_of_pages);
    if (d->type==7) conventional_pages=add_saturated(conventional_pages,d->number_of_pages);
  }
  char line[192],*p=append_ascii(line,"AIUEOS_HW_PROBE_MEMORY capability=present descriptors=");
  p=append_dec(p,count);p=append_ascii(p," total_pages=");p=append_dec(p,total_pages);
  p=append_ascii(p," conventional_pages=");p=append_dec(p,conventional_pages);
  *p++='\r';*p++='\n';*p=0;emit(line);bs->free_pool(map);
}

static void report_acpi(struct efi_system_table *system) {
  uint64_t count=system->number_of_table_entries;
  if (!system->configuration_table || count>256) {
    emit("AIUEOS_HW_PROBE_ACPI rsdp=absent reason=bounded-table\r\n"); return;
  }
  struct efi_configuration_table *tables=system->configuration_table;
  const char *version=0;
  for (uint64_t i=0;i<count;i++) {
    if (guid_equal(&tables[i].vendor_guid,&acpi20_guid) && tables[i].vendor_table) {version="2.0+";break;}
    if (guid_equal(&tables[i].vendor_guid,&acpi10_guid) && tables[i].vendor_table) version="1.0";
  }
  if (version) { emit("AIUEOS_HW_PROBE_ACPI rsdp=present version=");emit(version);emit("\r\n"); }
  else emit("AIUEOS_HW_PROBE_ACPI rsdp=absent\r\n");
}

static void report_pci(struct efi_boot_services *bs) {
  uint64_t bytes=0;
  efi_status status=bs->locate_handle(EFI_BY_PROTOCOL,&pci_io_guid,0,&bytes,0);
  if (status==EFI_NOT_FOUND || !bytes) { emit("AIUEOS_HW_PROBE_PCI capability=absent reason=no-protocol\r\n");return; }
  if (status!=EFI_BUFFER_TOO_SMALL || bytes>MAX_PCI_HANDLES*sizeof(efi_handle)) {
    emit("AIUEOS_HW_PROBE_PCI capability=absent reason=handle-bound\r\n");return;
  }
  efi_handle handles[MAX_PCI_HANDLES];
  if (bs->locate_handle(EFI_BY_PROTOCOL,&pci_io_guid,0,&bytes,handles)!=EFI_SUCCESS) {
    emit("AIUEOS_HW_PROBE_PCI capability=absent reason=enumeration\r\n");return;
  }
  uint64_t count=bytes/sizeof(efi_handle),reported=0,display=0,network=0,storage=0,nvme=0;
  for (uint64_t i=0;i<count;i++) {
    struct efi_pci_io_protocol *pci=0; uint32_t config[4]={0};
    uint64_t segment=0,bus=0,device=0,function=0;
    if (bs->handle_protocol(handles[i],&pci_io_guid,(void **)&pci)!=EFI_SUCCESS || !pci ||
        !pci->pci.read || !pci->get_location) continue;
    if (pci->get_location(pci,&segment,&bus,&device,&function)!=EFI_SUCCESS) continue;
    if (pci->pci.read(pci,2,0,4,config)!=EFI_SUCCESS) continue;
    uint16_t vendor=(uint16_t)config[0],device_id=(uint16_t)(config[0]>>16);
    if (vendor==0xffff) continue;
    char line[224],*p=append_ascii(line,"AIUEOS_HW_PROBE_PCI_DEVICE segment=");
    p=append_hex(p,segment,4);p=append_ascii(p," bdf=");p=append_hex(p,bus,2);*p++=':';
    p=append_hex(p,device,2);*p++='.';p=append_hex(p,function,1);p=append_ascii(p," vendor=");
    p=append_hex(p,vendor,4);p=append_ascii(p," device=");p=append_hex(p,device_id,4);
    p=append_ascii(p," class=");p=append_hex(p,(config[2]>>24)&0xff,2);
    p=append_hex(p,(config[2]>>16)&0xff,2);p=append_hex(p,(config[2]>>8)&0xff,2);
    *p++='\r';*p++='\n';*p=0;emit(line);reported++;
    uint32_t class_code=(config[2]>>8)&0xffffff;
    if ((class_code>>16)==3) display++;
    if ((class_code>>16)==2) network++;
    if ((class_code>>16)==1) storage++;
    if (class_code==0x010802) nvme++;
  }
  emit_value("AIUEOS_HW_PROBE_PCI capability=present devices=",reported);
  char summary[192],*p=append_ascii(summary,"AIUEOS_HW_PROBE_K16_CLASSES display=");
  p=append_dec(p,display);p=append_ascii(p," network=");p=append_dec(p,network);
  p=append_ascii(p," storage=");p=append_dec(p,storage);p=append_ascii(p," nvme=");
  p=append_dec(p,nvme);*p++='\r';*p++='\n';*p=0;emit(summary);
}

static void report_block_io(struct efi_boot_services *bs) {
  uint64_t bytes=0;
  efi_status status=bs->locate_handle(EFI_BY_PROTOCOL,&block_io_guid,0,&bytes,0);
  if (status==EFI_NOT_FOUND || !bytes) {
    emit("AIUEOS_HW_PROBE_BLOCK capability=absent\r\n");return;
  }
  if (status!=EFI_BUFFER_TOO_SMALL || bytes>sizeof(efi_handle)*MAX_BLOCK_HANDLES) {
    emit("AIUEOS_HW_PROBE_BLOCK capability=absent reason=handle-bound\r\n");return;
  }
  efi_handle handles[MAX_BLOCK_HANDLES];
  if (bs->locate_handle(EFI_BY_PROTOCOL,&block_io_guid,0,&bytes,handles)!=EFI_SUCCESS) {
    emit("AIUEOS_HW_PROBE_BLOCK capability=absent reason=enumeration\r\n");return;
  }
  uint64_t present=0,removable=0,fixed=0,partitions=0;
  for (uint64_t i=0;i<bytes/sizeof(efi_handle);i++) {
    struct efi_block_io_protocol *block=0;
    if (bs->handle_protocol(handles[i],&block_io_guid,(void **)&block)!=EFI_SUCCESS ||
        !block || !block->media || !block->media->media_present) continue;
    present++;
    if (block->media->logical_partition) partitions++;
    else if (block->media->removable_media) removable++;
    else fixed++;
  }
  char line[192],*p=append_ascii(line,"AIUEOS_HW_PROBE_BLOCK capability=present handles=");
  p=append_dec(p,present);p=append_ascii(p," whole_fixed=");p=append_dec(p,fixed);
  p=append_ascii(p," whole_removable=");p=append_dec(p,removable);
  p=append_ascii(p," partitions=");p=append_dec(p,partitions);
  *p++='\r';*p++='\n';*p=0;emit(line);
}

static efi_status start_native_core(efi_handle image, struct efi_system_table *system) {
  static const char16 path[]=u"\\EFI\\AIUEOS\\BOOTFULL.EFI";
  struct efi_boot_services *bs=system->boot_services;
  struct efi_loaded_image *parent=0,*child_loaded=0;
  struct efi_simple_file_system *fs=0;
  struct efi_file *root=0,*file=0;
  void *buffer=0; efi_handle child=0; uint64_t size=MAX_NATIVE_CORE_EFI;
  if (!bs->load_image || !bs->start_image ||
      bs->handle_protocol(image,&loaded_image_guid,(void **)&parent)!=EFI_SUCCESS || !parent ||
      bs->handle_protocol(parent->device_handle,&simple_fs_guid,(void **)&fs)!=EFI_SUCCESS || !fs ||
      fs->open_volume(fs,&root)!=EFI_SUCCESS || !root ||
      root->open(root,&file,path,1,0)!=EFI_SUCCESS || !file ||
      bs->allocate_pool(EFI_BOOT_SERVICES_DATA,size,&buffer)!=EFI_SUCCESS || !buffer) {
    emit("AIUEOS_HW_PROBE_CHAINLOAD_FAIL stage=open\r\n");return EFI_INVALID_PARAMETER;
  }
  efi_status status=file->read(file,&size,buffer);
  file->close(file);root->close(root);
  if (status!=EFI_SUCCESS || size<2 || ((uint8_t *)buffer)[0]!='M' ||
      ((uint8_t *)buffer)[1]!='Z') {
    bs->free_pool(buffer);emit("AIUEOS_HW_PROBE_CHAINLOAD_FAIL stage=read\r\n");
    return EFI_INVALID_PARAMETER;
  }
  status=bs->load_image(0,image,0,buffer,size,&child);
  bs->free_pool(buffer);
  if (status!=EFI_SUCCESS || !child) {
    emit("AIUEOS_HW_PROBE_CHAINLOAD_FAIL stage=load-image\r\n");return status;
  }
  if (bs->handle_protocol(child,&loaded_image_guid,(void **)&child_loaded)==EFI_SUCCESS && child_loaded)
    child_loaded->device_handle=parent->device_handle;
  emit("AIUEOS_HW_PROBE_CHAINLOAD_OK target=native-core-v2 internal-disk-writes=none usb-log-writes=self-only\r\n");
  status=bs->start_image(child,0,0);
  emit("AIUEOS_HW_PROBE_CHAINLOAD_FAIL stage=start-returned\r\n");
  return status;
}

static void find_serial(struct efi_boot_services *bs) {
  uint64_t bytes=sizeof(efi_handle)*MAX_SERIAL_HANDLES;
  efi_handle handles[MAX_SERIAL_HANDLES];
  if (bs->locate_handle(EFI_BY_PROTOCOL,&serial_io_guid,0,&bytes,handles)!=EFI_SUCCESS || !bytes) return;
  bs->handle_protocol(handles[0],&serial_io_guid,(void **)&serial);
}

efi_status EFIAPI efi_main(efi_handle image, struct efi_system_table *system) {
  if (!system || !system->boot_services || !system->console_out ||
      !system->console_out->output_string) return EFI_INVALID_PARAMETER;
  console=system->console_out;
  find_serial(system->boot_services);
  int collected=collect_terminal_result(image,system);
  if (collected) {
    if (system->boot_services->stall)
      for (;;) system->boot_services->stall(60000000ULL);
    return collected>0 ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
  }
  emit("\r\nAIUEOS HARDWARE PROBE (INTERNAL DISKS READ ONLY)\r\n");
  emit("AIUEOS_HW_PROBE_START mode=uefi-boot-services internal_disk_writes=disabled usb_log_writes=enabled\r\n");
  report_cpu();
  report_firmware(system);
  report_gop(system,system->boot_services);
  report_memory(system->boot_services);
  report_acpi(system);
  report_pci(system->boot_services);
  report_block_io(system->boot_services);
  emit("AIUEOS_HW_PROBE_DONE exit_boot_services=no internal_disk_writes=none\r\n");
  emit("AIUEOS_PHYSICAL_QUALIFICATION_PENDING native-core-v2 internal-ssd-writes=none\r\n");
  static const char16 probe_path[]=u"\\EFI\\AIUEOS\\PROBE.LOG";
  if (write_self_file(image,system,probe_path,probe_log,sizeof(probe_log))!=EFI_SUCCESS) {
    emit("AIUEOS_HW_PROBE_LOG_SAVE_FAIL usb-self-write\r\n");
    if (system->boot_services->stall)
      for (;;) system->boot_services->stall(60000000ULL);
    return EFI_INVALID_PARAMETER;
  }
  emit("AIUEOS_HW_PROBE_LOG_SAVED path=EFI/AIUEOS/PROBE.LOG usb-self-write\r\n");
  if (!prepare_one_shot_return(system->runtime_services)) {
    emit("AIUEOS_HW_PROBE_RETURN_ARM_FAIL bootnext-or-result-variable\r\n");
    if (system->boot_services->stall)
      for (;;) system->boot_services->stall(60000000ULL);
    return EFI_INVALID_PARAMETER;
  }
  emit("AIUEOS_HW_PROBE_RETURN_ARMED bootnext=current result=pending\r\n");
  emit("Native core starts after 30 seconds, then returns here to save RESULT.LOG.\r\n");
  if (system->boot_services->stall) system->boot_services->stall(AIUEOS_HW_PROBE_DELAY_US);
  return start_native_core(image,system);
}
