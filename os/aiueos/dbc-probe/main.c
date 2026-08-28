#include <stddef.h>
#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL ((uint64_t)0x8000000000000005ULL)
#define EFI_NOT_FOUND ((uint64_t)0x8000000000000014ULL)
#define EFI_INVALID_PARAMETER ((uint64_t)0x8000000000000002ULL)
#define EFI_BY_PROTOCOL 2
#define EFI_BOOT_SERVICES_DATA 4
#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_PCI_IO_WIDTH_UINT32 2
#define EFI_PCI_IO_WIDTH_UINT64 3
#define EFI_PCI_IO_OPERATION_COMMON_BUFFER 2
#define EFI_PCI_IO_OPERATION_COMMON_BUFFER_64 5
#define EFI_PCI_IO_ATTRIBUTE_ENABLE 2
#define EFI_PCI_IO_ATTRIBUTE_MEMORY 0x0200ULL
#define EFI_PCI_IO_ATTRIBUTE_BUS_MASTER 0x0400ULL
#define EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE 0x8000ULL
#define MAX_PCI_HANDLES 256
#define MAX_DBC_CONTROLLERS 5
#define MAX_PXE_HANDLES 8
#define MAX_XHCI_EXT_CAPS 50
#define MAX_XHCI_EXT_CAP_OFFSET 0x40000U
#define DBC_DMA_BYTES 4096ULL
#define DBC_RING_TRBS 16U
#define DBC_USABLE_TRBS 15U
#define DBC_DCE (1U << 31)
#define DBC_DCR (1U << 0)
#define DBC_DRC (1U << 4)
#define DBC_PORT_CCS (1U << 0)
#define DBC_PORT_PED (1U << 1)
#define DBC_PORT_CHANGE_MASK ((1U << 17) | (1U << 21) | (1U << 22) | (1U << 23))
#define DBC_TRB_NORMAL 1U
#define DBC_TRB_LINK 6U
#define DBC_TRB_TRANSFER_EVENT 32U
#define DBC_TRB_PORT_STATUS_EVENT 34U
#define DBC_TRB_IOC (1U << 5)
#define DBC_TRB_ISP (1U << 2)
#define DBC_LINK_TC (1U << 1)
#define DBC_COMPLETION_SUCCESS 1U
#define DBC_COMPLETION_SHORT_PACKET 13U
#define DBC_VENDOR_ID 0xffffU
#define DBC_PRODUCT_ID 0xa11eU
#define NETLOG_PORT 7777U

typedef uint64_t efi_status;
typedef void *efi_handle;
typedef uint16_t char16;

struct efi_guid { uint32_t a; uint16_t b, c; uint8_t d[8]; };
struct efi_table_header {
  uint64_t signature;
  uint32_t revision, header_size, crc32, reserved;
};

struct efi_simple_text_output;
typedef efi_status(EFIAPI *efi_output_string)(struct efi_simple_text_output *,
                                               const char16 *);
struct efi_simple_text_output {
  void *reset;
  efi_output_string output_string;
  void *rest[8];
};

typedef efi_status(EFIAPI *efi_handle_protocol)(efi_handle,
                                                const struct efi_guid *, void **);
typedef efi_status(EFIAPI *efi_locate_handle)(uint32_t, const struct efi_guid *,
                                              void *, uint64_t *, efi_handle *);
typedef efi_status(EFIAPI *efi_stall)(uint64_t);
struct efi_boot_services {
  struct efi_table_header header;
  void *raise_tpl, *restore_tpl, *allocate_pages, *free_pages, *get_memory_map;
  void *allocate_pool, *free_pool;
  void *create_event, *set_timer, *wait_for_event, *signal_event, *close_event,
       *check_event;
  void *install_protocol_interface, *reinstall_protocol_interface,
       *uninstall_protocol_interface;
  efi_handle_protocol handle_protocol;
  void *reserved, *register_protocol_notify;
  efi_locate_handle locate_handle;
  void *locate_device_path, *install_configuration_table, *load_image,
       *start_image, *exit, *unload_image, *exit_boot_services,
       *get_next_monotonic_count;
  efi_stall stall;
};

struct efi_system_table {
  struct efi_table_header header;
  char16 *firmware_vendor;
  uint32_t firmware_revision, padding;
  efi_handle console_in_handle;
  void *console_in;
  efi_handle console_out_handle;
  struct efi_simple_text_output *console_out;
  efi_handle standard_error_handle;
  struct efi_simple_text_output *standard_error;
  void *runtime_services;
  struct efi_boot_services *boot_services;
  uint64_t number_of_table_entries;
  void *configuration_table;
};

struct efi_loaded_image_protocol {
  uint32_t revision;
  efi_handle parent_handle;
  struct efi_system_table *system_table;
  efi_handle device_handle;
  void *file_path, *reserved;
  uint32_t load_options_size;
  void *load_options, *image_base;
  uint64_t image_size;
  uint32_t image_code_type, image_data_type;
  void *unload;
};

union efi_ip_address {
  uint8_t bytes[16];
  uint32_t words[4];
};

struct efi_pxe_base_code_protocol;
typedef efi_status(EFIAPI *efi_pxe_udp_write)(
    struct efi_pxe_base_code_protocol *, uint16_t, union efi_ip_address *,
    uint16_t *, union efi_ip_address *, union efi_ip_address *, uint16_t *,
    uint64_t *, void *, uint64_t *, void *);
struct efi_pxe_base_code_protocol {
  uint64_t revision;
  void *start, *stop, *dhcp, *discover, *mtftp;
  efi_pxe_udp_write udp_write;
  void *udp_read, *set_ip_filter, *arp, *set_parameters, *set_station_ip,
       *set_packets, *mode;
};

struct efi_pci_io_protocol;
typedef efi_status(EFIAPI *efi_pci_config_access)(struct efi_pci_io_protocol *,
                                                  uint32_t, uint32_t, uint64_t,
                                                  void *);
struct efi_pci_access { efi_pci_config_access read, write; };
typedef efi_status(EFIAPI *efi_pci_mem_access)(struct efi_pci_io_protocol *,
                                               uint32_t, uint8_t, uint64_t,
                                               uint64_t, void *);
struct efi_pci_mem { efi_pci_mem_access read, write; };
typedef efi_status(EFIAPI *efi_pci_map)(struct efi_pci_io_protocol *, uint32_t,
                                        void *, uint64_t *, uint64_t *, void **);
typedef efi_status(EFIAPI *efi_pci_unmap)(struct efi_pci_io_protocol *, void *);
typedef efi_status(EFIAPI *efi_pci_allocate_buffer)(struct efi_pci_io_protocol *,
                                                    uint32_t, uint32_t, uint64_t,
                                                    void **, uint64_t);
typedef efi_status(EFIAPI *efi_pci_free_buffer)(struct efi_pci_io_protocol *,
                                                uint64_t, void *);
typedef efi_status(EFIAPI *efi_pci_flush)(struct efi_pci_io_protocol *);
typedef efi_status(EFIAPI *efi_pci_get_location)(struct efi_pci_io_protocol *,
                                                 uint64_t *, uint64_t *,
                                                 uint64_t *, uint64_t *);
typedef efi_status(EFIAPI *efi_pci_attributes)(struct efi_pci_io_protocol *,
                                               uint32_t, uint64_t, uint64_t *);
struct efi_pci_io_protocol {
  void *poll_mem, *poll_io;
  struct efi_pci_mem mem, io;
  struct efi_pci_access pci;
  void *copy_mem;
  efi_pci_map map;
  efi_pci_unmap unmap;
  efi_pci_allocate_buffer allocate_buffer;
  efi_pci_free_buffer free_buffer;
  efi_pci_flush flush;
  efi_pci_get_location get_location;
  efi_pci_attributes attributes;
  void *get_bar_attributes, *set_bar_attributes;
  uint64_t rom_size;
  void *rom_image;
};

struct dbc_trb {
  uint64_t parameter;
  uint32_t status;
  uint32_t control;
} __attribute__((aligned(16)));

struct dbc_erst_entry {
  uint64_t base;
  uint32_t size;
  uint32_t reserved;
};

struct dbc_dma {
  _Alignas(64) uint8_t context[192];
  struct dbc_erst_entry erst;
  uint8_t erst_padding[48];
  _Alignas(64) struct dbc_trb event_ring[DBC_RING_TRBS];
  _Alignas(64) struct dbc_trb out_ring[DBC_RING_TRBS];
  _Alignas(64) struct dbc_trb in_ring[DBC_RING_TRBS];
  _Alignas(64) uint8_t strings[128];
  _Alignas(64) uint8_t transmit[1024];
  _Alignas(64) uint8_t receive[1024];
};

_Static_assert(offsetof(struct dbc_dma, context) == 0x000, "DbC context offset");
_Static_assert(offsetof(struct dbc_dma, erst) == 0x0c0, "DbC ERST offset");
_Static_assert(offsetof(struct dbc_dma, event_ring) == 0x100, "DbC event offset");
_Static_assert(offsetof(struct dbc_dma, out_ring) == 0x200, "DbC OUT offset");
_Static_assert(offsetof(struct dbc_dma, in_ring) == 0x300, "DbC IN offset");
_Static_assert(sizeof(struct dbc_dma) <= DBC_DMA_BYTES, "DbC DMA page overflow");

struct dbc_state {
  struct efi_pci_io_protocol *pci;
  struct dbc_dma *dma;
  void *mapping;
  uint64_t device_base;
  uint32_t capability_offset;
  uint8_t bus, device, function;
  uint8_t active, configured;
  uint8_t event_cycle, out_cycle, in_cycle;
  uint8_t event_index, out_index, in_index;
  uint8_t transmit_busy, receive_busy;
  uint32_t heartbeat_ticks, preconfigure_ticks;
  uint64_t transmit_trb, receive_trb;
  uint64_t sequence, received;
  uint32_t last_control, last_port;
};

static const struct efi_guid pci_io_guid =
  {0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
static const struct efi_guid loaded_image_guid =
  {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const struct efi_guid pxe_base_code_guid =
  {0x03c4e603,0xac28,0x11d3,{0x9a,0x2d,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static struct efi_simple_text_output *console;
static struct efi_boot_services *boot_services;
static struct efi_pxe_base_code_protocol *pxe_handles[MAX_PXE_HANDLES];
static uint32_t pxe_count;
static int32_t active_pxe=-1;
static struct dbc_state controllers[MAX_DBC_CONTROLLERS];
static uint32_t controller_count;

static uint64_t ascii_length(const char *text) {
  uint64_t length=0;
  while (text[length]) length++;
  return length;
}

static int netlog_send_with(struct efi_pxe_base_code_protocol *pxe,
                            const char *text) {
  if (!pxe || !pxe->udp_write || !text) return 0;
  union efi_ip_address destination={{10,77,0,1}};
  uint16_t port=NETLOG_PORT;
  uint64_t bytes=ascii_length(text);
  return pxe->udp_write(pxe,0,&destination,&port,0,0,0,0,0,&bytes,
                        (void *)(uintptr_t)text)==EFI_SUCCESS;
}

static void netlog_ascii(const char *text) {
  if (active_pxe>=0 && (uint32_t)active_pxe<pxe_count &&
      netlog_send_with(pxe_handles[active_pxe],text)) return;
  active_pxe=-1;
  for (uint32_t index=0;index<pxe_count;index++) {
    if (netlog_send_with(pxe_handles[index],text)) {
      active_pxe=(int32_t)index;
      return;
    }
  }
}

static void console_ascii(const char *text) {
  netlog_ascii(text);
  char16 buffer[96];
  while (*text && console && console->output_string) {
    uint32_t count = 0;
    while (*text && count < 95) buffer[count++] = (uint8_t)*text++;
    buffer[count] = 0;
    console->output_string(console, buffer);
  }
}

static void add_pxe_handle(struct efi_pxe_base_code_protocol *pxe) {
  if (!pxe || !pxe->udp_write) return;
  for (uint32_t index=0;index<pxe_count;index++)
    if (pxe_handles[index]==pxe) return;
  if (pxe_count<MAX_PXE_HANDLES) pxe_handles[pxe_count++]=pxe;
}

static void discover_netlog(efi_handle image) {
  if (!boot_services || !boot_services->handle_protocol) return;
  struct efi_loaded_image_protocol *loaded=0;
  struct efi_pxe_base_code_protocol *pxe=0;
  if (boot_services->handle_protocol(image,&loaded_image_guid,
                                     (void **)&loaded)==EFI_SUCCESS &&
      loaded && loaded->device_handle &&
      boot_services->handle_protocol(loaded->device_handle,&pxe_base_code_guid,
                                     (void **)&pxe)==EFI_SUCCESS)
    add_pxe_handle(pxe);

  if (!boot_services->locate_handle) return;
  uint64_t bytes=0;
  efi_status status=boot_services->locate_handle(
      EFI_BY_PROTOCOL,&pxe_base_code_guid,0,&bytes,0);
  if (status!=EFI_BUFFER_TOO_SMALL || !bytes ||
      bytes>MAX_PXE_HANDLES*sizeof(efi_handle)) return;
  efi_handle handles[MAX_PXE_HANDLES];
  if (boot_services->locate_handle(EFI_BY_PROTOCOL,&pxe_base_code_guid,0,
                                   &bytes,handles)!=EFI_SUCCESS) return;
  for (uint64_t index=0;index<bytes/sizeof(efi_handle);index++) {
    pxe=0;
    if (boot_services->handle_protocol(handles[index],&pxe_base_code_guid,
                                       (void **)&pxe)==EFI_SUCCESS)
      add_pxe_handle(pxe);
  }
}

static char *append_ascii(char *output, const char *text) {
  while (*text) *output++ = *text++;
  return output;
}

static char *append_dec(char *output, uint64_t value) {
  char reverse[24];
  uint32_t count = 0;
  do {
    reverse[count++] = (char)('0' + value % 10);
    value /= 10;
  } while (value && count < sizeof(reverse));
  while (count) *output++ = reverse[--count];
  return output;
}

static char *append_hex(char *output, uint64_t value, uint32_t digits) {
  static const char hex[] = "0123456789abcdef";
  for (uint32_t index = digits; index; index--)
    *output++ = hex[(value >> ((index - 1) * 4)) & 15];
  return output;
}

static void append_line_end(char **output) {
  *(*output)++ = '\r';
  *(*output)++ = '\n';
  **output = 0;
}

static void zero_bytes(void *memory, uint64_t bytes) {
  uint8_t *target = memory;
  for (uint64_t index = 0; index < bytes; index++) target[index] = 0;
}

static void copy_bytes(void *destination, const void *source, uint64_t bytes) {
  uint8_t *target = destination;
  const uint8_t *input = source;
  for (uint64_t index = 0; index < bytes; index++) target[index] = input[index];
}

static int bytes_start_with(const uint8_t *input, uint64_t input_bytes,
                            const char *prefix) {
  uint64_t index=0;
  while (prefix[index]) {
    if (index>=input_bytes || input[index]!=(uint8_t)prefix[index]) return 0;
    index++;
  }
  return 1;
}

static void dma_barrier(void) {
  __asm__ volatile("mfence" ::: "memory");
}

static int mmio_read32(struct efi_pci_io_protocol *pci, uint64_t offset,
                       uint32_t *value) {
  return pci && pci->mem.read && value &&
    pci->mem.read(pci,EFI_PCI_IO_WIDTH_UINT32,0,offset,1,value)==EFI_SUCCESS;
}

static int mmio_write32(struct efi_pci_io_protocol *pci, uint64_t offset,
                        uint32_t value) {
  return pci && pci->mem.write &&
    pci->mem.write(pci,EFI_PCI_IO_WIDTH_UINT32,0,offset,1,&value)==EFI_SUCCESS;
}

static int mmio_read64(struct efi_pci_io_protocol *pci, uint64_t offset,
                       uint64_t *value) {
  return pci && pci->mem.read && value &&
    pci->mem.read(pci,EFI_PCI_IO_WIDTH_UINT64,0,offset,1,value)==EFI_SUCCESS;
}

static int mmio_write64(struct efi_pci_io_protocol *pci, uint64_t offset,
                        uint64_t value) {
  return pci && pci->mem.write &&
    pci->mem.write(pci,EFI_PCI_IO_WIDTH_UINT64,0,offset,1,&value)==EFI_SUCCESS;
}

static int find_dbc(struct efi_pci_io_protocol *pci, uint32_t *found) {
  uint32_t hccparams = 0;
  if (!mmio_read32(pci,0x10,&hccparams)) return 0;
  uint32_t offset = ((hccparams >> 16) & 0xffffU) << 2;
  for (uint32_t visited = 0; offset && visited < MAX_XHCI_EXT_CAPS; visited++) {
    if (offset > MAX_XHCI_EXT_CAP_OFFSET - 4) return 0;
    uint32_t header = 0;
    if (!mmio_read32(pci,offset,&header)) return 0;
    if ((header & 0xffU) == 10U) {
      *found = offset;
      return 1;
    }
    uint32_t next = (header >> 8) & 0xffU;
    if (!next || offset > MAX_XHCI_EXT_CAP_OFFSET - (next << 2)) return 0;
    offset += next << 2;
  }
  return 0;
}

static uint64_t device_address(const struct dbc_state *state, const void *pointer) {
  return state->device_base +
    (uint64_t)((const uint8_t *)pointer - (const uint8_t *)state->dma);
}

static uint8_t usb_string(uint8_t *destination, uint64_t capacity,
                          const char *ascii) {
  uint64_t length = 0;
  while (ascii[length]) length++;
  if (length > 126 || 2 + length * 2 > capacity) return 0;
  destination[0] = (uint8_t)(2 + length * 2);
  destination[1] = 3;
  for (uint64_t index = 0; index < length; index++) {
    destination[2 + index * 2] = (uint8_t)ascii[index];
    destination[3 + index * 2] = 0;
  }
  return destination[0];
}

static void initialize_link(struct dbc_state *state, struct dbc_trb *ring,
                            uint8_t cycle) {
  struct dbc_trb *link = &ring[DBC_USABLE_TRBS];
  link->parameter = device_address(state,ring);
  link->status = 0;
  link->control = (DBC_TRB_LINK << 10) | DBC_LINK_TC | cycle;
}

static void initialize_endpoint(uint8_t *context, uint32_t endpoint_type,
                                uint32_t max_burst, uint64_t ring) {
  uint32_t *words = (uint32_t *)(void *)context;
  words[0] = 0;
  words[1] = (endpoint_type << 3) | (max_burst << 8) | (1024U << 16);
  *(uint64_t *)(void *)&words[2] = ring | 1U;
  words[4] = 1024U;
}

static void report_controller(const struct dbc_state *state, const char *status) {
  char line[256], *output = append_ascii(line,"AIUEOS_DBC_CONTROLLER bdf=");
  output=append_hex(output,state->bus,2);*output++=':';
  output=append_hex(output,state->device,2);*output++='.';
  output=append_hex(output,state->function,1);
  output=append_ascii(output," offset=");
  output=append_hex(output,state->capability_offset,8);
  output=append_ascii(output," state=");
  output=append_ascii(output,status);
  append_line_end(&output);
  console_ascii(line);
}

static int enable_controller(struct dbc_state *state,
                             struct efi_boot_services *boot) {
  struct efi_pci_io_protocol *pci = state->pci;
  uint32_t capability = 0, operational_status = 0;
  if (!mmio_read32(pci,0,&capability) ||
      !mmio_read32(pci,(capability & 0xffU) + 4U,&operational_status) ||
      (operational_status & (1U << 11))) {
    report_controller(state,"controller-not-ready");
    return 0;
  }
  uint32_t control = 0;
  if (!mmio_read32(pci,state->capability_offset+0x20,&control)) {
    report_controller(state,"control-unreadable");
    return 0;
  }
  if (control & DBC_DCE) {
    if (!mmio_write32(pci,state->capability_offset+0x20,0)) {
      report_controller(state,"disable-failed");
      return 0;
    }
    if (boot->stall) boot->stall(1000);
  }
  if (!pci->allocate_buffer || !pci->map ||
      (pci->attributes &&
       pci->attributes(pci,EFI_PCI_IO_ATTRIBUTE_ENABLE,
                       EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                       0)!=EFI_SUCCESS)) {
    report_controller(state,"dma-unavailable");
    return 0;
  }
  void *host = 0;
  uint8_t dma_64=1;
  efi_status allocated = pci->allocate_buffer(
      pci,EFI_ALLOCATE_ANY_PAGES,EFI_BOOT_SERVICES_DATA,1,&host,
      EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE);
  if (allocated != EFI_SUCCESS) {
    dma_64=0;
    allocated = pci->allocate_buffer(
        pci,EFI_ALLOCATE_ANY_PAGES,EFI_BOOT_SERVICES_DATA,1,&host,0);
  }
  if (allocated != EFI_SUCCESS || !host) {
    report_controller(state,"allocation-failed");
    return 0;
  }
  uint64_t bytes = DBC_DMA_BYTES, device = 0;
  void *mapping = 0;
  uint32_t operation=dma_64 ? EFI_PCI_IO_OPERATION_COMMON_BUFFER_64 :
                              EFI_PCI_IO_OPERATION_COMMON_BUFFER;
  if (pci->map(pci,operation,host,&bytes,&device,
               &mapping)!=EFI_SUCCESS || bytes != DBC_DMA_BYTES || !device) {
    if (pci->free_buffer) pci->free_buffer(pci,1,host);
    report_controller(state,"mapping-failed");
    return 0;
  }
  state->dma = host;
  state->device_base = device;
  state->mapping = mapping;
  zero_bytes(host,DBC_DMA_BYTES);

  uint8_t *strings = state->dma->strings;
  strings[0]=4;strings[1]=3;strings[2]=0x09;strings[3]=0x04;
  uint8_t language_length=4;
  uint8_t manufacturer_length=usb_string(strings+8,32,"AIUEOS");
  uint8_t product_length=usb_string(strings+40,40,"AIUEOS DbC Live");
  char serial[32], *serial_end=append_ascii(serial,"K16-");
  serial_end=append_hex(serial_end,state->bus,2);*serial_end++='-';
  serial_end=append_hex(serial_end,state->device,2);*serial_end++='-';
  serial_end=append_hex(serial_end,state->function,1);*serial_end=0;
  uint8_t serial_length=usb_string(strings+80,40,serial);
  uint64_t *info=(uint64_t *)(void *)state->dma->context;
  info[0]=device_address(state,strings);
  info[1]=device_address(state,strings+8);
  info[2]=device_address(state,strings+40);
  info[3]=device_address(state,strings+80);
  ((uint32_t *)(void *)state->dma->context)[8]=
    (uint32_t)language_length | ((uint32_t)manufacturer_length<<8) |
    ((uint32_t)product_length<<16) | ((uint32_t)serial_length<<24);

  uint32_t max_burst=(control>>16)&0xffU;
  initialize_endpoint(state->dma->context+0x40,2,max_burst,
                      device_address(state,state->dma->out_ring));
  initialize_endpoint(state->dma->context+0x80,6,max_burst,
                      device_address(state,state->dma->in_ring));
  initialize_link(state,state->dma->out_ring,0);
  initialize_link(state,state->dma->in_ring,0);
  state->dma->erst.base=device_address(state,state->dma->event_ring);
  state->dma->erst.size=DBC_RING_TRBS;
  state->event_cycle=state->out_cycle=state->in_cycle=1;

  uint64_t base=state->capability_offset;
  dma_barrier();
  if (!mmio_write32(pci,base+0x08,1) ||
      !mmio_write64(pci,base+0x10,device_address(state,&state->dma->erst)) ||
      !mmio_write64(pci,base+0x18,device_address(state,state->dma->event_ring)) ||
      !mmio_write64(pci,base+0x30,device_address(state,state->dma->context)) ||
      !mmio_write32(pci,base+0x38,(DBC_VENDOR_ID<<16)) ||
      !mmio_write32(pci,base+0x3c,(0x0100U<<16)|DBC_PRODUCT_ID)) {
    report_controller(state,"register-programming-failed");
    return 0;
  }
  if (pci->flush) pci->flush(pci);
  uint64_t context_readback=0, erst_readback=0;
  if (!mmio_read64(pci,base+0x30,&context_readback) ||
      !mmio_read64(pci,base+0x10,&erst_readback) ||
      (context_readback&~0xfULL)!=(device_address(state,state->dma->context)&~0xfULL) ||
      (erst_readback&~0xfULL)!=(device_address(state,&state->dma->erst)&~0xfULL) ||
      !mmio_write32(pci,base+0x20,DBC_DCE)) {
    report_controller(state,"enable-failed");
    return 0;
  }
  if (pci->flush) pci->flush(pci);
  state->active=1;
  state->last_control=UINT32_MAX;
  state->last_port=UINT32_MAX;
  report_controller(state,"enabled");
  return 1;
}

static void prepare_link_for_wrap(struct dbc_state *state, struct dbc_trb *ring,
                                  uint8_t index, uint8_t cycle) {
  if (index==DBC_USABLE_TRBS-1) initialize_link(state,ring,cycle);
}

static uint64_t build_heartbeat(struct dbc_state *state) {
  char message[256], *output=append_ascii(message,"AIUEOS_DBC_LIVE seq=");
  output=append_dec(output,state->sequence++);
  output=append_ascii(output," rx=");output=append_dec(output,state->received);
  output=append_ascii(output," bdf=");output=append_hex(output,state->bus,2);
  *output++=':';output=append_hex(output,state->device,2);*output++='.';
  output=append_hex(output,state->function,1);
  append_line_end(&output);
  uint64_t bytes=(uint64_t)(output-message);
  copy_bytes(state->dma->transmit,message,bytes);
  return bytes;
}

/* Per xHCI 1.2b section 7.6.3.2, the endpoint presented as IN to the Debug
   Host is cross-coupled to the DbC OUT Transfer Ring. */
static void queue_transmit(struct dbc_state *state) {
  if (state->transmit_busy) return;
  uint64_t bytes=build_heartbeat(state);
  struct dbc_trb *trb=&state->dma->out_ring[state->out_index];
  prepare_link_for_wrap(state,state->dma->out_ring,state->out_index,state->out_cycle);
  trb->parameter=device_address(state,state->dma->transmit);
  trb->status=(uint32_t)bytes;
  dma_barrier();
  trb->control=(DBC_TRB_NORMAL<<10)|DBC_TRB_IOC|state->out_cycle;
  state->transmit_trb=device_address(state,trb);
  state->transmit_busy=1;
  state->heartbeat_ticks=0;
  dma_barrier();
  if (state->pci->flush) state->pci->flush(state->pci);
  mmio_write32(state->pci,state->capability_offset+0x04,0U);
}

/* The endpoint presented as OUT to the Debug Host is cross-coupled to the
   DbC IN Transfer Ring, which owns receive buffers on the target. */
static void queue_receive(struct dbc_state *state) {
  if (state->receive_busy) return;
  struct dbc_trb *trb=&state->dma->in_ring[state->in_index];
  prepare_link_for_wrap(state,state->dma->in_ring,state->in_index,state->in_cycle);
  trb->parameter=device_address(state,state->dma->receive);
  trb->status=sizeof(state->dma->receive);
  dma_barrier();
  trb->control=(DBC_TRB_NORMAL<<10)|DBC_TRB_IOC|DBC_TRB_ISP|state->in_cycle;
  state->receive_trb=device_address(state,trb);
  state->receive_busy=1;
  dma_barrier();
  if (state->pci->flush) state->pci->flush(state->pci);
  mmio_write32(state->pci,state->capability_offset+0x04,1U<<8);
}

static void advance_ring(uint8_t *index, uint8_t *cycle) {
  (*index)++;
  if (*index==DBC_USABLE_TRBS) {
    *index=0;
    *cycle^=1U;
  }
}

static void transfer_complete(struct dbc_state *state,
                              const struct dbc_trb *event) {
  uint32_t completion=event->status>>24;
  if (event->parameter==state->transmit_trb && state->transmit_busy) {
    state->transmit_busy=0;
    advance_ring(&state->out_index,&state->out_cycle);
  } else if (event->parameter==state->receive_trb && state->receive_busy) {
    uint32_t residual=event->status&0x00ffffffU;
    uint32_t transferred=residual<=sizeof(state->dma->receive) ?
                         (uint32_t)sizeof(state->dma->receive)-residual : 0;
    if ((completion==DBC_COMPLETION_SUCCESS ||
         completion==DBC_COMPLETION_SHORT_PACKET) &&
        bytes_start_with(state->dma->receive,transferred,"AIUEOS_DBC_ACK "))
      state->received++;
    state->receive_busy=0;
    advance_ring(&state->in_index,&state->in_cycle);
  }
}

static void drain_events(struct dbc_state *state) {
  for (uint32_t count=0;count<DBC_RING_TRBS;count++) {
    volatile struct dbc_trb *volatile_event=&state->dma->event_ring[state->event_index];
    uint32_t control=volatile_event->control;
    if ((control&1U)!=state->event_cycle) break;
    struct dbc_trb event={volatile_event->parameter,volatile_event->status,control};
    uint32_t type=(event.control>>10)&0x3fU;
    if (type==DBC_TRB_TRANSFER_EVENT) transfer_complete(state,&event);
    state->event_index++;
    if (state->event_index==DBC_RING_TRBS) {
      state->event_index=0;
      state->event_cycle^=1U;
    }
    dma_barrier();
    mmio_write64(state->pci,state->capability_offset+0x18,
                 device_address(state,&state->dma->event_ring[state->event_index]));
  }
}

static void report_state(struct dbc_state *state, uint32_t control,
                         uint32_t port, uint32_t debug_status) {
  if (control==state->last_control && port==state->last_port) return;
  state->last_control=control;
  state->last_port=port;
  char line[320],*output=append_ascii(line,"AIUEOS_DBC_STATE bdf=");
  output=append_hex(output,state->bus,2);*output++=':';
  output=append_hex(output,state->device,2);*output++='.';
  output=append_hex(output,state->function,1);
  output=append_ascii(output," dce=");output=append_dec(output,(control>>31)&1U);
  output=append_ascii(output," debug_port=");output=append_dec(output,debug_status>>24);
  output=append_ascii(output," ccs=");output=append_dec(output,port&1U);
  output=append_ascii(output," ped=");output=append_dec(output,(port>>1)&1U);
  output=append_ascii(output," pls=");output=append_dec(output,(port>>5)&15U);
  output=append_ascii(output," speed=");output=append_dec(output,(port>>10)&15U);
  output=append_ascii(output," dcr=");output=append_dec(output,control&1U);
  append_line_end(&output);
  console_ascii(line);
}

static void service_controller(struct dbc_state *state) {
  if (!state->active) return;
  uint32_t control=0,status=0,port=0;
  uint64_t base=state->capability_offset;
  if (!mmio_read32(state->pci,base+0x20,&control) ||
      !mmio_read32(state->pci,base+0x24,&status) ||
      !mmio_read32(state->pci,base+0x28,&port)) return;
  report_state(state,control,port,status);
  if (!(control&DBC_DCE)) {
    console_ascii("AIUEOS_DBC_RESET_DETECTED reenable-required\r\n");
    state->active=0;
    return;
  }
  drain_events(state);
  if ((port&DBC_PORT_CHANGE_MASK) ||
      ((port&DBC_PORT_CCS) && !(port&DBC_PORT_PED))) {
    uint32_t port_write=port&DBC_PORT_CHANGE_MASK;
    if (port&DBC_PORT_CCS) port_write|=DBC_PORT_PED;
    mmio_write32(state->pci,base+0x28,port_write);
  }
  if (control&DBC_DRC)
    mmio_write32(state->pci,base+0x20,DBC_DCE|DBC_DRC);
  state->configured=(control&DBC_DCR)!=0;
  if (state->configured) {
    state->preconfigure_ticks=0;
    queue_receive(state);
    if (state->heartbeat_ticks<100) state->heartbeat_ticks++;
    if (!state->sequence || state->heartbeat_ticks>=100) queue_transmit(state);
  } else if (!state->sequence && ++state->preconfigure_ticks>=300) {
    /* The console may have scrolled past the first state transition by the
       time a technician photographs a stalled target.  Re-emit the complete
       link state immediately before each enumeration retry so the physical
       port assignment can be diagnosed without a writable log device. */
    state->last_control=UINT32_MAX;
    state->last_port=UINT32_MAX;
    report_state(state,control,port,status);
    console_ascii("AIUEOS_DBC_ENUM_RETRY dce-toggle=yes\r\n");
    mmio_write32(state->pci,base+0x20,0);
    if (state->pci->flush) state->pci->flush(state->pci);
    if (boot_services && boot_services->stall) boot_services->stall(1000);
    mmio_write32(state->pci,base+0x20,DBC_DCE);
    if (state->pci->flush) state->pci->flush(state->pci);
    state->preconfigure_ticks=0;
  }
}

static void discover_controllers(struct efi_boot_services *boot) {
  uint64_t bytes=0;
  efi_status status=boot->locate_handle(EFI_BY_PROTOCOL,&pci_io_guid,0,&bytes,0);
  if (status!=EFI_BUFFER_TOO_SMALL || !bytes ||
      bytes>MAX_PCI_HANDLES*sizeof(efi_handle)) {
    console_ascii("AIUEOS_DBC_DISCOVERY_FAIL pci-handle-bound\r\n");
    return;
  }
  efi_handle handles[MAX_PCI_HANDLES];
  if (boot->locate_handle(EFI_BY_PROTOCOL,&pci_io_guid,0,&bytes,handles)!=EFI_SUCCESS) {
    console_ascii("AIUEOS_DBC_DISCOVERY_FAIL pci-enumeration\r\n");
    return;
  }
  for (uint64_t index=0;index<bytes/sizeof(efi_handle) &&
       controller_count<MAX_DBC_CONTROLLERS;index++) {
    struct efi_pci_io_protocol *pci=0;
    uint32_t config[4]={0};
    uint64_t segment=0,bus=0,device=0,function=0;
    if (boot->handle_protocol(handles[index],&pci_io_guid,(void **)&pci)!=EFI_SUCCESS ||
        !pci || !pci->pci.read || !pci->get_location ||
        pci->get_location(pci,&segment,&bus,&device,&function)!=EFI_SUCCESS ||
        pci->pci.read(pci,EFI_PCI_IO_WIDTH_UINT32,0,4,config)!=EFI_SUCCESS ||
        ((config[2]>>8)&0xffffffU)!=0x0c0330U) continue;
    uint32_t dbc_offset=0;
    if (!find_dbc(pci,&dbc_offset)) continue;
    struct dbc_state *state=&controllers[controller_count++];
    zero_bytes(state,sizeof(*state));
    state->pci=pci;
    state->capability_offset=dbc_offset;
    state->bus=(uint8_t)bus;
    state->device=(uint8_t)device;
    state->function=(uint8_t)function;
    enable_controller(state,boot);
  }
}

efi_status EFIAPI efi_main(efi_handle image, struct efi_system_table *system) {
  (void)image;
  if (!system || !system->boot_services || !system->console_out ||
      !system->console_out->output_string) return EFI_INVALID_PARAMETER;
  console=system->console_out;
  boot_services=system->boot_services;
  discover_netlog(image);
  console_ascii("\r\nAIUEOS DBC LIVE PROBE (NO DISK WRITES)\r\n");
  console_ascii("AIUEOS_DBC_START transport=xhci-dbc direction=duplex internal-ssd-writes=none usb-log-writes=none\r\n");
  discover_controllers(system->boot_services);
  char summary[128],*output=append_ascii(summary,"AIUEOS_DBC_DISCOVERY controllers=");
  output=append_dec(output,controller_count);
  output=append_ascii(output," max=5\r\n");*output=0;
  console_ascii(summary);
  if (!controller_count) {
    console_ascii("AIUEOS_DBC_TERMINAL no-debug-capability\r\n");
  } else {
    console_ascii("AIUEOS_DBC_WAITING connect-or-replug-type-c mac-receiver-required\r\n");
  }
  for (;;) {
    for (uint32_t index=0;index<controller_count;index++)
      service_controller(&controllers[index]);
    if (system->boot_services->stall) system->boot_services->stall(10000);
  }
}
