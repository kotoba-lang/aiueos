#include <stdint.h>
#include <stddef.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL ((uint64_t)0x8000000000000005ULL)
#define EFI_NOT_FOUND ((uint64_t)0x8000000000000014ULL)
#define EFI_INVALID_PARAMETER ((uint64_t)0x8000000000000002ULL)
#define EFI_BY_PROTOCOL 2
#define EFI_BOOT_SERVICES_DATA 4
#define MAX_MAP_BYTES (256ULL * 1024ULL)
#define MAX_PCI_HANDLES 64
#define MAX_SERIAL_HANDLES 4

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
  void *locate_device_path, *install_configuration_table, *load_image, *start_image;
  void *exit, *unload_image, *exit_boot_services, *get_next_monotonic_count;
  efi_stall stall;
};

struct efi_system_table {
  struct efi_table_header header;
  char16 *firmware_vendor; uint32_t firmware_revision, padding;
  efi_handle console_in_handle; void *console_in;
  efi_handle console_out_handle; struct efi_simple_text_output *console_out;
  efi_handle standard_error_handle; struct efi_simple_text_output *standard_error;
  void *runtime_services; struct efi_boot_services *boot_services;
  uint64_t number_of_table_entries; void *configuration_table;
};
struct efi_configuration_table { struct efi_guid vendor_guid; void *vendor_table; };

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

static struct efi_simple_text_output *console;
static struct efi_serial_io_protocol *serial;

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
static void emit(const char *line) { console_ascii(line); serial_ascii(line); }
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
  uint64_t count=bytes/sizeof(efi_handle),reported=0;
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
  }
  emit_value("AIUEOS_HW_PROBE_PCI capability=present devices=",reported);
}

static void find_serial(struct efi_boot_services *bs) {
  uint64_t bytes=sizeof(efi_handle)*MAX_SERIAL_HANDLES;
  efi_handle handles[MAX_SERIAL_HANDLES];
  if (bs->locate_handle(EFI_BY_PROTOCOL,&serial_io_guid,0,&bytes,handles)!=EFI_SUCCESS || !bytes) return;
  bs->handle_protocol(handles[0],&serial_io_guid,(void **)&serial);
}

efi_status EFIAPI efi_main(efi_handle image, struct efi_system_table *system) {
  (void)image;
  if (!system || !system->boot_services || !system->console_out ||
      !system->console_out->output_string) return EFI_INVALID_PARAMETER;
  console=system->console_out;
  find_serial(system->boot_services);
  emit("\r\nAIUEOS HARDWARE PROBE (READ ONLY)\r\n");
  emit("AIUEOS_HW_PROBE_START mode=uefi-boot-services disk_writes=disabled\r\n");
  report_firmware(system);
  report_gop(system,system->boot_services);
  report_memory(system->boot_services);
  report_acpi(system);
  report_pci(system->boot_services);
  emit("AIUEOS_HW_PROBE_DONE exit_boot_services=no disk_writes=none\r\n");
  emit("Results remain visible for 30 seconds; photograph this screen.\r\n");
  if (system->boot_services->stall) system->boot_services->stall(30000000ULL);
  return EFI_SUCCESS;
}
