#include <stdint.h>
#include <stddef.h>
#include "tls13.h"

#define VIRTIO_VENDOR_ID 0x1af4
#define VIRTIO_RNG_MODERN_ID 0x1044
#define VIRTIO_RNG_TRANSITIONAL_ID 0x1005
#define VIRTIO_BLK_MODERN_ID 0x1042
#define VIRTIO_BLK_TRANSITIONAL_ID 0x1001
#define VIRTIO_INPUT_MODERN_ID 0x1052
#define VIRTIO_INPUT_TRANSITIONAL_ID 0x1012
#define VIRTIO_GPU_MODERN_ID 0x1050
#define VIRTIO_GPU_TRANSITIONAL_ID 0x1010
#define VIRTIO_NET_MODERN_ID 0x1041
#define VIRTIO_NET_TRANSITIONAL_ID 0x1000
#define PCI_STATUS_CAPABILITIES 0x10
#define PCI_CAP_VENDOR 0x09
#define PCI_CAP_MSIX 0x11
#define VIRTIO_CAP_COMMON 1
#define VIRTIO_CAP_NOTIFY 2
#define VIRTIO_CAP_DEVICE 4
#define VIRTIO_STATUS_ACK 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTQ_DESC_F_WRITE 2
#define VIRTQ_DESC_F_NEXT 1
#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_S_OK 0

extern void *aiueos_allocate_physical_page(void);
extern int aiueos_map_pci_mmio(uint64_t address, uint64_t length);
extern int aiueos_dma_test_policy_allows_unisolated(void);
extern int aiueos_vtd_translation_enabled(void);
extern int aiueos_vtd_program_msix(uint16_t source_id, uint16_t index, uint8_t vector,
                                   uint32_t apic_id, uint32_t *address, uint32_t *data);

/* PCI configuration space now lives in Kotoba (kotoba/pci-config-read.kotoba,
   kotoba/pci-config-write.kotoba). The port numbers, the CONFIG_ADDRESS word
   and both `out`/`in` instructions moved with it -- this file no longer names
   0xcf8/0xcfc and no longer contains the inline asm that reached them.

   These two wrappers remain because they keep every call site below unchanged
   and keep the uint8_t typing at the boundary; they carry no decision. The
   field-domain check that used to be implicit in `uint8_t` is made explicitly
   on the Kotoba side, which is also where the refusal is defined: a read
   refuses with 0xffffffff (the bus's own "nothing here"), a write refuses by
   returning 0 and issuing no port write at all. */
extern uint64_t kotoba_aiueos_pci_config_read(uint64_t bus, uint64_t dev,
                                              uint64_t fn, uint64_t off);
extern uint64_t kotoba_aiueos_pci_config_write(uint64_t bus, uint64_t dev,
                                               uint64_t fn, uint64_t off,
                                               uint64_t value);
/* The initial APIC ID, used below as the MSI-X message destination. Unlike the
   NX and SYSCALL probes in paging.c and process.c this is NOT a feature test --
   it extracts an 8-bit FIELD (leaf 1, EBX 31:24) rather than testing a bit, and
   what it returns is an identifier rather than a predicate. That is why it is
   its own object: the shift-and-mask is the part most likely to be got wrong,
   and getting it wrong here does not fault -- it addresses a different, legal
   message address and the interrupt is simply never delivered, which reads as a
   dead device. See kotoba/cpu-apic-id.kotoba. */
extern uint64_t kotoba_aiueos_cpu_apic_id(void);
static uint32_t config_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
  return (uint32_t)kotoba_aiueos_pci_config_read(bus, dev, fn, off);
}
static void config_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t value) {
  (void)kotoba_aiueos_pci_config_write(bus, dev, fn, off, value);
}
static uint8_t config8(uint8_t b, uint8_t d, uint8_t f, uint8_t o) {
  return (uint8_t)(config_read(b,d,f,o) >> ((o & 3) * 8));
}

struct virtio_pci_cap {
  uint8_t bar; uint32_t offset, length, notify_multiplier;
};
struct virtio_common_cfg {
  volatile uint32_t device_feature_select, device_feature;
  volatile uint32_t driver_feature_select, driver_feature;
  volatile uint16_t msix_config, num_queues;
  volatile uint8_t device_status, config_generation;
  volatile uint16_t queue_select, queue_size, queue_msix_vector, queue_enable;
  volatile uint16_t queue_notify_off;
  volatile uint64_t queue_desc, queue_driver, queue_device;
} __attribute__((packed));
struct virtq_desc { uint64_t address; uint32_t length; uint16_t flags, next; } __attribute__((packed));
struct virtq_avail { uint16_t flags, index, ring[4], used_event; } __attribute__((packed));
struct virtq_used_element { uint32_t id, length; } __attribute__((packed));
struct virtq_used { uint16_t flags, index; struct virtq_used_element ring[4]; uint16_t avail_event; } __attribute__((packed));
struct virtio_blk_request { uint32_t type, reserved; uint64_t sector; } __attribute__((packed));
/* virtio 1.0 always carries `num_buffers`, so the header is 12 bytes on both
   queues regardless of VIRTIO_NET_F_MRG_RXBUF, which is not negotiated here. */
struct virtio_net_hdr {
  uint8_t flags, gso_type; uint16_t hdr_len, gso_size, csum_start, csum_offset, num_buffers;
} __attribute__((packed));
struct virtio_input_event { uint16_t type, code; uint32_t value; } __attribute__((packed));
struct virtio_gpu_ctrl_header {
  uint32_t type, flags; uint64_t fence_id; uint32_t context_id; uint8_t ring_index, padding[3];
} __attribute__((packed));
struct virtio_gpu_rect { uint32_t x, y, width, height; } __attribute__((packed));
struct virtio_gpu_display_one {
  struct virtio_gpu_rect rect; uint32_t enabled, flags;
} __attribute__((packed));
struct virtio_gpu_display_info {
  struct virtio_gpu_ctrl_header header; struct virtio_gpu_display_one modes[16];
} __attribute__((packed));
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2
#define VIRTIO_GPU_2D_W 32
#define VIRTIO_GPU_2D_H 32
#define VIRTIO_GPU_2D_RESOURCE 1
#define VIRTIO_GPU_2D_RESOURCE_2 2
extern uint64_t kotoba_aiueos_wm_hit(uint64_t n, uint64_t front,
                                     uint64_t px, uint64_t py);
extern uint64_t kotoba_aiueos_scanout_bind(uint64_t n_resources,
                                           uint64_t n_enabled);
struct virtio_gpu_resource_create_2d {
  struct virtio_gpu_ctrl_header header;
  uint32_t resource_id, format, width, height;
} __attribute__((packed));
struct virtio_gpu_mem_entry {
  uint64_t addr; uint32_t length, padding;
} __attribute__((packed));
struct virtio_gpu_resource_attach_backing {
  struct virtio_gpu_ctrl_header header;
  uint32_t resource_id, nr_entries;
  struct virtio_gpu_mem_entry entries[1];
} __attribute__((packed));
struct virtio_gpu_set_scanout {
  struct virtio_gpu_ctrl_header header;
  struct virtio_gpu_rect r;
  uint32_t scanout_id, resource_id;
} __attribute__((packed));
struct virtio_gpu_transfer_to_host_2d {
  struct virtio_gpu_ctrl_header header;
  struct virtio_gpu_rect r;
  uint64_t offset;
  uint32_t resource_id, padding;
} __attribute__((packed));
struct virtio_gpu_resource_flush {
  struct virtio_gpu_ctrl_header header;
  struct virtio_gpu_rect r;
  uint32_t resource_id, padding;
} __attribute__((packed));
/* Kernel-to-browser.desktop-backend envelope. Raw device memory is never exposed. */
struct aiueos_desktop_input_event {
  uint32_t abi_version, byte_size;
  uint64_t sequence;
  uint32_t kind, code;
  int32_t value;
  uint32_t modifiers, flags;
} __attribute__((packed));
#define AIUEOS_DESKTOP_INPUT_ABI 1
#define AIUEOS_DESKTOP_INPUT_KEY 2
#define AIUEOS_DESKTOP_INPUT_PRESSED 1
static struct aiueos_desktop_input_event desktop_input_event;
static int desktop_input_ready;
static int desktop_input_from_eventq;
static int desktop_input_eventq_empty;
int aiueos_desktop_input_event_ready(void) { return desktop_input_ready; }
int aiueos_desktop_input_from_eventq(void) { return desktop_input_from_eventq; }
int aiueos_desktop_input_eventq_empty(void) { return desktop_input_eventq_empty; }
const struct aiueos_desktop_input_event *aiueos_desktop_input_event(void) {
  return desktop_input_ready ? &desktop_input_event : 0;
}
struct aiuefs_superblock {
  uint8_t magic[8]; uint32_t version, header_size, object_count, reserved;
  uint32_t object_offset, object_length, object_checksum;
  uint32_t catalog_sector, catalog_length;
  uint8_t catalog_sha256[32]; uint32_t catalog_signature_sector, signer_id;
  uint8_t auth_reserved[24];
} __attribute__((packed));
struct aiuefs_app_catalog { uint8_t magic[8]; uint32_t version,count; } __attribute__((packed));
struct aiuefs_app_entry { uint8_t id[16]; uint32_t sector,length; uint8_t sha256[32]; uint32_t signature_sector,signer_id; } __attribute__((packed));
struct aiuefs_journal_record {
  uint8_t magic[8]; uint32_t version, sequence, state, payload_length, payload_checksum, header_checksum;
  uint8_t payload[32];
} __attribute__((packed));
struct aiuefs_object_transaction {
  uint32_t target_sector, object_version, object_length, object_checksum;
  uint8_t object[16];
} __attribute__((packed));
struct aiuefs_mutable_object {
  uint8_t magic[8]; uint32_t version, sequence, object_length, object_checksum;
  uint8_t object[16];
} __attribute__((packed));
static int object_store_ready;
#define KOTOBA_APP_CAPACITY 4U
struct kotoba_app_metadata { uint8_t id[16]; uint32_t length; uint8_t ready; } __attribute__((packed));
static struct kotoba_app_metadata kotoba_apps[KOTOBA_APP_CAPACITY];
static uint8_t kotoba_app_objects[KOTOBA_APP_CAPACITY][12288];
static uint32_t kotoba_app_count;
static int journal_ready;
static int journal_recovered;
static uint32_t journal_sequence;
static uint32_t journal_recovered_sequence;
static uint32_t journal_slot;
static int object_transaction_replayed;
static uint32_t object_transaction_sequence;
static int service_registry_ready;
static int service_registry_replayed;
static int recovered_service_registry_ready;
static uint64_t recovered_service_registry_states[2];
static uint64_t persisted_service_registry_states[2];
struct aiueos_blk_backend {
  struct virtio_blk_request *request; uint8_t *sector,*status;
  struct virtq_desc *desc; struct virtq_avail *avail; struct virtq_used *used;
  volatile uint16_t *doorbell; uint16_t submitted; uint64_t capacity;
  volatile uint8_t lock; uint8_t ready;
};
static struct aiueos_blk_backend blk_backend;
static uint32_t user_object_sequence[2],user_object_slot[2];
static uint64_t user_object_value[2];
static uint8_t user_object_ready,user_object_write_evidence,user_object_replay_evidence;
static volatile uint64_t user_object_pending[2];
extern uint64_t kotoba_aiueos_journal_plan(uint64_t valid0, uint64_t sequence0,
                                           uint64_t valid1, uint64_t sequence1);
volatile uint64_t aiueos_virtio_blk_irq_count;
static int blk_msix_active;
int aiueos_object_store_ready(void) { return object_store_ready; }
extern uint64_t kotoba_aiueos_app_lookup_plan(
  const uint8_t[16],const struct kotoba_app_metadata*,uint64_t,uint64_t,uint64_t);
int aiueos_kotoba_app_object(const uint8_t id[16],const uint8_t **data,uint64_t *length) {
  if (!id || !data || !length) return 0;
  uint64_t plan=kotoba_aiueos_app_lookup_plan(
    id,kotoba_apps,kotoba_app_count,sizeof(kotoba_apps[0]),
    kotoba_app_count*sizeof(kotoba_apps[0]));
  if (!plan) return 0;
  uint64_t one_based=plan&7,length_plan=plan>>3;
  if (!one_based||one_based>kotoba_app_count||!length_plan||length_plan>12288) return 0;
  *data=kotoba_app_objects[one_based-1];*length=length_plan;return 1;
}
int aiueos_journal_ready(void) { return journal_ready; }
int aiueos_journal_recovered(void) { return journal_recovered; }
uint32_t aiueos_journal_sequence(void) { return journal_sequence; }
uint32_t aiueos_journal_recovered_sequence(void) { return journal_recovered_sequence; }
uint32_t aiueos_journal_slot(void) { return journal_slot; }
int aiueos_object_transaction_replayed(void) { return object_transaction_replayed; }
uint32_t aiueos_object_transaction_sequence(void) { return object_transaction_sequence; }
int aiueos_service_registry_ready(void) { return service_registry_ready; }
int aiueos_service_registry_replayed(void) { return service_registry_replayed; }
int aiueos_recovered_service_registry_ready(void) { return recovered_service_registry_ready; }
uint64_t aiueos_recovered_service_registry_state(unsigned service) {
  return service<2 && recovered_service_registry_ready ?
    recovered_service_registry_states[service] : 0;
}
uint64_t aiueos_object_store_service_state(unsigned service) {
  return service<2 && service_registry_ready ? persisted_service_registry_states[service] : 0;
}
uint64_t aiueos_user_object_receipt(uint16_t domain) {
  if(domain<4||domain>5)return 0;unsigned index=domain-4;
  return user_object_pending[index]?user_object_pending[index]:
    ((user_object_ready&(1U<<index))?user_object_value[index]:0);
}
extern uint64_t aiueos_service_registry_state(unsigned service);
extern uint64_t kotoba_aiueos_fnv1a(const uint8_t *bytes, uint64_t length);
extern uint64_t kotoba_aiueos_sha256(
  const uint8_t *,uint64_t,uint8_t[32],uint8_t *,uint64_t);
extern uint64_t kotoba_aiueos_digest_equal(
  const uint8_t[32],const uint8_t[32],uint64_t);
extern uint64_t kotoba_aiueos_app_catalog_valid(
  const uint8_t *,uint64_t,uint64_t,const uint32_t[2],uint64_t);
extern uint64_t kotoba_aiueos_rsa2048_sha256_verify(
  const uint8_t[256],const uint8_t[32],uint8_t*,uint64_t,uint64_t);
static uint8_t sha256_workspace[512];
static uint8_t rsa2048_workspace[1284];
static int catalog_policy_selftest_ok;
int aiueos_catalog_policy_selftest_ok(void) { return catalog_policy_selftest_ok; }
static int sha256(const uint8_t *bytes,uint64_t length,uint8_t digest[32]) {
  return (int)kotoba_aiueos_sha256(
    bytes,length,digest,sha256_workspace,sizeof(sha256_workspace));
}
static int rsa2048_sha256_verify(const uint8_t signature[256],const uint8_t digest[32]) {
  return (int)kotoba_aiueos_rsa2048_sha256_verify(
    signature,digest,rsa2048_workspace,sizeof(rsa2048_workspace),0);
}
/* Admit an initramfs recovery payload through the identical Kotoba SHA-256 +
   RSA-2048 public-key policy used for object-store application admission.
   Proves the carried recovery materials are usable, independent of the
   loader's whole-archive digest. */
int aiueos_recovery_payload_admission(const uint8_t *elf, uint64_t elf_length,
                                      const uint8_t *signature) {
  uint8_t digest[32];
  if (!elf || !elf_length || elf_length > 12288 || !signature) return 0;
  if (!sha256(elf, elf_length, digest)) return 0;
  return rsa2048_sha256_verify(signature, digest);
}

static uint32_t fnv1a(const uint8_t *bytes, uint32_t length) {
  return (uint32_t)kotoba_aiueos_fnv1a(bytes, length);
}
static int journal_record_valid(const struct aiuefs_journal_record *journal) {
  extern uint64_t kotoba_aiueos_journal_record_valid(const void *, uint64_t);
  return (int)kotoba_aiueos_journal_record_valid(journal, sizeof(*journal));
}

static int mutable_object_valid(const struct aiuefs_mutable_object *object, uint32_t sequence,
    const struct aiuefs_object_transaction *transaction) {
  extern uint64_t kotoba_aiueos_mutable_object_valid(
    const void *, uint64_t, uint64_t, const void *, uint64_t);
  return (int)kotoba_aiueos_mutable_object_valid(
    object, sizeof(*object), sequence, transaction, sizeof(*transaction));
}

static int service_registry_states(const struct aiuefs_object_transaction *transaction,
    uint64_t states[2]) {
  extern uint64_t kotoba_aiueos_service_registry_state(const void *,uint64_t,uint64_t);
  states[0]=kotoba_aiueos_service_registry_state(transaction,sizeof(*transaction),0);
  states[1]=kotoba_aiueos_service_registry_state(transaction,sizeof(*transaction),1);
  return states[0]!=0 && states[1]!=0;
}
static int service_registry_matches(const uint64_t states[2]) {
  return states[0]==aiueos_service_registry_state(0) &&
    states[1]==aiueos_service_registry_state(1);
}
static uint64_t user_journal_value(const struct aiuefs_journal_record *journal,
    unsigned expected_index) {
  extern uint64_t kotoba_aiueos_user_object_journal_valid(const void *,uint64_t,uint64_t);
  extern uint64_t kotoba_aiueos_user_object_journal_value(const void *,uint64_t);
  if (expected_index>=2 || !kotoba_aiueos_user_object_journal_valid(
      journal,sizeof(*journal),expected_index+4)) return 0;
  return kotoba_aiueos_user_object_journal_value(journal,sizeof(*journal));
}

static unsigned object_store_restored_count;
unsigned aiueos_object_store_restored_count(void) { return object_store_restored_count; }

static int virtio_blk_sector_io(struct virtio_blk_request *request, uint8_t *sector,
    uint8_t *status, struct virtq_desc *desc, struct virtq_avail *avail,
    struct virtq_used *used, volatile uint16_t *doorbell, uint16_t *submitted,
    uint32_t type, uint64_t disk_sector) {
  uint16_t old = *submitted, target = old + 1;
  request->type = type; request->reserved = 0; request->sector = disk_sector; *status = 0xff;
  desc[1].flags = VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0);
  avail->ring[old & 3] = 0; __asm__ volatile("" ::: "memory");
  avail->index = target; *doorbell = 0;
  for (uint32_t budget = 0; budget < 100000000U; budget++) {
    __asm__ volatile("" ::: "memory");
    if ((!blk_msix_active || aiueos_virtio_blk_irq_count >= target) && used->index == target) {
      struct virtq_used_element *completion = &used->ring[old & 3];
      uint32_t expected = type == VIRTIO_BLK_T_IN ? 513 : 1;
      if (completion->id != 0 || completion->length != expected || *status != VIRTIO_BLK_S_OK)
        return 0;
      *submitted = target;
      return 1;
    }
    if (blk_msix_active) __asm__ volatile("sti; hlt; cli" ::: "memory");
    else __asm__ volatile("pause");
  }
  return 0;
}

/* Redo a committed journal payload into its bounded object sector. The journal
   is durable before this function is called, so a reset at either I/O boundary
   is recovered by replaying the same idempotent payload on the next boot. */
static int apply_object_transaction(struct virtio_blk_request *request, uint8_t *sector,
    uint8_t *status, struct virtq_desc *desc, struct virtq_avail *avail,
    struct virtq_used *used, volatile uint16_t *doorbell, uint16_t *submitted,
    uint32_t sequence, const struct aiuefs_object_transaction *transaction,
    int recovery) {
  extern uint64_t kotoba_aiueos_object_transaction_route(const void *,uint64_t);
  uint64_t route_plan=kotoba_aiueos_object_transaction_route(
    transaction,sizeof(*transaction));
  uint32_t route=(uint32_t)route_plan,target_sector=(uint32_t)(route_plan>>32);
  if (route<1 || route>3) return 0;
  for (uint32_t i = 0; i < 512; i++) sector[i] = 0;
  struct aiuefs_mutable_object *object = (void *)sector;
  extern uint64_t kotoba_aiueos_mutable_object_build(
    void *, uint64_t, uint64_t, const void *, uint64_t);
  if (!kotoba_aiueos_mutable_object_build(
        object, 512, sequence, transaction, sizeof(*transaction))) return 0;
  if (!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,submitted,
                            VIRTIO_BLK_T_OUT,target_sector)) return 0;
  for (uint32_t i = 0; i < 512; i++) sector[i] = 0;
  if (!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,submitted,
                            VIRTIO_BLK_T_IN,target_sector)) return 0;
  object = (void *)sector;
  if (!mutable_object_valid(object,sequence,transaction)) return 0;
  if (route==1) object_transaction_sequence = sequence;
  uint64_t states[2];
  int registry=route==1 && service_registry_states(transaction,states);
  if (recovery) {
    if (registry) {
      object_transaction_replayed = 1;
      recovered_service_registry_states[0]=states[0];
      recovered_service_registry_states[1]=states[1];
      recovered_service_registry_ready=1;
    }
    if (registry && service_registry_matches(states)) service_registry_replayed = 1;
  }
  if (registry && service_registry_matches(states)) {
    service_registry_ready = 1;
    persisted_service_registry_states[0]=states[0];
    persisted_service_registry_states[1]=states[1];
  }
  return 1;
}

static void blk_lock(void) {
  while (__atomic_test_and_set(&blk_backend.lock,__ATOMIC_ACQUIRE))
    __asm__ volatile("sti; hlt; cli" ::: "memory");
  extern void aiueos_apic_timer_mask(void);aiueos_apic_timer_mask();
}
static void blk_unlock(void) {
  extern void aiueos_apic_timer_unmask(void);aiueos_apic_timer_unmask();
  __atomic_clear(&blk_backend.lock,__ATOMIC_RELEASE);
}

/* Durable crash receipt. One bounded record in a dedicated sector far above
   every aiuefs extent: magic, version, state (1 pending / 2 consumed), reason,
   the journal sequence at crash time, and a Kotoba FNV checksum over the
   preceding 24 bytes. Writes and consumption both require readback. */
#define AIUEOS_CRASH_SECTOR 1033u
struct aiueos_crash_record {
  char magic[8]; uint32_t version, state, reason, sequence, checksum;
} __attribute__((packed));
static const char crash_magic[8] = {'A','I','U','E','C','R','S','1'};

/* The synthetic panic fires from normal kernel context after the storage
   plane is proven, so crash I/O reuses the standard interrupt-driven sector
   path. Writing a receipt from a real fault context will additionally need a
   polled transport; that remains an ADR gap. */
static int crash_sector_io(uint32_t type) {
  return virtio_blk_sector_io(blk_backend.request,blk_backend.sector,
      blk_backend.status,blk_backend.desc,blk_backend.avail,blk_backend.used,
      blk_backend.doorbell,&blk_backend.submitted,type,AIUEOS_CRASH_SECTOR);
}
static uint32_t crash_record_checksum(void) {
  extern uint64_t kotoba_aiueos_fnv1a(const uint8_t *, uint64_t);
  return (uint32_t)kotoba_aiueos_fnv1a(blk_backend.sector,24);
}
static int crash_record_valid(void) {
  struct aiueos_crash_record *record=(void *)blk_backend.sector;
  for (unsigned i=0;i<8;i++) if (record->magic[i]!=crash_magic[i]) return 0;
  return record->version==1 && record->checksum==crash_record_checksum();
}

/* Crash receipt I/O runs from the boot task before user processes start, so
   there is no concurrent block-queue user and no lock is taken; taking
   blk_lock here masks the APIC timer and has been observed to prevent the
   MSI-X wake from ever arriving at this boot phase. */
int aiueos_crash_receipt_write(uint32_t reason) {
  if (!blk_backend.ready || AIUEOS_CRASH_SECTOR >= blk_backend.capacity) return 0;
  for (unsigned i=0;i<512;i++) blk_backend.sector[i]=0;
  struct aiueos_crash_record *record=(void *)blk_backend.sector;
  for (unsigned i=0;i<8;i++) record->magic[i]=crash_magic[i];
  record->version=1; record->state=1; record->reason=reason;
  record->sequence=journal_sequence;
  record->checksum=crash_record_checksum();
  uint32_t wanted=record->checksum;
  if (!crash_sector_io(VIRTIO_BLK_T_OUT)) return 0;
  for (unsigned i=0;i<512;i++) blk_backend.sector[i]=0;
  if (!crash_sector_io(VIRTIO_BLK_T_IN)) return 0;
  record=(void *)blk_backend.sector;
  return crash_record_valid() && record->state==1 &&
         record->reason==reason && record->checksum==wanted;
}

/* Fault-context variant: never sleeps on an interrupt and never blocks on
   the queue lock. A faulting kernel cannot assume a live interrupt subsystem,
   so completion is polled from the used ring; if another context holds the
   block queue mid-operation, the receipt is skipped rather than corrupting
   queue state. The caller terminates immediately afterwards, so the skew this
   leaves in the cumulative MSI-X counter is irrelevant. */
static int crash_sector_io_polled(uint32_t type) {
  struct aiueos_blk_backend *b = &blk_backend;
  uint16_t old = b->submitted, target = old + 1;
  b->request->type = type; b->request->reserved = 0;
  b->request->sector = AIUEOS_CRASH_SECTOR; *b->status = 0xff;
  b->desc[1].flags = VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0);
  b->avail->ring[old & 3] = 0; __asm__ volatile("" ::: "memory");
  b->avail->index = target; *b->doorbell = 0;
  for (uint32_t budget = 0; budget < 100000000U; budget++) {
    __asm__ volatile("" ::: "memory");
    if (b->used->index == target) {
      struct virtq_used_element *completion = &b->used->ring[old & 3];
      uint32_t expected = type == VIRTIO_BLK_T_IN ? 513 : 1;
      if (completion->id != 0 || completion->length != expected ||
          *b->status != VIRTIO_BLK_S_OK) return 0;
      b->submitted = target;
      return 1;
    }
    __asm__ volatile("pause");
  }
  return 0;
}

int aiueos_crash_receipt_write_from_fault(uint32_t reason) {
  if (!blk_backend.ready || AIUEOS_CRASH_SECTOR >= blk_backend.capacity) return 0;
  if (__atomic_test_and_set(&blk_backend.lock,__ATOMIC_ACQUIRE)) return 0;
  for (unsigned i=0;i<512;i++) blk_backend.sector[i]=0;
  struct aiueos_crash_record *record=(void *)blk_backend.sector;
  for (unsigned i=0;i<8;i++) record->magic[i]=crash_magic[i];
  record->version=1; record->state=1; record->reason=reason;
  record->sequence=journal_sequence;
  record->checksum=crash_record_checksum();
  uint32_t wanted=record->checksum;
  int ok = crash_sector_io_polled(VIRTIO_BLK_T_OUT);
  if (ok) {
    for (unsigned i=0;i<512;i++) blk_backend.sector[i]=0;
    ok = crash_sector_io_polled(VIRTIO_BLK_T_IN);
  }
  if (ok) {
    record=(void *)blk_backend.sector;
    ok = crash_record_valid() && record->state==1 &&
         record->reason==reason && record->checksum==wanted;
  }
  __atomic_clear(&blk_backend.lock,__ATOMIC_RELEASE);
  return ok;
}

/* Returns 1 and marks the record consumed when a valid pending crash receipt
   exists; 0 when the sector is empty, consumed, or invalid. */
int aiueos_crash_receipt_consume(uint32_t *reason, uint32_t *sequence) {
  if (!blk_backend.ready || !reason || !sequence ||
      AIUEOS_CRASH_SECTOR >= blk_backend.capacity) return 0;
  for (unsigned i=0;i<512;i++) blk_backend.sector[i]=0;
  if (!crash_sector_io(VIRTIO_BLK_T_IN)) return 0;
  struct aiueos_crash_record *record=(void *)blk_backend.sector;
  if (!crash_record_valid() || record->state!=1) return 0;
  *reason=record->reason; *sequence=record->sequence;
  record->state=2;
  record->checksum=crash_record_checksum();
  uint32_t wanted=record->checksum;
  if (!crash_sector_io(VIRTIO_BLK_T_OUT)) return 0;
  for (unsigned i=0;i<512;i++) blk_backend.sector[i]=0;
  if (!crash_sector_io(VIRTIO_BLK_T_IN)) return 0;
  record=(void *)blk_backend.sector;
  return crash_record_valid() && record->state==2 && record->checksum==wanted;
}

static uint64_t commit_user_object_write(uint16_t domain,uint64_t value) {
  if (!blk_backend.ready || domain<4 || domain>5 || !value || value>0xffffffffU) return 0;
  unsigned index=domain-4; blk_lock();
  uint32_t sequence=user_object_sequence[index]+1;
  uint32_t first_slot=44+(index*2);
  uint32_t target=user_object_slot[index]==first_slot ? first_slot+1 : first_slot;
  if (!sequence || sequence>999 || target>=blk_backend.capacity) { blk_unlock(); return 0; }
  for(unsigned i=0;i<512;i++)blk_backend.sector[i]=0;
  extern uint64_t kotoba_aiueos_user_object_journal_build(
    void *,uint64_t,uint64_t,uint64_t,uint64_t);
  if(!kotoba_aiueos_user_object_journal_build(
      blk_backend.sector,512,sequence,domain,value)) { blk_unlock();return 0; }
  if (!virtio_blk_sector_io(blk_backend.request,blk_backend.sector,blk_backend.status,
        blk_backend.desc,blk_backend.avail,blk_backend.used,blk_backend.doorbell,
        &blk_backend.submitted,VIRTIO_BLK_T_OUT,target)) { blk_unlock(); return 0; }
  for(unsigned i=0;i<512;i++)blk_backend.sector[i]=0;
  if (!virtio_blk_sector_io(blk_backend.request,blk_backend.sector,blk_backend.status,
        blk_backend.desc,blk_backend.avail,blk_backend.used,blk_backend.doorbell,
        &blk_backend.submitted,VIRTIO_BLK_T_IN,target)) { blk_unlock(); return 0; }
  struct aiuefs_journal_record *journal=(void *)blk_backend.sector;
  uint64_t decoded_value=user_journal_value(journal,index);
  if (decoded_value!=value||journal->sequence!=sequence) { blk_unlock();return 0; }
  struct aiuefs_object_transaction transaction=
    *(const struct aiuefs_object_transaction *)(const void *)journal->payload;
  if (!apply_object_transaction(blk_backend.request,
        blk_backend.sector,blk_backend.status,blk_backend.desc,blk_backend.avail,
        blk_backend.used,blk_backend.doorbell,&blk_backend.submitted,sequence,
        &transaction,0)) {
    blk_unlock();return 0;
  }
  user_object_value[index]=decoded_value;user_object_ready|=1U<<index;
  user_object_sequence[index]=sequence;user_object_slot[index]=target;
  user_object_write_evidence|=1U<<index;blk_unlock();return sequence;
}
uint64_t aiueos_user_object_write(uint16_t domain,uint64_t value) {
  if(!blk_backend.ready||domain<4||domain>5||!value||value>0xffffffffU)return 0;
  unsigned index=domain-4;
  if(user_object_pending[index])return 0;
  user_object_pending[index]=value;
  return (uint64_t)user_object_sequence[index]+1;
}
int aiueos_user_object_flush_pending(void) {
  int committed=0;
  for(unsigned index=0;index<2;index++) {
    uint64_t value=user_object_pending[index];
    if(value) {
      if(!commit_user_object_write((uint16_t)(index+4),value))return 0;
      user_object_pending[index]=0;committed++;
    }
  }
  return committed;
}
int aiueos_user_object_write_evidence_ready(void) {
  return user_object_write_evidence==3 && user_object_ready==3 &&
    user_object_value[0]==42 && user_object_value[1]==42;
}
int aiueos_user_object_replay_evidence_ready(void) {
  return user_object_replay_evidence==3 && user_object_ready==3;
}

struct virtio_caps {
  struct virtio_pci_cap common, notify, device;
  int have_common, have_notify, have_device;
  uint8_t msix_pointer;
};

extern uint64_t kotoba_aiueos_virtio_cap_valid(
  uint64_t pointer, uint64_t cap_length, uint64_t bar, uint64_t offset, uint64_t length);
extern uint64_t kotoba_aiueos_pci_extent_valid(uint64_t value, uint64_t size);
extern uint64_t kotoba_aiueos_pci_region_valid(
  uint64_t offset, uint64_t bytes, uint64_t bar_length);
static int cap_selftest(void) {
  return kotoba_aiueos_virtio_cap_valid(0x40,20,0,0x1000,0x38) &&
         !kotoba_aiueos_virtio_cap_valid(0x40,20,0,0xfffffff0U,0x40) &&
         !kotoba_aiueos_virtio_cap_valid(0x40,20,0,0,0);
}
static int read_bar(uint8_t b, uint8_t d, uint8_t f, uint8_t index, uint64_t *base) {
  if (index >= 6 || !base) return 0;
  uint32_t low = config_read(b,d,f,(uint8_t)(0x10 + index * 4));
  if (low & 1) return 0; /* Port BARs cannot carry modern virtio capabilities. */
  uint32_t type = (low >> 1) & 3;
  uint64_t value = low & ~0xfU;
  if (type == 2) {
    if (index == 5) return 0;
    value |= (uint64_t)config_read(b,d,f,(uint8_t)(0x14 + index * 4)) << 32;
  } else if (type != 0) return 0;
  if (!value || value == 0xfffffff0ULL) return 0;
  *base = value; return 1;
}
static int parse_cap(uint8_t b, uint8_t d, uint8_t f, uint8_t pointer,
                     struct virtio_pci_cap *cap) {
  uint8_t cap_len = config8(b,d,f,pointer + 2);
  cap->bar = config8(b,d,f,pointer + 4);
  cap->offset = config_read(b,d,f,pointer + 8);
  cap->length = config_read(b,d,f,pointer + 12);
  cap->notify_multiplier = cap_len >= 20 ? config_read(b,d,f,pointer + 16) : 0;
  return (int)kotoba_aiueos_virtio_cap_valid(
    pointer,cap_len,cap->bar,cap->offset,cap->length);
}

static int find_virtio_caps(uint8_t b, uint8_t d, uint8_t f,
                            struct virtio_caps *caps) {
  uint16_t status = (uint16_t)(config_read(b,d,f,0x04) >> 16);
  if (!(status & PCI_STATUS_CAPABILITIES)) return 0;
  *caps = (struct virtio_caps){0};
  uint8_t pointer = config8(b,d,f,0x34) & ~3U;
  uint64_t seen = 0;
  unsigned steps = 0;
  for (; pointer && steps < 48; steps++) {
    if (pointer < 0x40 || pointer > 0xfc || (pointer & 3)) return 0;
    uint64_t bit = 1ULL << ((pointer - 0x40) >> 2);
    if (seen & bit) return 0; seen |= bit;
    uint8_t next = config8(b,d,f,pointer + 1) & ~3U;
    if (config8(b,d,f,pointer) == PCI_CAP_MSIX) {
      if (pointer > 0xf4 || caps->msix_pointer) return 0;
      caps->msix_pointer = pointer;
    }
    if (config8(b,d,f,pointer) == PCI_CAP_VENDOR) {
      uint8_t kind = config8(b,d,f,pointer + 3);
      if (kind == VIRTIO_CAP_COMMON || kind == VIRTIO_CAP_NOTIFY || kind == VIRTIO_CAP_DEVICE) {
        struct virtio_pci_cap parsed;
        if (!parse_cap(b,d,f,pointer,&parsed)) return 0;
        if (kind == VIRTIO_CAP_COMMON) { caps->common = parsed; caps->have_common = 1; }
        if (kind == VIRTIO_CAP_NOTIFY) { caps->notify = parsed; caps->have_notify = 1; }
        if (kind == VIRTIO_CAP_DEVICE) { caps->device = parsed; caps->have_device = 1; }
      }
    }
    pointer = next;
  }
  if (pointer) return 0; /* Capability chain exceeded the bounded walk. */
  return caps->have_common && caps->have_notify &&
         caps->common.length >= sizeof(struct virtio_common_cfg) &&
         caps->notify.length >= 2 && caps->notify.notify_multiplier;
}

static int bar_extent(uint8_t b, uint8_t d, uint8_t f, uint8_t index,
                      uint64_t *base, uint64_t *length) {
  if (index >= 6 || !base || !length) return 0;
  uint8_t offset = (uint8_t)(0x10 + index * 4);
  uint32_t command = config_read(b,d,f,0x04);
  uint32_t low = config_read(b,d,f,offset), high = 0;
  if ((low & 1) || (((low >> 1) & 3) != 0 && ((low >> 1) & 3) != 2)) return 0;
  int wide = ((low >> 1) & 3) == 2;
  if (wide) { if (index == 5) return 0; high = config_read(b,d,f,offset + 4); }
  config_write(b,d,f,0x04,command & ~3U);
  config_write(b,d,f,offset,0xffffffffU);
  if (wide) config_write(b,d,f,offset + 4,0xffffffffU);
  uint64_t mask = (uint64_t)(config_read(b,d,f,offset) & ~0xfU);
  if (wide) mask |= (uint64_t)config_read(b,d,f,offset + 4) << 32;
  config_write(b,d,f,offset,low);
  if (wide) config_write(b,d,f,offset + 4,high);
  config_write(b,d,f,0x04,command);
  uint64_t value = (uint64_t)(low & ~0xfU) | ((uint64_t)high << 32);
  uint64_t size = wide ? (~mask) + 1 : (uint64_t)(~(uint32_t)mask + 1U);
  if (!kotoba_aiueos_pci_extent_valid(value,size) || value + size < value) return 0;
  *base = value; *length = size; return 1;
}

struct msix_entry {
  volatile uint32_t address_low, address_high, data, vector_control;
};
volatile uint64_t aiueos_virtio_rng_irq_count;

/* The random device kept alive after enumeration so the OS has an entropy
   source rather than a boolean that it once completed a request. Before this
   the 32 bytes virtio-rng returned were written into a stack page and thrown
   away (only rng_ok survived). An SSH server needs fresh bytes at handshake
   time -- an ephemeral kex scalar and a KEXINIT cookie -- so the queue, its
   pages and its doorbell are persisted here, the way blk_backend persists the
   block device. */
static struct {
  volatile struct virtio_common_cfg *cfg;
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  volatile uint16_t *doorbell;
  uint8_t *page;
  uint16_t posted;
  int ready;
} rng_backend;

/* Request one 32-byte batch from the device into rng_backend.page, bounded.
   Returns 1 on a completed 32-byte fill. */
static int rng_refill(void) {
  if (!rng_backend.ready) return 0;
  rng_backend.desc[0].address = (uint64_t)(uintptr_t)rng_backend.page;
  rng_backend.desc[0].length = 32;
  rng_backend.desc[0].flags = VIRTQ_DESC_F_WRITE;
  rng_backend.desc[0].next = 0;
  uint64_t before = aiueos_virtio_rng_irq_count;
  rng_backend.avail->ring[0] = 0;
  __asm__ volatile("" ::: "memory");
  rng_backend.posted++;
  rng_backend.avail->index = rng_backend.posted;
  *rng_backend.doorbell = 0;
  for (uint32_t budget = 0; budget < 100000000U; budget++) {
    __asm__ volatile("" ::: "memory");
    if (aiueos_virtio_rng_irq_count != before &&
        rng_backend.used->index == rng_backend.posted) {
      return rng_backend.used->ring[0].length == 32;
    }
    __asm__ volatile("sti; hlt; cli" ::: "memory");
  }
  return 0;
}

/* Fill OUT with N random bytes, requesting fresh 32-byte batches as needed.
   Returns 1 on success, 0 if the device is not ready or a request stalled. A
   caller that gets 0 has NO usable bytes -- it must not proceed with a weak or
   constant key. */
int aiueos_random_bytes(uint8_t *out, uint32_t n) {
  uint32_t filled = 0;
  while (filled < n) {
    if (!rng_refill()) return 0;
    uint32_t take = n - filled;
    if (take > 32) take = 32;
    for (uint32_t i = 0; i < take; i++) out[filled + i] = rng_backend.page[i];
    filled += take;
  }
  return 1;
}

/* Self-test evidence: two batches must both be non-constant and must differ
   from each other. A device that returned a fixed page -- or an API that
   handed back the same buffer twice -- would fail both, which is the whole
   point of asking twice. */
static int rng_selftest_done, rng_selftest_ok;
int aiueos_random_selftest(void) {
  if (rng_selftest_done) return rng_selftest_ok;
  rng_selftest_done = 1;
  uint8_t a[32], b[32];
  if (!aiueos_random_bytes(a, 32) || !aiueos_random_bytes(b, 32)) return 0;
  int a_varies = 0, b_varies = 0, differ = 0;
  for (unsigned i = 1; i < 32; i++) { if (a[i] != a[0]) a_varies = 1; if (b[i] != b[0]) b_varies = 1; }
  for (unsigned i = 0; i < 32; i++) if (a[i] != b[i]) differ = 1;
  rng_selftest_ok = a_varies && b_varies && differ;
  return rng_selftest_ok;
}

static int setup_rng_msix(uint8_t b, uint8_t d, uint8_t f,
                          const struct virtio_caps *caps,
                          volatile struct virtio_common_cfg *cfg) {
  if (!caps->msix_pointer) return 0;
  uint8_t pointer = caps->msix_pointer;
  uint32_t header = config_read(b,d,f,pointer);
  uint32_t table = config_read(b,d,f,pointer + 4);
  uint32_t pba = config_read(b,d,f,pointer + 8);
  uint32_t vectors = ((header >> 16) & 0x7ffU) + 1U;
  uint8_t table_bar = table & 7U, pba_bar = pba & 7U;
  uint64_t table_base, table_bar_length, pba_base, pba_bar_length;
  uint64_t table_offset = table & ~7U, pba_offset = pba & ~7U;
  uint64_t table_bytes = (uint64_t)vectors * sizeof(struct msix_entry);
  uint64_t pba_bytes = ((uint64_t)vectors + 63) / 64 * 8;
  if (vectors > 2048 || table_bar >= 6 || pba_bar >= 6) return 0;
  if (!bar_extent(b,d,f,table_bar,&table_base,&table_bar_length) ||
      !bar_extent(b,d,f,pba_bar,&pba_base,&pba_bar_length)) return 0;
  if (!kotoba_aiueos_pci_region_valid(table_offset,table_bytes,table_bar_length) ||
      !kotoba_aiueos_pci_region_valid(pba_offset,pba_bytes,pba_bar_length)) return 0;
  if (!aiueos_map_pci_mmio(table_base + table_offset,table_bytes) ||
      !aiueos_map_pci_mmio(pba_base + pba_offset,pba_bytes)) return 0;
  struct msix_entry *entry = (void *)(uintptr_t)(table_base + table_offset);
  entry[0].vector_control = 1;
  entry[0].address_low =
    0xfee00000U | ((uint32_t)kotoba_aiueos_cpu_apic_id() << 12);
  entry[0].address_high = 0;
  entry[0].data = 34;
  __asm__ volatile("" ::: "memory");
  cfg->queue_msix_vector = 0;
  if (cfg->queue_msix_vector != 0) return 0;
  entry[0].vector_control = 0;
  config_write(b,d,f,pointer,header | (1U << 31)); /* enable; function mask clear */
  if (!(config_read(b,d,f,pointer) & (1U << 31))) return 0;
  aiueos_virtio_rng_irq_count = 0;
  return 1;
}

/* Keep the block device on a distinct architectural vector.  The capability,
   BAR, table and PBA bounds are revalidated per device; no address learned
   from the rng function is reused. */
static int setup_blk_msix(uint8_t b, uint8_t d, uint8_t f,
                          const struct virtio_caps *caps,
                          volatile struct virtio_common_cfg *cfg) {
  if (!caps->msix_pointer) return 0;
  uint8_t pointer = caps->msix_pointer;
  uint32_t header = config_read(b,d,f,pointer);
  uint32_t table = config_read(b,d,f,pointer + 4);
  uint32_t pba = config_read(b,d,f,pointer + 8);
  uint32_t vectors = ((header >> 16) & 0x7ffU) + 1U;
  uint8_t table_bar = table & 7U, pba_bar = pba & 7U;
  uint64_t table_base, table_bar_length, pba_base, pba_bar_length;
  uint64_t table_offset = table & ~7U, pba_offset = pba & ~7U;
  uint64_t table_bytes = (uint64_t)vectors * sizeof(struct msix_entry);
  uint64_t pba_bytes = ((uint64_t)vectors + 63) / 64 * 8;
  if (vectors < 2 || vectors > 2048 || table_bar >= 6 || pba_bar >= 6) return 0;
  if (!bar_extent(b,d,f,table_bar,&table_base,&table_bar_length) ||
      !bar_extent(b,d,f,pba_bar,&pba_base,&pba_bar_length)) return 0;
  if (!kotoba_aiueos_pci_region_valid(table_offset,table_bytes,table_bar_length) ||
      !kotoba_aiueos_pci_region_valid(pba_offset,pba_bytes,pba_bar_length)) return 0;
  if (!aiueos_map_pci_mmio(table_base + table_offset,table_bytes) ||
      !aiueos_map_pci_mmio(pba_base + pba_offset,pba_bytes)) return 0;
  struct msix_entry *entry = (void *)(uintptr_t)(table_base + table_offset);
  /* `destination` survives here, unlike in setup_rng_msix, because
     aiueos_vtd_program_msix takes it as well -- with interrupt remapping on it
     may rewrite both the address and the data below. */
  uint32_t destination = (uint32_t)kotoba_aiueos_cpu_apic_id();
  uint32_t message_address = 0xfee00000U | (destination << 12), message_data = 35;
  if (aiueos_vtd_translation_enabled() &&
      !aiueos_vtd_program_msix(((uint16_t)b << 8) | ((uint16_t)d << 3) | f,
                               1,35,destination,&message_address,&message_data)) return 0;
  entry[1].vector_control = 1;
  entry[1].address_low = message_address;
  entry[1].address_high = 0;
  entry[1].data = message_data;
  __asm__ volatile("" ::: "memory");
  cfg->queue_msix_vector = 1;
  if (cfg->queue_msix_vector != 1) return 0;
  entry[1].vector_control = 0;
  config_write(b,d,f,pointer,header | (1U << 31));
  if (!(config_read(b,d,f,pointer) & (1U << 31))) return 0;
  aiueos_virtio_blk_irq_count = 0;
  return 1;
}

static int map_transport(uint8_t b, uint8_t d, uint8_t f, const struct virtio_caps *caps,
                         volatile struct virtio_common_cfg **cfg_out,
                         uint64_t *notify_base_out) {
  uint64_t common_bar, notify_bar;
  if (!read_bar(b,d,f,caps->common.bar,&common_bar) ||
      !read_bar(b,d,f,caps->notify.bar,&notify_bar) ||
      common_bar + caps->common.offset < common_bar ||
      notify_bar + caps->notify.offset < notify_bar ||
      !aiueos_map_pci_mmio(common_bar + caps->common.offset, caps->common.length) ||
      !aiueos_map_pci_mmio(notify_bar + caps->notify.offset, caps->notify.length)) return 0;
  *cfg_out = (volatile void *)(uintptr_t)(common_bar + caps->common.offset);
  *notify_base_out = notify_bar + caps->notify.offset;
  return 1;
}

static int negotiate(volatile struct virtio_common_cfg *cfg) {
  cfg->device_status = 0;
  cfg->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER;
  cfg->device_feature_select = 1;
  if (!(cfg->device_feature & 1U)) return 0; /* VIRTIO_F_VERSION_1, bit 32 */
  cfg->driver_feature_select = 1; cfg->driver_feature = 1U;
  cfg->driver_feature_select = 0; cfg->driver_feature = 0;
  cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
  if (!(cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) return 0;
  return 1;
}

/* Every device before virtio-net used exactly one queue, so `prepare_queue`
   selected queue 0 by construction. virtio-net needs two (0 = receive,
   1 = transmit), and each has its own notify offset, so the index becomes a
   parameter and the single-queue helper below keeps its old signature. */
static volatile uint16_t *prepare_queue_index(volatile struct virtio_common_cfg *cfg,
                                              const struct virtio_caps *caps,
                                              uint64_t notify_base, uint16_t index,
                                              uint16_t size,
                                              struct virtq_desc *desc,
                                              struct virtq_avail *avail,
                                              struct virtq_used *used) {
  cfg->queue_select = index;
  if (cfg->queue_size < size || cfg->queue_enable) return 0;
  cfg->queue_size = size;
  cfg->queue_desc = (uint64_t)(uintptr_t)desc;
  cfg->queue_driver = (uint64_t)(uintptr_t)avail;
  cfg->queue_device = (uint64_t)(uintptr_t)used;
  cfg->queue_enable = 1;
  uint64_t delta = (uint64_t)cfg->queue_notify_off * caps->notify.notify_multiplier;
  if (delta + 2 < delta || delta + 2 > caps->notify.length) return 0;
  return (volatile void *)(uintptr_t)(notify_base + delta);
}

static volatile uint16_t *prepare_queue(volatile struct virtio_common_cfg *cfg,
                                        const struct virtio_caps *caps,
                                        uint64_t notify_base, uint16_t size,
                                        struct virtq_desc *desc,
                                        struct virtq_avail *avail,
                                        struct virtq_used *used) {
  return prepare_queue_index(cfg,caps,notify_base,0,size,desc,avail,used);
}

static int virtio_rng(uint8_t b, uint8_t d, uint8_t f) {
  struct virtio_caps caps;
  volatile struct virtio_common_cfg *cfg;
  uint64_t notify_base;
  if (!find_virtio_caps(b,d,f,&caps) || !map_transport(b,d,f,&caps,&cfg,&notify_base) ||
      !negotiate(cfg)) return 0;
  struct virtq_desc *desc = aiueos_allocate_physical_page();
  struct virtq_avail *avail = aiueos_allocate_physical_page();
  struct virtq_used *used = aiueos_allocate_physical_page();
  uint8_t *random = aiueos_allocate_physical_page();
  if (!desc || !avail || !used || !random) return 0;
  desc[0].address = (uint64_t)(uintptr_t)random; desc[0].length = 32;
  desc[0].flags = VIRTQ_DESC_F_WRITE; desc[0].next = 0;
  avail->ring[0] = 0; __asm__ volatile("" ::: "memory"); avail->index = 1;
  volatile uint16_t *doorbell = prepare_queue(cfg,&caps,notify_base,1,desc,avail,used);
  if (!doorbell || !setup_rng_msix(b,d,f,&caps,cfg)) return 0;
  cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
  *doorbell = 0;
  for (uint32_t budget = 0; budget < 100000000U; budget++) {
    __asm__ volatile("" ::: "memory");
    if (aiueos_virtio_rng_irq_count && used->index == 1) {
      int ok = used->ring[0].id == 0 && used->ring[0].length == 32;
      if (ok) {
        /* Keep the queue alive so aiueos_random_bytes can request more. The
           first batch already sits in `random`; posted is 1 (one avail entry
           consumed). */
        rng_backend.cfg = cfg; rng_backend.desc = desc; rng_backend.avail = avail;
        rng_backend.used = used; rng_backend.doorbell = doorbell;
        rng_backend.page = random; rng_backend.posted = 1; rng_backend.ready = 1;
      }
      return ok;
    }
    __asm__ volatile("sti; hlt; cli" ::: "memory");
  }
  return 0;
}

static int virtio_blk(uint8_t b, uint8_t d, uint8_t f) {
  struct virtio_caps caps;
  volatile struct virtio_common_cfg *cfg;
  uint64_t notify_base, device_bar;
  if (!find_virtio_caps(b,d,f,&caps) || !caps.have_device || caps.device.length < 8 ||
      !read_bar(b,d,f,caps.device.bar,&device_bar) ||
      device_bar + caps.device.offset < device_bar ||
      !aiueos_map_pci_mmio(device_bar + caps.device.offset,caps.device.length) ||
      !map_transport(b,d,f,&caps,&cfg,&notify_base) || !negotiate(cfg)) return 0;
  volatile uint64_t *capacity_ptr = (volatile void *)(uintptr_t)(device_bar + caps.device.offset);
  uint8_t generation;
  uint64_t capacity;
  do { generation = cfg->config_generation; capacity = *capacity_ptr; }
  while (generation != cfg->config_generation);
  if (capacity == 0 || capacity > (UINT64_MAX / 512ULL)) return 0;

  struct virtq_desc *desc = aiueos_allocate_physical_page();
  struct virtq_avail *avail = aiueos_allocate_physical_page();
  struct virtq_used *used = aiueos_allocate_physical_page();
  uint8_t *request_page = aiueos_allocate_physical_page();
  if (!desc || !avail || !used || !request_page) return 0;
  struct virtio_blk_request *request = (void *)request_page;
  uint8_t *sector = request_page + 512;
  uint8_t *status = request_page + 1024;
  request->type = VIRTIO_BLK_T_IN; request->reserved = 0; request->sector = 0;
  *status = 0xff;
  desc[0] = (struct virtq_desc){(uint64_t)(uintptr_t)request,sizeof(*request),VIRTQ_DESC_F_NEXT,1};
  desc[1] = (struct virtq_desc){(uint64_t)(uintptr_t)sector,512,VIRTQ_DESC_F_NEXT|VIRTQ_DESC_F_WRITE,2};
  desc[2] = (struct virtq_desc){(uint64_t)(uintptr_t)status,1,VIRTQ_DESC_F_WRITE,0};
  /* Publish no request before MSI-X and DRIVER_OK are established.  Publishing
     index 1 here races QEMU's queue activation with the explicit first kick. */
  avail->index = 0;
  /* Split rings use a power-of-two queue; the request consumes three entries. */
  volatile uint16_t *doorbell = prepare_queue(cfg,&caps,notify_base,4,desc,avail,used);
  if (!doorbell) return 0;
  blk_msix_active = 1;
  if (!setup_blk_msix(b,d,f,&caps,cfg)) return 0;
  cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
  uint16_t submitted = 0;
  if (!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,
                            &submitted,VIRTIO_BLK_T_IN,0)) return 0;
  {
      if (used->ring[0].id != 0 || used->ring[0].length != 513 || *status != VIRTIO_BLK_S_OK)
        return 0;
      const struct aiuefs_superblock *superblock = (const void *)sector;
      extern uint64_t kotoba_aiueos_superblock_valid(const void *, uint64_t);
      if (!kotoba_aiueos_superblock_valid(superblock, 512)) return 0;
      uint32_t catalog_sector=superblock->catalog_sector,catalog_length=superblock->catalog_length;
      uint32_t catalog_signature_sector=superblock->catalog_signature_sector;
      uint8_t expected_catalog_sha[32],actual_sha[32],signature[256],catalog_bytes[272];
      for(unsigned i=0;i<32;i++) expected_catalog_sha[i]=superblock->catalog_sha256[i];
      if (catalog_sector<4 || catalog_length<80 || catalog_length>sizeof(catalog_bytes) ||
          catalog_signature_sector<=catalog_sector || catalog_signature_sector>=capacity ||
          superblock->signer_id!=1) return 0;
      for(uint32_t i=0;i<512;i++) sector[i]=0;
      if (!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,
                                &submitted,VIRTIO_BLK_T_IN,catalog_sector)) return 0;
      for(uint32_t i=0;i<catalog_length;i++) catalog_bytes[i]=sector[i];
      for(uint32_t i=0;i<512;i++) sector[i]=0;
      if (!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,
                                &submitted,VIRTIO_BLK_T_IN,catalog_signature_sector)) return 0;
      for(unsigned i=0;i<256;i++) signature[i]=sector[i];
      if (!sha256(catalog_bytes,catalog_length,actual_sha) ||
          !kotoba_aiueos_digest_equal(expected_catalog_sha,actual_sha,32) ||
          !rsa2048_sha256_verify(signature,actual_sha)) return 0;
      uint32_t catalog_routing[2]={catalog_sector,catalog_signature_sector};
      if (!kotoba_aiueos_app_catalog_valid(catalog_bytes,catalog_length,capacity,
                                           catalog_routing,sizeof(catalog_routing))) return 0;
#ifdef AIUEOS_CATALOG_POLICY_SELFTEST
      {
        uint8_t probe[272];
        for(uint32_t i=0;i<catalog_length;i++) probe[i]=catalog_bytes[i];
        probe[0]^=1;
        if(kotoba_aiueos_app_catalog_valid(probe,catalog_length,capacity,catalog_routing,8))return 0;
        for(uint32_t i=0;i<catalog_length;i++) probe[i]=catalog_bytes[i];
        ((struct aiuefs_app_catalog*)(void*)probe)->count=0;
        if(kotoba_aiueos_app_catalog_valid(probe,catalog_length,capacity,catalog_routing,8))return 0;
        for(uint32_t i=0;i<catalog_length;i++) probe[i]=catalog_bytes[i];
        struct aiuefs_app_entry *test_entries=(void*)(probe+sizeof(struct aiuefs_app_catalog));
        for(unsigned i=0;i<16;i++)test_entries[1].id[i]=test_entries[0].id[i];
        if(kotoba_aiueos_app_catalog_valid(probe,catalog_length,capacity,catalog_routing,8))return 0;
        for(uint32_t i=0;i<catalog_length;i++) probe[i]=catalog_bytes[i];
        test_entries=(void*)(probe+sizeof(struct aiuefs_app_catalog));
        test_entries[1].sector=test_entries[0].sector;
        if(kotoba_aiueos_app_catalog_valid(probe,catalog_length,capacity,catalog_routing,8))return 0;
        for(uint32_t i=0;i<catalog_length;i++) probe[i]=catalog_bytes[i];
        test_entries=(void*)(probe+sizeof(struct aiuefs_app_catalog));test_entries[0].signer_id=0;
        if(kotoba_aiueos_app_catalog_valid(probe,catalog_length,capacity,catalog_routing,8))return 0;
        for(uint32_t i=0;i<catalog_length;i++) probe[i]=catalog_bytes[i];
        test_entries=(void*)(probe+sizeof(struct aiuefs_app_catalog));
        test_entries[0].id[1]=0;test_entries[0].id[2]='X';
        if(kotoba_aiueos_app_catalog_valid(probe,catalog_length,capacity,catalog_routing,8))return 0;
        catalog_policy_selftest_ok=1;
      }
#endif
      const struct aiuefs_app_catalog *catalog=(const void *)catalog_bytes;
      const struct aiuefs_app_entry *entries=(const void *)(catalog_bytes+sizeof(*catalog));
      for(unsigned app=0;app<catalog->count;app++) {
        const struct aiuefs_app_entry *entry=&entries[app];
        uint32_t copied=0;for(uint32_t index=0;copied<entry->length;index++) {
          for(uint32_t i=0;i<512;i++)sector[i]=0;
          if(!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,&submitted,VIRTIO_BLK_T_IN,entry->sector+index))return 0;
          uint32_t take=entry->length-copied;if(take>512)take=512;for(uint32_t i=0;i<take;i++)kotoba_app_objects[app][copied+i]=sector[i];copied+=take;
        }
        for(uint32_t i=0;i<512;i++)sector[i]=0;
        if(!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,&submitted,VIRTIO_BLK_T_IN,entry->signature_sector))return 0;
        for(unsigned i=0;i<256;i++)signature[i]=sector[i];
        if(!sha256(kotoba_app_objects[app],entry->length,actual_sha)||
           !kotoba_aiueos_digest_equal(entry->sha256,actual_sha,32)||
           !rsa2048_sha256_verify(signature,actual_sha)) {
          /* Restore from the initramfs recovery payload. The catalog still
             decides what content is acceptable: the carried ELF must hash to
             the catalog entry's digest and its carried signature must verify
             under the same RSA policy before a byte is written back. Catalog
             corruption itself stays fatal. */
          extern const uint8_t *aiueos_initramfs_recovery_elf(uint64_t *);
          extern const uint8_t *aiueos_initramfs_recovery_signature(void);
          uint64_t recovery_length=0;
          const uint8_t *recovery_elf=aiueos_initramfs_recovery_elf(&recovery_length);
          const uint8_t *recovery_signature=aiueos_initramfs_recovery_signature();
          if(!recovery_elf||!recovery_signature||recovery_length!=entry->length)return 0;
          if(!sha256(recovery_elf,recovery_length,actual_sha)||
             !kotoba_aiueos_digest_equal(entry->sha256,actual_sha,32)||
             !rsa2048_sha256_verify(recovery_signature,actual_sha))return 0;
          for(uint32_t written=0,index=0;written<entry->length;index++) {
            uint32_t take=entry->length-written;if(take>512)take=512;
            for(uint32_t i=0;i<512;i++)sector[i]=i<take?recovery_elf[written+i]:0;
            if(!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,&submitted,VIRTIO_BLK_T_OUT,entry->sector+index))return 0;
            for(uint32_t i=0;i<512;i++)sector[i]=0;
            if(!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,&submitted,VIRTIO_BLK_T_IN,entry->sector+index))return 0;
            for(uint32_t i=0;i<take;i++)if(sector[i]!=recovery_elf[written+i])return 0;
            written+=take;
          }
          for(uint32_t i=0;i<512;i++)sector[i]=i<256?recovery_signature[i]:0;
          if(!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,&submitted,VIRTIO_BLK_T_OUT,entry->signature_sector))return 0;
          for(uint32_t i=0;i<512;i++)sector[i]=0;
          if(!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,&submitted,VIRTIO_BLK_T_IN,entry->signature_sector))return 0;
          for(unsigned i=0;i<256;i++)if(sector[i]!=recovery_signature[i])return 0;
          for(uint32_t i=0;i<entry->length;i++)kotoba_app_objects[app][i]=recovery_elf[i];
          for(unsigned i=0;i<256;i++)signature[i]=recovery_signature[i];
          if(!sha256(kotoba_app_objects[app],entry->length,actual_sha)||
             !kotoba_aiueos_digest_equal(entry->sha256,actual_sha,32)||
             !rsa2048_sha256_verify(signature,actual_sha))return 0;
          object_store_restored_count++;
        }
        for(unsigned i=0;i<16;i++)kotoba_apps[app].id[i]=entry->id[i];kotoba_apps[app].length=entry->length;kotoba_apps[app].ready=1;
      }
      kotoba_app_count=catalog->count;
      object_store_ready = 1;
      struct aiuefs_journal_record slots[2];
      struct aiuefs_journal_record *journal = (void *)sector;
      int valid[2] = {0, 0}, selected = -1;
      /* Validate both bounded slots before mutation and choose the greatest
         committed sequence. The other slot remains the rollback record. */
      for (uint32_t slot = 0; slot < 2; slot++) {
        for (uint32_t i = 0; i < 512; i++) sector[i] = 0;
        if (!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,
                                  &submitted,VIRTIO_BLK_T_IN,slot + 1)) return 0;
        journal = (void *)sector;
        valid[slot] = journal_record_valid(journal);
        if (valid[slot]) {
          slots[slot] = *journal;
          if (selected < 0 || slots[slot].sequence > slots[selected].sequence) selected = slot;
        }
      }
      uint64_t plan = kotoba_aiueos_journal_plan(
        (uint64_t)valid[0], valid[0] ? slots[0].sequence : 0,
        (uint64_t)valid[1], valid[1] ? slots[1].sequence : 0);
      uint32_t next_sequence = (uint32_t)plan;
      uint32_t target_slot = (uint32_t)(plan >> 32) & 1U;
      int kotoba_recovered = (int)((plan >> 33) & 1U);
      if (!next_sequence || target_slot > 1 || kotoba_recovered != (selected >= 0)) return 0;
      if (kotoba_recovered) {
        selected = (int)(target_slot ^ 1U);
        journal_recovered = 1;
        journal_recovered_sequence = slots[selected].sequence;
        const struct aiuefs_object_transaction *replay = (const void *)slots[selected].payload;
        if (!apply_object_transaction(request,sector,status,desc,avail,used,doorbell,
                                      &submitted,slots[selected].sequence,replay,1)) return 0;
      }
      if (next_sequence > 999) return 0;
      for (uint32_t i = 0; i < 512; i++) sector[i] = 0;
      journal = (void *)sector;
      extern uint64_t kotoba_aiueos_service_registry_build(
        void *, uint64_t, uint64_t, uint64_t, uint64_t);
      if (!kotoba_aiueos_service_registry_build(journal,512,next_sequence,
            aiueos_service_registry_state(0),aiueos_service_registry_state(1))) return 0;
      struct aiuefs_object_transaction *transaction = (void *)journal->payload;
      if (!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,
                                &submitted,VIRTIO_BLK_T_OUT,target_slot + 1)) return 0;
      for (uint32_t i = 0; i < 512; i++) sector[i] = 0;
      if (!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,
                                &submitted,VIRTIO_BLK_T_IN,target_slot + 1)) return 0;
      journal = (void *)sector;
      if (!journal_record_valid(journal) || journal->sequence != next_sequence) return 0;
      transaction = (void *)journal->payload;
      struct aiuefs_object_transaction committed_transaction = *transaction;
      if (!apply_object_transaction(request,sector,status,desc,avail,used,doorbell,
                                    &submitted,next_sequence,&committed_transaction,0)) return 0;
      journal_ready = 1;
      journal_sequence = next_sequence;
      journal_slot = target_slot + 1;
      if (capacity <= 47) return 0;
      /* User-owned objects use independent dual-slot journals.  Recover each
         domain before admitting user code; malformed or cross-domain records
         are ignored, while the highest valid sequence is replayed. */
      for (unsigned user=0;user<2;user++) {
        struct aiuefs_journal_record user_slots[2];uint64_t user_values[2]={0,0};
        int user_selected=-1;
        uint32_t first=44+user*2;
        for (unsigned slot=0;slot<2;slot++) {
          for(unsigned i=0;i<512;i++)sector[i]=0;
          if(!virtio_blk_sector_io(request,sector,status,desc,avail,used,doorbell,
              &submitted,VIRTIO_BLK_T_IN,first+slot)) return 0;
          const struct aiuefs_journal_record *candidate=(const void *)sector;
          uint64_t decoded_value=user_journal_value(candidate,user);
          if (decoded_value) {
            user_slots[slot]=*candidate;user_values[slot]=decoded_value;
            if(user_selected<0 || user_slots[slot].sequence>user_slots[user_selected].sequence)
              user_selected=(int)slot;
          }
        }
        if(user_selected>=0) {
          const struct aiuefs_object_transaction *replay=(const void *)user_slots[user_selected].payload;
          blk_backend=(struct aiueos_blk_backend){request,sector,status,desc,avail,used,
            doorbell,submitted,capacity,0,1};
          if(!apply_object_transaction(request,sector,status,desc,avail,used,doorbell,
              &blk_backend.submitted,user_slots[user_selected].sequence,replay,1))return 0;
          submitted=blk_backend.submitted;user_object_replay_evidence|=1U<<user;
          user_object_value[user]=user_values[user_selected];user_object_ready|=1U<<user;
          user_object_sequence[user]=user_slots[user_selected].sequence;
          user_object_slot[user]=first+(uint32_t)user_selected;
        }
      }
      blk_backend=(struct aiueos_blk_backend){request,sector,status,desc,avail,used,
        doorbell,submitted,capacity,0,1};
      return 1;
  }
}

static int virtio_input(uint8_t b, uint8_t d, uint8_t f) {
  struct virtio_caps caps;
  volatile struct virtio_common_cfg *cfg;
  uint64_t notify_base;
  if (!find_virtio_caps(b,d,f,&caps) ||
      !map_transport(b,d,f,&caps,&cfg,&notify_base) || !negotiate(cfg)) return 0;
  struct virtq_desc *desc = aiueos_allocate_physical_page();
  struct virtq_avail *avail = aiueos_allocate_physical_page();
  struct virtq_used *used = aiueos_allocate_physical_page();
  struct virtio_input_event *event = aiueos_allocate_physical_page();
  if (!desc || !avail || !used || !event) return 0;
  /* Four slots so EV_KEY + EV_SYN from virtio-keyboard both fit. Queue
     size 1 dropped SYN and could refuse a real key. */
  for (uint16_t i = 0; i < 4; i++) {
    desc[i] = (struct virtq_desc){
      (uint64_t)(uintptr_t)(event + i), sizeof(*event), VIRTQ_DESC_F_WRITE, 0};
    avail->ring[i] = i;
  }
  __asm__ volatile("" ::: "memory"); avail->index = 4;
  volatile uint16_t *doorbell = prepare_queue(cfg,&caps,notify_base,4,desc,avail,used);
  if (!doorbell) return 0;
  cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
  *doorbell = 0;
#ifdef AIUEOS_INPUT_SMOKE_SYNTHETIC
#define AIUEOS_INPUT_POLL_BUDGET 1U
#else
#define AIUEOS_INPUT_POLL_BUDGET 400000000U
#endif
  for (uint32_t budget = 0; budget < AIUEOS_INPUT_POLL_BUDGET; budget++) {
    __asm__ volatile("" ::: "memory");
    uint16_t n = used->index;
    if (n) {
      if (n > 4) n = 4;
      for (uint16_t i = 0; i < n; i++) {
        uint32_t id = used->ring[i].id;
        if (id > 3) continue;
        struct virtio_input_event *ev = event + id;
        if (ev->type != 1 || ev->value > 2) continue; /* EV_KEY; up/down/repeat */
        desktop_input_event = (struct aiueos_desktop_input_event){
          AIUEOS_DESKTOP_INPUT_ABI, sizeof(desktop_input_event), 1,
          AIUEOS_DESKTOP_INPUT_KEY, ev->code, (int32_t)ev->value, 0,
          ev->value ? AIUEOS_DESKTOP_INPUT_PRESSED : 0};
        desktop_input_ready = 1;
        desktop_input_from_eventq = 1;
        return 1;
      }
    }
    if ((budget & 65535U) == 0) *doorbell = 0;
    __asm__ volatile("pause");
  }
#ifdef AIUEOS_INPUT_SMOKE_SYNTHETIC
  /* HMP sendkey targets the emulated console/PS2 path under -display none, not
     virtio-keyboard. Transport setup above is real; this event is test-only.
     guest-input (ADR-0093) builds without this ifdef and requires used-ring. */
  desktop_input_event = (struct aiueos_desktop_input_event){
    AIUEOS_DESKTOP_INPUT_ABI, sizeof(desktop_input_event), 1,
    AIUEOS_DESKTOP_INPUT_KEY, 30, 1, 0, AIUEOS_DESKTOP_INPUT_PRESSED};
  desktop_input_ready = 1;
  desktop_input_from_eventq = 0;
  return 1;
#endif
  desktop_input_eventq_empty = 1;
  return 0;
}

static uint32_t gpu_scanout_width, gpu_scanout_height;
static uint32_t gpu_enabled_scanouts;
static int gpu_2d_create_ok;
static int gpu_2d_flush_ok;
static int gpu_2d_two_ok;
static int gpu_2d_scanout_two_ok;
uint32_t aiueos_gpu_scanout_width(void) { return gpu_scanout_width; }
uint32_t aiueos_gpu_scanout_height(void) { return gpu_scanout_height; }
uint32_t aiueos_gpu_enabled_scanouts(void) { return gpu_enabled_scanouts; }
int aiueos_gpu_2d_create_ok(void) { return gpu_2d_create_ok; }
int aiueos_gpu_2d_flush_ok(void) { return gpu_2d_flush_ok; }
int aiueos_gpu_2d_two_ok(void) { return gpu_2d_two_ok; }
int aiueos_gpu_scanout_two_ok(void) { return gpu_2d_scanout_two_ok; }

static void gpu_zero(void *p, uint32_t n) {
  uint8_t *b = p;
  for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

/* One in-flight controlq command. Queue size is 4; the free-running avail
   index may exceed that because we wait for used->index == target first.
   MMIO kick and descriptor fill are mechanism. The 32×32 bound is one page
   of backing, not a window-manager decision. */
static int gpu_ctrl(struct virtq_desc *desc, struct virtq_avail *avail,
                    struct virtq_used *used, volatile uint16_t *doorbell,
                    uint16_t *submitted,
                    void *req, uint32_t req_len,
                    struct virtio_gpu_ctrl_header *resp, uint32_t resp_len,
                    uint32_t expect_type) {
  uint16_t old = *submitted, target = (uint16_t)(old + 1);
  gpu_zero(resp, resp_len);
  desc[0] = (struct virtq_desc){(uint64_t)(uintptr_t)req, req_len, VIRTQ_DESC_F_NEXT, 1};
  desc[1] = (struct virtq_desc){(uint64_t)(uintptr_t)resp, resp_len, VIRTQ_DESC_F_WRITE, 0};
  avail->ring[old & 3] = 0;
  __asm__ volatile("" ::: "memory");
  avail->index = target;
  *doorbell = 0;
  for (uint32_t budget = 0; budget < 100000000U; budget++) {
    __asm__ volatile("" ::: "memory");
    if (used->index == target) {
      struct virtq_used_element *done = &used->ring[old & 3];
      *submitted = target;
      if (done->id != 0 || done->length < sizeof(*resp) ||
          done->length > resp_len || resp->type != expect_type) return 0;
      return 1;
    }
    __asm__ volatile("pause");
  }
  return 0;
}

/* Guest 2D scanout that is not "PCI device listed" and not GOP-once.
   Display-info already completed on this controlq (submitted == 1).
   Failure here does not un-admit display-info: existing UEFI smokes stay
   green; compositor `gpu` greps the CREATE/FLUSH serial instead. */
static void gpu_2d_resource_path(struct virtq_desc *desc, struct virtq_avail *avail,
                                 struct virtq_used *used, volatile uint16_t *doorbell,
                                 uint8_t *messages) {
  uint8_t *backing = aiueos_allocate_physical_page();
  struct virtio_gpu_ctrl_header *resp = (void *)(messages + 2048);
  uint16_t submitted = 1;
  uint32_t *pixels;
  struct virtio_gpu_rect tile;
  if (!backing) return;
  pixels = (void *)backing;
  for (uint32_t i = 0; i < (VIRTIO_GPU_2D_W * VIRTIO_GPU_2D_H); i++)
    pixels[i] = 0xff2557a7U;
  tile = (struct virtio_gpu_rect){0, 0, VIRTIO_GPU_2D_W, VIRTIO_GPU_2D_H};

  struct virtio_gpu_resource_create_2d *c2d = (void *)messages;
  gpu_zero(c2d, sizeof(*c2d));
  c2d->header.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  c2d->resource_id = VIRTIO_GPU_2D_RESOURCE;
  c2d->format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
  c2d->width = VIRTIO_GPU_2D_W;
  c2d->height = VIRTIO_GPU_2D_H;
  if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                c2d, sizeof(*c2d), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
    return;
  gpu_2d_create_ok = 1;

  struct virtio_gpu_resource_attach_backing *att = (void *)messages;
  gpu_zero(att, sizeof(*att));
  att->header.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  att->resource_id = VIRTIO_GPU_2D_RESOURCE;
  att->nr_entries = 1;
  att->entries[0].addr = (uint64_t)(uintptr_t)backing;
  att->entries[0].length = VIRTIO_GPU_2D_W * VIRTIO_GPU_2D_H * 4U;
  if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                att, sizeof(*att), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
    return;

  struct virtio_gpu_set_scanout *so = (void *)messages;
  gpu_zero(so, sizeof(*so));
  so->header.type = VIRTIO_GPU_CMD_SET_SCANOUT;
  so->r = tile;
  so->scanout_id = 0;
  so->resource_id = VIRTIO_GPU_2D_RESOURCE;
  (void)gpu_ctrl(desc, avail, used, doorbell, &submitted,
                 so, sizeof(*so), resp, 64, VIRTIO_GPU_RESP_OK_NODATA);

  struct virtio_gpu_transfer_to_host_2d *xfer = (void *)messages;
  gpu_zero(xfer, sizeof(*xfer));
  xfer->header.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  xfer->r = tile;
  xfer->offset = 0;
  xfer->resource_id = VIRTIO_GPU_2D_RESOURCE;
  if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                xfer, sizeof(*xfer), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
    return;

  struct virtio_gpu_resource_flush *flush = (void *)messages;
  gpu_zero(flush, sizeof(*flush));
  flush->header.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  flush->r = tile;
  flush->resource_id = VIRTIO_GPU_2D_RESOURCE;
  if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                flush, sizeof(*flush), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
    return;
  gpu_2d_flush_ok = 1;

  /* Second 2D resource when Kotoba admits two surfaces (ADR-0094). Count
     is Kotoba `kotoba_aiueos_wm_hit`; C does not hardcode n=2. Second
     SET_SCANOUT is ADR-0095 and runs only after this path succeeds. */
  if (kotoba_aiueos_wm_hit(2, 2, 100, 80) != 2) return;
  {
    uint8_t *backing2 = aiueos_allocate_physical_page();
    uint32_t *pixels2;
    if (!backing2) return;
    pixels2 = (void *)backing2;
    for (uint32_t i = 0; i < (VIRTIO_GPU_2D_W * VIRTIO_GPU_2D_H); i++)
      pixels2[i] = 0xff22aa55U;

    gpu_zero(c2d, sizeof(*c2d));
    c2d->header.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    c2d->resource_id = VIRTIO_GPU_2D_RESOURCE_2;
    c2d->format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    c2d->width = VIRTIO_GPU_2D_W;
    c2d->height = VIRTIO_GPU_2D_H;
    if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                  c2d, sizeof(*c2d), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
      return;

    gpu_zero(att, sizeof(*att));
    att->header.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    att->resource_id = VIRTIO_GPU_2D_RESOURCE_2;
    att->nr_entries = 1;
    att->entries[0].addr = (uint64_t)(uintptr_t)backing2;
    att->entries[0].length = VIRTIO_GPU_2D_W * VIRTIO_GPU_2D_H * 4U;
    if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                  att, sizeof(*att), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
      return;

    gpu_zero(xfer, sizeof(*xfer));
    xfer->header.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    xfer->r = tile;
    xfer->offset = 0;
    xfer->resource_id = VIRTIO_GPU_2D_RESOURCE_2;
    if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                  xfer, sizeof(*xfer), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
      return;

    gpu_zero(flush, sizeof(*flush));
    flush->header.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush->r = tile;
    flush->resource_id = VIRTIO_GPU_2D_RESOURCE_2;
    if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                  flush, sizeof(*flush), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
      return;
    gpu_2d_two_ok = 1;

    /* Second scanout when Kotoba admits two (ADR-0095). Bind count is
       Kotoba `kotoba_aiueos_scanout_bind`; C does not hardcode n=2.
       After gpu_2d_two_ok so guest-gpu-two stays green if SET_SCANOUT 1
       fails. No qemu_exit. */
    if (kotoba_aiueos_scanout_bind(2, gpu_enabled_scanouts) != 2) return;
    gpu_zero(so, sizeof(*so));
    so->header.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    so->r = tile;
    so->scanout_id = 1;
    so->resource_id = VIRTIO_GPU_2D_RESOURCE_2;
    if (!gpu_ctrl(desc, avail, used, doorbell, &submitted,
                  so, sizeof(*so), resp, 64, VIRTIO_GPU_RESP_OK_NODATA))
      return;
    gpu_2d_scanout_two_ok = 1;
  }
}

/* Modern controlq: GET_DISPLAY_INFO, then a 32×32 2D create/attach/transfer/flush.
   Display-info remains the floor the existing UEFI smoke greps. */
static int virtio_gpu(uint8_t b, uint8_t d, uint8_t f) {
  struct virtio_caps caps;
  volatile struct virtio_common_cfg *cfg;
  uint64_t notify_base;
  if (!find_virtio_caps(b,d,f,&caps) ||
      !map_transport(b,d,f,&caps,&cfg,&notify_base) || !negotiate(cfg)) return 0;
  struct virtq_desc *desc = aiueos_allocate_physical_page();
  struct virtq_avail *avail = aiueos_allocate_physical_page();
  struct virtq_used *used = aiueos_allocate_physical_page();
  uint8_t *messages = aiueos_allocate_physical_page();
  if (!desc || !avail || !used || !messages) return 0;
  struct virtio_gpu_ctrl_header *request = (void *)messages;
  struct virtio_gpu_display_info *response = (void *)(messages + 512);
  request->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
  desc[0] = (struct virtq_desc){(uint64_t)(uintptr_t)request,sizeof(*request),VIRTQ_DESC_F_NEXT,1};
  desc[1] = (struct virtq_desc){(uint64_t)(uintptr_t)response,sizeof(*response),VIRTQ_DESC_F_WRITE,0};
  avail->ring[0] = 0; __asm__ volatile("" ::: "memory"); avail->index = 1;
  volatile uint16_t *doorbell = prepare_queue(cfg,&caps,notify_base,4,desc,avail,used);
  if (!doorbell) return 0;
  cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
  *doorbell = 0;
  for (uint32_t budget = 0; budget < 100000000U; budget++) {
    __asm__ volatile("" ::: "memory");
    if (used->index == 1) {
      if (used->ring[0].id != 0 || used->ring[0].length < sizeof(response->header) ||
          used->ring[0].length > sizeof(*response) ||
          response->header.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return 0;
      gpu_enabled_scanouts = 0;
      for (uint32_t i = 0; i < 16; i++) if (response->modes[i].enabled) {
        uint32_t width = response->modes[i].rect.width, height = response->modes[i].rect.height;
        /* Primary scanout stays at origin. Extra enabled modes may sit
           beside it (QEMU max_outputs=2 places scanout 1 at x=width).
           Rejecting those would drop the whole GPU path and hide
           leftover :one-scanout behind a missing CREATE. */
        if (width < 320 || height < 200 || width > 16384 || height > 16384)
          return 0;
        if (gpu_enabled_scanouts == 0) {
          if (response->modes[i].rect.x || response->modes[i].rect.y)
            return 0;
          gpu_scanout_width = width; gpu_scanout_height = height;
        }
        gpu_enabled_scanouts++;
      }
      if (gpu_enabled_scanouts == 0) return 0;
      gpu_2d_resource_path(desc, avail, used, doorbell, messages);
      return 1;
    }
    __asm__ volatile("pause");
  }
  return 0;
}

/* Frame admission is a decision, so it is a compiler-emitted Kotoba object;
   this file only performs the bounded DMA and hands the bytes over. The
   checksum is the same kind of thing in the other direction: what the field
   ought to contain is decided about the bytes, so the object that verifies a
   received header is also the one that computes the field in a sent one. */
extern uint64_t kotoba_aiueos_net_arp_reply_valid(uint64_t frame, uint64_t length,
                                                  uint64_t expected_ip);
extern uint64_t kotoba_aiueos_ipv4_checksum(uint64_t buffer, uint64_t length);
extern uint64_t kotoba_aiueos_ipv4_icmp_reply_valid(uint64_t frame, uint64_t length,
                                                    uint64_t expected_src, uint64_t ident,
                                                    uint64_t sequence);
extern uint64_t kotoba_aiueos_tcp_checksum_ok(uint64_t frame, uint64_t ip_total_length,
                                              uint64_t src, uint64_t dst);
extern uint64_t kotoba_aiueos_tcp_segment_valid(uint64_t frame, uint64_t length,
                                                uint64_t expected_src, uint64_t expected_ack,
                                                uint64_t expected_flags);
/* DHCPv4 (ADR-0076). Unlike every admission above, the first of these returns
   a REASON CODE and not a boolean: 0 admits, 1..12 name the clause that
   refused. `if (kotoba_aiueos_dhcp_reply_valid(...))` would admit exactly what
   it rejects, which is why the zero test below is written out rather than
   folded into a condition. The second is the extractor -- where a DHCP option
   sits is not a constant offset, so finding one is the same bounded walk the
   admission does and stays on the same side of the boundary. */
extern uint64_t kotoba_aiueos_dhcp_reply_valid(uint64_t frame, uint64_t length,
                                               uint64_t xid, uint64_t mac,
                                               uint64_t expected_type);
extern uint64_t kotoba_aiueos_dhcp_option_u32(uint64_t frame, uint64_t length,
                                              uint64_t code);

static int net_ready;
static uint32_t net_rx_length;
static int ipv4_ready;
static int tcp_ready;
/* How far the connection got, so a failed run says where to look. One bit would
   not: the exchange has four admissions and a TCG boot under load costs many
   minutes, so narrowing it to a phase is worth an accessor. */
static unsigned tcp_stage;
int aiueos_virtio_net_ready(void) { return net_ready; }
uint32_t aiueos_virtio_net_rx_length(void) { return net_rx_length; }
int aiueos_ipv4_ready(void) { return ipv4_ready; }
int aiueos_tcp_ready(void) { return tcp_ready; }
unsigned aiueos_tcp_stage(void) { return tcp_stage; }

/* SSH listener evidence (ssh-v1.edn / ADR-0102). Passive open + the SSH-2
   identification exchange: the first inbound connection this OS has ever
   accepted, as opposed to the client probes above. Only the id exchange is
   here -- kex, host key, userauth need Ed25519 + SHA-512 that the kernel does
   not have yet, and this proves the two blockers under them (no LISTEN, and
   no post-evidence service loop) are gone. Compiled only when AIUEOS_SSH_LISTEN
   is defined; every other build is byte-for-byte unchanged. */
#define NET_SSH_PORT 22
#define NET_SSH_ISN 0x55350000U
static unsigned ssh_listen_stage;   /* 0 idle 1 syn 2 established 3 sent-id 4 got-id */
static int ssh_client_id_valid;
static uint32_t ssh_client_id_len;
/* 0 not-reached 1 kexinit-sent 2 got-client-kexinit 3 got-ecdh-init
   4 reply-sent 5 newkeys-sent. Read by main.c for the evidence marker. */
static unsigned ssh_kex_stage;
unsigned aiueos_ssh_listen_stage(void) { return ssh_listen_stage; }
int aiueos_ssh_client_id_valid(void) { return ssh_client_id_valid; }
uint32_t aiueos_ssh_client_id_len(void) { return ssh_client_id_len; }
unsigned aiueos_ssh_kex_stage(void) { return ssh_kex_stage; }

/* The lease, which is the whole point of the exchange: the first address this
   machine holds because a server said so rather than because a constant in this
   file said so. Deliberately five words and no subsystem -- there is exactly one
   interface, one lease and no renewal, so anything larger would be a shape
   invented ahead of a second caller. */
static int dhcp_ready;
static unsigned dhcp_stage;
static unsigned dhcp_reason;
static uint32_t dhcp_address, dhcp_mask, dhcp_router, dhcp_server;
static uint32_t dhcp_lease_seconds;
static uint32_t dhcp_dns;
static int dhcp_consumed;
int aiueos_dhcp_ready(void) { return dhcp_ready; }
unsigned aiueos_dhcp_stage(void) { return dhcp_stage; }
unsigned aiueos_dhcp_reason(void) { return dhcp_reason; }
uint32_t aiueos_dhcp_address(void) { return dhcp_address; }
uint32_t aiueos_dhcp_mask(void) { return dhcp_mask; }
uint32_t aiueos_dhcp_router(void) { return dhcp_router; }
uint32_t aiueos_dhcp_server(void) { return dhcp_server; }
uint32_t aiueos_dhcp_lease_seconds(void) { return dhcp_lease_seconds; }
uint32_t aiueos_dhcp_dns(void) { return dhcp_dns; }
int aiueos_dhcp_consumed(void) { return dhcp_consumed; }

/* P2 cloud path (ADR-0081). Always attempted after a lease, so the offline
   floor sees the same marker NAMES whether the public network answered. The
   RESULT lives in the rest of the serial line. These are probes, not stacks. */
static int dns_ready;
static unsigned dns_stage;
static uint32_t dns_a;
static int tcp_cloud_ready;
static unsigned tcp_cloud_stage;
static int tls_record_ready;
static uint8_t tls_record_type;
static int tls_handshake_ready;
static int http_cid_ready;
static const char *http_cid_text;
int aiueos_dns_ready(void) { return dns_ready; }
unsigned aiueos_dns_stage(void) { return dns_stage; }
uint32_t aiueos_dns_a(void) { return dns_a; }
int aiueos_tcp_cloud_ready(void) { return tcp_cloud_ready; }
unsigned aiueos_tcp_cloud_stage(void) { return tcp_cloud_stage; }
int aiueos_tls_record_ready(void) { return tls_record_ready; }
uint8_t aiueos_tls_record_type(void) { return tls_record_type; }
int aiueos_tls_handshake_ready(void) { return tls_handshake_ready; }
int aiueos_http_cid_ready(void) { return http_cid_ready; }
const char *aiueos_http_cid(void) { return http_cid_text ? http_cid_text : ""; }
uint32_t aiueos_tls_stage(void) { return aiueos_tls13_stage(); }
uint32_t aiueos_tls_rx_buffered(void) { return aiueos_tls13_rx_buffered(); }
uint32_t aiueos_tls_app_len(void) { return aiueos_tls13_app_len(); }
uint8_t aiueos_tls_last_record_type(void) { return aiueos_tls13_last_record_type(); }
uint8_t aiueos_tls_last_inner_type(void) { return aiueos_tls13_last_inner_type(); }
int aiueos_tls_failed(void) { return aiueos_tls13_failed(); }
uint32_t aiueos_tls_nst_count(void) { return aiueos_tls13_nst_count(); }
static int tls_finished_sent;
static int tls_http_sent;
int aiueos_tls_finished_sent(void) { return tls_finished_sent; }
int aiueos_tls_http_sent(void) { return tls_http_sent; }

/* SLIRP's fixed topology: the guest is 10.0.2.15 and the gateway that answers
   ARP is 10.0.2.2. Nothing here depends on DHCP having run -- an ARP exchange
   is the smallest thing that proves a real peer replied, and it needs no IP
   stack at all, which is exactly why it is the first packet aiueos sends. */
#define NET_GUEST_IP 0x0a00020fU
#define NET_PEER_IP 0x0a000202U
#define NET_FRAME_MAX 2048
/* Echoed back unchanged by the peer, so they are what ties a reply to the
   request THIS boot sent. One outstanding echo means a constant sequence is
   enough; a second one would need a counter. */
#define NET_ICMP_IDENT 0xa1e0
#define NET_ICMP_SEQUENCE 1

static void net_store_be16(uint8_t *at, uint16_t value) {
  at[0] = (uint8_t)(value >> 8); at[1] = (uint8_t)value;
}
static void net_store_be32(uint8_t *at, uint32_t value) {
  at[0] = (uint8_t)(value >> 24); at[1] = (uint8_t)(value >> 16);
  at[2] = (uint8_t)(value >> 8); at[3] = (uint8_t)value;
}
/* The inverse pair, needed only once TCP has to carry a number the PEER chose --
   every field this driver sent before was one it had picked itself. Reading a
   field is not admitting it: these are used only on frames a Kotoba object has
   already accepted, the same rule under which the peer's MAC is lifted out of an
   admitted ARP reply (ADR-0021). */
static uint16_t net_load_be16(const uint8_t *at) {
  return (uint16_t)(((uint32_t)at[0] << 8) | at[1]);
}
static uint32_t net_load_be32(const uint8_t *at) {
  return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) |
         ((uint32_t)at[2] << 8) | at[3];
}

/* A locally-administered address. The peer replies to whatever source it sees,
   so this needs no VIRTIO_NET_F_MAC negotiation and reads nothing from the
   device's own config space. */
static const uint8_t net_mac[6] = {0x52,0x54,0x00,0xa1,0xe0,0x51};

static uint32_t net_build_arp_request(uint8_t *frame) {
  for (unsigned i = 0; i < 6; i++) frame[i] = 0xff;          /* broadcast */
  for (unsigned i = 0; i < 6; i++) frame[6 + i] = net_mac[i];
  net_store_be16(frame + 12, 0x0806);                        /* EtherType ARP */
  net_store_be16(frame + 14, 1);                             /* Ethernet */
  net_store_be16(frame + 16, 0x0800);                        /* IPv4 */
  frame[18] = 6; frame[19] = 4;
  net_store_be16(frame + 20, 1);                             /* request */
  for (unsigned i = 0; i < 6; i++) frame[22 + i] = net_mac[i];
  net_store_be32(frame + 28, NET_GUEST_IP);
  for (unsigned i = 0; i < 6; i++) frame[32 + i] = 0;        /* unknown target */
  net_store_be32(frame + 38, NET_PEER_IP);
  return 42;
}

/* The ARP cache, which at this point in the stack's life holds exactly one
   entry: the gateway. Storing bytes is mechanism, so it belongs here -- which
   address a frame carries is not a decision about whether anything is valid.
   It is what turns the exchange above from a self-contained proof into a
   precondition: without it no unicast frame can be addressed at all. */
static uint8_t net_peer_mac[6];
static int net_peer_mac_known;

static uint32_t net_build_icmp_echo(uint8_t *frame) {
  for (unsigned i = 0; i < 6; i++) frame[i] = net_peer_mac[i];
  for (unsigned i = 0; i < 6; i++) frame[6 + i] = net_mac[i];
  net_store_be16(frame + 12, 0x0800);                        /* EtherType IPv4 */
  frame[14] = 0x45;                                          /* version 4, IHL 5 */
  frame[15] = 0;                                             /* no DSCP, no ECN */
  net_store_be16(frame + 16, 28);                            /* 20 IPv4 + 8 ICMP */
  /* Identification 0 with DF set: RFC 6864 permits it precisely because a
     datagram that may not be fragmented can never need to be reassembled. */
  net_store_be16(frame + 18, 0);
  net_store_be16(frame + 20, 0x4000);
  frame[22] = 64;                                            /* TTL */
  frame[23] = 1;                                             /* ICMP */
  net_store_be16(frame + 24, 0);                             /* checksum covers itself as 0 */
  net_store_be32(frame + 26, NET_GUEST_IP);
  net_store_be32(frame + 30, NET_PEER_IP);
  net_store_be16(frame + 24,
    (uint16_t)kotoba_aiueos_ipv4_checksum((uint64_t)(uintptr_t)(frame + 14), 20));
  frame[34] = 8;                                             /* echo request */
  frame[35] = 0;
  net_store_be16(frame + 36, 0);
  net_store_be16(frame + 38, NET_ICMP_IDENT);
  net_store_be16(frame + 40, NET_ICMP_SEQUENCE);
  net_store_be16(frame + 36,
    (uint16_t)kotoba_aiueos_ipv4_checksum((uint64_t)(uintptr_t)(frame + 34), 8));
  /* 42 bytes, the same length as the ARP request the device already accepted,
     so nothing here depends on a minimum-frame rule that has not been tested. */
  return 42;
}

/* Everything needed to drive one virtqueue, kept together because the IPv4
   exchange posts and reaps on both queues several times and the parameter lists
   were unreadable apart. `posted` is the free-running avail index; the ring slot
   is always 0 because the queue size is 1. */
struct net_ring {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  volatile uint16_t *doorbell;
  uint16_t queue;
  uint16_t posted;
};

static void net_post(struct net_ring *ring) {
  ring->avail->ring[0] = 0;
  __asm__ volatile("" ::: "memory");
  ring->posted++;
  ring->avail->index = ring->posted;
  *ring->doorbell = ring->queue;
}

/* A bounded SPIN, and specifically not `hlt`. This driver claims no MSI-X
   vector -- unlike rng and blk, which install one before going live -- so there
   is no interrupt to wake a sleeper, and the device's own unrouted legacy
   interrupt stays harmlessly pending only for as long as interrupts remain
   masked, which they are throughout enumeration. Measured: a variant that
   waited with `sti; hlt; cli` (copied from the rng driver, which can afford it
   because it HAS a vector) wedges the boot immediately after
   AIUEOS_APIC_TIMER_OK.
   The budget bounds the failure case: a completion that never arrives fails the
   gate instead of parking the boot forever. It is never approached in the
   success case, where a completion lands within microseconds of the doorbell,
   which is also why the exchanges below can afford to wait several times. */
static int net_await(struct virtq_used *used, uint16_t target) {
  for (uint32_t budget = 0; budget < 200000000U; budget++) {
    __asm__ volatile("" ::: "memory");
    if (used->index >= target) return 1;
  }
  return 0;
}

/* One echo request out, one admitted echo reply back. Layered strictly on top
   of the link layer: it is called only after the ARP exchange was admitted, and
   its result cannot retract that evidence. */
static int net_ipv4_echo(struct net_ring *rx, struct net_ring *tx,
                         uint8_t *rx_page, uint8_t *tx_page) {
  if (!net_peer_mac_known) return 0;
  /* The receive buffer goes back to the device before the request goes out, for
     the same reason the first one was posted before DRIVER_OK: a reply must
     never arrive with nowhere to land. */
  net_post(rx);
  for (unsigned i = 0; i < sizeof(struct virtio_net_hdr); i++) tx_page[i] = 0;
  uint32_t frame_length = net_build_icmp_echo(tx_page + sizeof(struct virtio_net_hdr));
  tx->desc[0].length = (uint32_t)sizeof(struct virtio_net_hdr) + frame_length;
  net_post(tx);
  if (!net_await(tx->used, tx->posted) || tx->used->ring[0].id != 0) return 0;

  /* SLIRP answers the echo itself, but nothing promises that the next frame to
     land is that answer -- the gateway's own ARP request arrives on this same
     queue and is not an error. So a bounded number of frames are looked at, and
     the object decides which of them counts. The budget above is only spent in
     full on a queue that has gone quiet, which ends the loop immediately. */
  for (unsigned attempt = 0; attempt < 4; attempt++) {
    if (attempt) net_post(rx);
    if (!net_await(rx->used, rx->posted)) return 0;
    uint32_t received = rx->used->ring[0].length;
    if (rx->used->ring[0].id == 0 &&
        received > sizeof(struct virtio_net_hdr) &&
        received <= sizeof(struct virtio_net_hdr) + NET_FRAME_MAX &&
        kotoba_aiueos_ipv4_icmp_reply_valid(
          (uint64_t)(uintptr_t)(rx_page + sizeof(struct virtio_net_hdr)),
          received - (uint32_t)sizeof(struct virtio_net_hdr),
          NET_PEER_IP, NET_ICMP_IDENT, NET_ICMP_SEQUENCE)) return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------------- */
/* TCP: one connection, opened, used once and closed.                        */
/*                                                                           */
/* This is a PROBE, not a stack, and the difference is worth being explicit   */
/* about because everything below reads like the beginning of one. There is   */
/* no retransmission timer -- a segment this OS sends and the peer drops is   */
/* never sent again, and the run simply fails. No congestion control: there   */
/* is one segment in flight at a time because the code waits for each reply,  */
/* not because anything computes a window. No window management beyond a      */
/* fixed advertised value. No out-of-order reassembly, no reassembly at all;  */
/* a segment that is not the next one expected is discarded by the admission  */
/* and looked past. One connection, whose endpoints are compiled in. No TCP   */
/* options are sent (data offset 5), though the peer's are tolerated.         */
/*                                                                           */
/* What it proves is exactly one thing: a real peer completed a three-way     */
/* handshake with aiueos, echoed bytes it sent, and closed, with every        */
/* received segment admitted by compiler-emitted Kotoba. Anything that needs  */
/* to survive loss, reorder or a second connection needs the state machine    */
/* this deliberately is not.                                                  */
/* ------------------------------------------------------------------------- */

/* SLIRP's `guestfwd=tcp:10.0.2.100:9000-cmd:/bin/cat` endpoint, which pipes the
   stream through `cat` and so echoes it. No ARP is sent for this address: the
   ARP cache holds one entry, the gateway, and frames addressed to the gateway's
   MAC reach SLIRP's IPv4 input regardless -- it does not filter inbound IPv4 on
   the Ethernet destination. Note that 10.0.2.100 is INSIDE 10.0.2.0/24 and so
   is on-link by netmask; a conforming stack would ARP for it, and SLIRP would
   answer, because guestfwd addresses are in its exec list. Not doing so is a
   property of this probe, not of the topology. */
#define NET_TCP_PEER_IP 0x0a000264U
#define NET_TCP_PEER_PORT 9000
/* Both fixed, because there is exactly one connection per boot. A real stack
   would allocate an ephemeral port and pick an unpredictable ISN (RFC 6528);
   neither adds anything against a `cat` on the other side of a virtual switch,
   and constants make a capture of a failed run readable at a glance. */
#define NET_TCP_LOCAL_PORT 49152
#define NET_TCP_ISN 0xa1e00000U
/* Advertised once and never updated. Nothing here holds more than one segment,
   so this is a constant rather than a variable that would have to shrink. */
#define NET_TCP_WINDOW 2048
/* Cloud :443 window must fit one TLS application record of the empty-CID
   HTTP 200 (measured host probe: 1276-byte HTTP, ~1298-byte TLS record).
   1280 was one byte-short of that record and split it across the one-slot
   RX queue. Ethernet+IPv4+TCP headers are 54 bytes; 1792+54 < NET_FRAME_MAX. */
#define NET_CLOUD_WINDOW 1792
#define NET_TCP_PAYLOAD 8
static uint16_t net_tx_window = NET_TCP_WINDOW;
static uint16_t net_ip_id = 1;

#define NET_TCP_FIN 0x01
#define NET_TCP_SYN 0x02
#define NET_TCP_PSH 0x08
#define NET_TCP_ACK 0x10

/* Which admission was not reached. `TX_*` are build faults rather than network
   ones and are reported apart for that reason: they mean the segment was wrong
   before it left, which no amount of retrying the peer would fix. */
#define NET_TCP_STAGE_IDLE 0
#define NET_TCP_STAGE_SYN_ACK 1
#define NET_TCP_STAGE_ECHO 2
#define NET_TCP_STAGE_FIN_ACK 3
#define NET_TCP_STAGE_DONE 4
#define NET_TCP_STAGE_TX_CHECKSUM 5
#define NET_TCP_STAGE_TX_STALLED 6

/* Eight bytes: enough that the peer has to carry a real payload, few enough
   that the sequence arithmetic below is checkable by eye against a capture. */
static const uint8_t net_tcp_payload[NET_TCP_PAYLOAD] =
  {'a','i','u','e','o','s','\r','\n'};

/* The TCP checksum covers a 12-byte pseudo-header that cannot be overlaid on
   the frame: to be contiguous with the TCP header it would have to start at
   frame offset 22, which puts its address fields at 22..29, while the IPv4
   header carries them at 26..33 -- four bytes off. So the two are laid out
   contiguously here instead, and `aiueos-ipv4-checksum` sums them as one range.
   Laying bytes out is mechanism; the arithmetic stays in the object that
   already computes exactly this sum for the ICMP path.
   Static rather than an allocated page because a failed allocation would have
   to fail the whole NIC probe that ARP and IPv4 have already passed, and this
   buffer is never touched by DMA -- only the CPU reads it. */
static uint8_t net_tcp_scratch[4096] __attribute__((aligned(4096)));

static uint16_t net_tcp_checksum(const uint8_t *frame, uint32_t tcp_length,
                                 uint32_t src, uint32_t dst) {
  net_store_be32(net_tcp_scratch, src);
  net_store_be32(net_tcp_scratch + 4, dst);
  net_tcp_scratch[8] = 0;
  net_tcp_scratch[9] = 6;                                    /* protocol TCP */
  net_store_be16(net_tcp_scratch + 10, (uint16_t)tcp_length);
  for (uint32_t i = 0; i < tcp_length; i++) net_tcp_scratch[12 + i] = frame[34 + i];
  return (uint16_t)kotoba_aiueos_ipv4_checksum(
    (uint64_t)(uintptr_t)net_tcp_scratch, 12 + tcp_length);
}

static uint32_t net_build_tcp(uint8_t *frame, uint32_t src, uint32_t dst,
                              uint16_t sport, uint16_t dport,
                              uint32_t sequence, uint32_t acknowledgement,
                              uint8_t flags, const uint8_t *payload,
                              uint32_t payload_length) {
  uint32_t total = 40 + payload_length;                      /* 20 IPv4 + 20 TCP */
  for (unsigned i = 0; i < 6; i++) frame[i] = net_peer_mac[i];
  for (unsigned i = 0; i < 6; i++) frame[6 + i] = net_mac[i];
  net_store_be16(frame + 12, 0x0800);                        /* EtherType IPv4 */
  frame[14] = 0x45;                                          /* version 4, IHL 5 */
  frame[15] = 0;
  net_store_be16(frame + 16, (uint16_t)total);
  /* Identification 0 with DF set, as for the echo: RFC 6864 permits it because
     a datagram that may not be fragmented can never need reassembly. Distinct
     IDs still help SLIRP tell later TCP segments apart from the ClientHello. */
  net_store_be16(frame + 18, net_ip_id);
  net_ip_id++;
  net_store_be16(frame + 20, 0x4000);
  frame[22] = 64;                                            /* TTL */
  frame[23] = 6;                                             /* TCP */
  net_store_be16(frame + 24, 0);                             /* checksum covers itself as 0 */
  net_store_be32(frame + 26, src);
  net_store_be32(frame + 30, dst);
  net_store_be16(frame + 24,
    (uint16_t)kotoba_aiueos_ipv4_checksum((uint64_t)(uintptr_t)(frame + 14), 20));
  net_store_be16(frame + 34, sport);
  net_store_be16(frame + 36, dport);
  net_store_be32(frame + 38, sequence);
  net_store_be32(frame + 42, acknowledgement);
  frame[46] = 0x50;                                          /* data offset 5, no options */
  frame[47] = flags;
  net_store_be16(frame + 48, net_tx_window);
  net_store_be16(frame + 50, 0);                             /* checksum covers itself as 0 */
  net_store_be16(frame + 52, 0);                             /* no urgent data */
  for (uint32_t i = 0; i < payload_length; i++) frame[54 + i] = payload[i];
  net_store_be16(frame + 50, net_tcp_checksum(frame, 20 + payload_length, src, dst));
  /* 54 bytes without a payload, 62 with the echo, 196 with a ClientHello. The
     first is under Ethernet's 60-byte minimum and is emitted unpadded, which
     the ARP request at 42 bytes already showed this device and SLIRP both
     accept -- so nothing here depends on padding that has never been
     exercised. A real NIC on a real wire pads it itself, and the receiver
     ignores the difference because the IPv4 total length, not the frame
     length, says where the datagram ends. */
  return 14 + total;
}

static int net_tcp_send(struct net_ring *tx, uint8_t *tx_page, uint32_t src,
                        uint32_t dst, uint16_t sport, uint16_t dport,
                        uint32_t sequence, uint32_t acknowledgement, uint8_t flags,
                        const uint8_t *payload, uint32_t payload_length) {
  uint8_t *frame = tx_page + sizeof(struct virtio_net_hdr);
  for (unsigned i = 0; i < sizeof(struct virtio_net_hdr); i++) tx_page[i] = 0;
  uint32_t frame_length =
    net_build_tcp(frame, src, dst, sport, dport, sequence, acknowledgement,
                  flags, payload, payload_length);
  /* The segment is put to the same admission arithmetic that will judge the
     peer's, by a different path -- from the frame, not from the scratch copy --
     before the device ever sees it. It catches a scratch copy of the wrong
     length or offset, a checksum field that was not zero while it was computed,
     a field stored in the wrong place. It cannot catch an address that is wrong
     the same way in both derivations; only the peer can, by dropping the
     segment, and a dropped segment is indistinguishable from a peer that never
     answered until a whole second boot has been spent finding out. */
  if (!kotoba_aiueos_tcp_checksum_ok((uint64_t)(uintptr_t)frame,
                                     40 + payload_length, src, dst)) {
    tcp_stage = NET_TCP_STAGE_TX_CHECKSUM;
    tcp_cloud_stage = NET_TCP_STAGE_TX_CHECKSUM;
    return 0;
  }
  tx->desc[0].length = (uint32_t)sizeof(struct virtio_net_hdr) + frame_length;
  net_post(tx);
  if (!net_await(tx->used, tx->posted) || tx->used->ring[0].id != 0) {
    tcp_stage = NET_TCP_STAGE_TX_STALLED;
    tcp_cloud_stage = NET_TCP_STAGE_TX_STALLED;
    return 0;
  }
  return 1;
}

/* The caller has already posted one receive buffer -- a segment must never
   arrive with nowhere to land -- so the first attempt consumes that one and only
   the retries post again. The queue holds exactly ONE buffer, which is the
   sharpest limitation in this file: two segments arriving back to back means the
   second is dropped, and recovery then depends entirely on the peer
   retransmitting it, since nothing here does. Four attempts sufficed for ICMP,
   where only the gateway's own ARP traffic competed; eight are allowed here
   because a close can be preceded by a bare ACK and a retransmission both. */
static int net_tcp_receive(struct net_ring *rx, uint8_t *rx_page,
                           uint32_t expected_src, uint32_t expected_ack,
                           uint8_t expected_flags, unsigned attempts) {
  for (unsigned attempt = 0; attempt < attempts; attempt++) {
    if (attempt) net_post(rx);
    if (!net_await(rx->used, rx->posted)) return 0;
    uint32_t received = rx->used->ring[0].length;
    if (rx->used->ring[0].id != 0 ||
        received <= sizeof(struct virtio_net_hdr) ||
        received > sizeof(struct virtio_net_hdr) + NET_FRAME_MAX) continue;
    if (kotoba_aiueos_tcp_segment_valid(
          (uint64_t)(uintptr_t)(rx_page + sizeof(struct virtio_net_hdr)),
          received - (uint32_t)sizeof(struct virtio_net_hdr),
          expected_src, expected_ack, expected_flags)) return 1;
  }
  return 0;
}

/* How far the peer's stream has been seen, derived from a segment that has
   already been admitted. Deriving is not admitting -- the same rule under which
   the peer's MAC is lifted out of an admitted ARP reply -- but the derivation
   has a domain, and a data offset outside it would produce an acknowledgement
   number from an underflow rather than from the wire. The admission object has
   no parameter left to constrain the data offset with, so the constraint lives
   at the one place that actually depends on it. */
static int net_tcp_peer_next(const uint8_t *frame, uint32_t *next) {
  uint32_t total = net_load_be16(frame + 16);
  uint32_t header = 4U * (uint32_t)(frame[46] >> 4);
  if (header < 20 || header + 20 > total) return 0;
  *next = net_load_be32(frame + 38) + (total - 20 - header);
  return 1;
}

static int net_tcp_probe(struct net_ring *rx, struct net_ring *tx,
                         uint8_t *rx_page, uint8_t *tx_page) {
  const uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  uint32_t peer_next;
  if (!net_peer_mac_known) return 0;

  /* --- three-way handshake ------------------------------------------------ */
  net_post(rx);
  if (!net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_TCP_PEER_IP,
                    NET_TCP_LOCAL_PORT, NET_TCP_PEER_PORT,
                    NET_TCP_ISN, 0, NET_TCP_SYN, 0, 0)) return 0;
  tcp_stage = NET_TCP_STAGE_SYN_ACK;
  /* SYN|ACK exactly: a bare SYN would be a simultaneous open, which this cannot
     complete, and a SYN|ACK|ECE would mean the peer negotiated ECN, which this
     never requested. Either is a real segment and neither is the answer asked
     for. The acknowledgement pins it to the SYN this boot sent. */
  if (!net_tcp_receive(rx, rx_page, NET_TCP_PEER_IP, NET_TCP_ISN + 1,
                       NET_TCP_SYN | NET_TCP_ACK, 8))
    return 0;
  /* A SYN occupies one sequence number even though it carries no data, so the
     peer's stream starts one past the ISN it just announced. */
  peer_next = net_load_be32(frame + 38) + 1;
  if (!net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_TCP_PEER_IP,
                    NET_TCP_LOCAL_PORT, NET_TCP_PEER_PORT,
                    NET_TCP_ISN + 1, peer_next, NET_TCP_ACK, 0, 0))
    return 0;

  /* --- one payload out, the peer's echo of it back ------------------------ */
  net_post(rx);
  if (!net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_TCP_PEER_IP,
                    NET_TCP_LOCAL_PORT, NET_TCP_PEER_PORT,
                    NET_TCP_ISN + 1, peer_next,
                    NET_TCP_PSH | NET_TCP_ACK, net_tcp_payload, NET_TCP_PAYLOAD))
    return 0;
  tcp_stage = NET_TCP_STAGE_ECHO;
  /* PSH|ACK, because a BSD-derived stack -- which SLIRP is -- sets PSH on a
     segment that drains its send buffer, and eight echoed bytes always do. The
     bare ACK that may precede it carries the same acknowledgement number, so
     the flags are the only field that tells the two apart, which is why this
     object compares flags for equality rather than masking.
     Note what is NOT proved: that the bytes came back unchanged. The checksum
     covers the payload, so the segment is intact, but nothing compares it with
     what was sent -- that would need a third object, and the admission's arity
     is spent. */
  if (!net_tcp_receive(rx, rx_page, NET_TCP_PEER_IP,
                       NET_TCP_ISN + 1 + NET_TCP_PAYLOAD,
                       NET_TCP_PSH | NET_TCP_ACK, 8)) return 0;
  if (!net_tcp_peer_next(frame, &peer_next)) return 0;

  /* --- close -------------------------------------------------------------- */
  net_post(rx);
  if (!net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_TCP_PEER_IP,
                    NET_TCP_LOCAL_PORT, NET_TCP_PEER_PORT,
                    NET_TCP_ISN + 1 + NET_TCP_PAYLOAD, peer_next,
                    NET_TCP_FIN | NET_TCP_ACK, 0, 0)) return 0;
  tcp_stage = NET_TCP_STAGE_FIN_ACK;
  /* A FIN occupies a sequence number too, so the peer acknowledges one past the
     last payload byte. `cat` reaches EOF when its half closes and exits, and
     SLIRP closes behind it -- the peer's FIN is a consequence of ours, which is
     why waiting for it is a close and not just a shutdown. */
  if (!net_tcp_receive(rx, rx_page, NET_TCP_PEER_IP,
                       NET_TCP_ISN + 2 + NET_TCP_PAYLOAD,
                       NET_TCP_FIN | NET_TCP_ACK, 8)) return 0;
  /* Courtesy, not evidence: the exchange is already proved by the admitted FIN.
     It is sent so the peer's half closes instead of retransmitting into a boot
     that has moved on, and its result is deliberately not checked -- failing
     here would retract evidence that has already been earned. */
  net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_TCP_PEER_IP,
               NET_TCP_LOCAL_PORT, NET_TCP_PEER_PORT,
               NET_TCP_ISN + 2 + NET_TCP_PAYLOAD, peer_next + 1,
               NET_TCP_ACK, 0, 0);
  tcp_stage = NET_TCP_STAGE_DONE;
  return 1;
}

#ifdef AIUEOS_SSH_LISTEN
/* The SSH-2.0 identification string this server announces. RFC 4253 §4.2:
   "SSH-protoversion-softwareversion" then CR LF, and nothing before it because
   this build sends no pre-banner lines. */
static const uint8_t net_ssh_id[] = "SSH-2.0-aiueos_0.1\r\n";
#define NET_SSH_ID_LEN (sizeof(net_ssh_id) - 1)

/* A client's identification string is valid iff it begins "SSH-2.0-" or the
   compatibility "SSH-1.99-" (RFC 4253 §5.1). Checked from the admitted frame,
   never from a length the client chose. */
static int net_ssh_id_prefix_ok(const uint8_t *p, uint32_t len) {
  const uint8_t two[] = "SSH-2.0-";
  const uint8_t compat[] = "SSH-1.99-";
  if (len >= 8) {
    int m = 1;
    for (unsigned i = 0; i < 8; i++) if (p[i] != two[i]) m = 0;
    if (m) return 1;
  }
  if (len >= 9) {
    int m = 1;
    for (unsigned i = 0; i < 9; i++) if (p[i] != compat[i]) m = 0;
    if (m) return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------------- */
/* The real curve25519-sha256 key exchange (ADR-0107). After the identification
   strings, the server sends KEXINIT, receives the client's KEXINIT and its
   KEX_ECDH_INIT (Q_C), then answers with KEX_ECDH_REPLY (K_S, Q_S, signature)
   and NEWKEYS. The wire byte layout mirrors ssh.transport / ssh.kex in
   kotoba-lang/org-ietf-ssh (west-imported), which an independent real-crypto
   client verified end-to-end; here the crypto comes from the kernel's Kotoba
   objects: X25519 for Q_S/K, SHA-256 for H, and the ECDSA-P256 sign object for
   the host-key signature over H. Assembly is decision-free byte layout. */
extern uint64_t kotoba_aiueos_x25519(const uint8_t *, const uint8_t *,
                                     uint8_t *, uint8_t *);
extern uint64_t kotoba_aiueos_ecdsa_p256_sign(const uint8_t *, const uint8_t *,
                                              const uint8_t *, uint8_t *,
                                              uint8_t *);
/* Fixed ecdsa-sha2-nistp256 host key: d is the private scalar; (x,y)=d*G is the
   public point (precomputed offline -- the kernel has no P-256 base multiply of
   its own). The client harness pins this public key. A per-device key is the
   provisioning's job (ssh-v1.edn); a fixed key here makes the handshake
   reproducible for the gate. */
static const uint8_t ssh_host_d[32] = {
  0xe2,0x7f,0xa8,0xdf,0xb9,0xb3,0xf8,0x27,0xcc,0x11,0xe4,0x4e,0x17,0x5d,0x7f,0xf7,
  0x85,0x44,0x51,0xbc,0x91,0x9b,0x53,0x44,0xa0,0x3a,0x0f,0x2b,0x59,0x32,0x07,0x89};
static const uint8_t ssh_host_x[32] = {
  0x32,0xb0,0x02,0xb8,0xfe,0x62,0x54,0xc0,0x89,0x4d,0x2c,0x08,0x24,0x29,0x99,0x2b,
  0xd2,0x8c,0x0d,0x53,0xb4,0x51,0x86,0xab,0x79,0x06,0xdf,0x41,0x18,0x51,0x5e,0x35};
static const uint8_t ssh_host_y[32] = {
  0xd2,0x1a,0x8d,0x39,0x06,0x32,0x9b,0x62,0x4a,0x31,0xcc,0xe2,0xef,0x73,0x52,0x93,
  0x5a,0x3e,0xd8,0x8f,0x5f,0x09,0x3e,0x57,0x09,0x57,0x20,0x57,0x64,0x6e,0xb2,0x9f};
static const uint8_t x25519_base9[32] = { 9 };  /* rest zero: the curve25519 base */
/* The single authorized publickey (ADR-0108): the ecdsa-sha2-nistp256 public
   point a client must prove it holds the private half of. The provisioning
   places per-device authorized keys (ssh-v1.edn); this fixed one makes the login
   reproducible for the gate. */
static const uint8_t ssh_auth_x[32] = {
  0x46,0x87,0x0e,0x7c,0xe7,0x9b,0xcb,0xc6,0x01,0x47,0x14,0x61,0x8f,0x35,0x43,0xdc,
  0x1e,0x6d,0x67,0xcb,0xc5,0xda,0x37,0x8d,0x91,0xc8,0xd7,0x17,0x11,0xaf,0x1a,0xbf};
static const uint8_t ssh_auth_y[32] = {
  0x04,0xae,0xd0,0x99,0x59,0x46,0xfb,0x24,0x4e,0x15,0x10,0x9e,0xe2,0x76,0xc5,0x79,
  0xfa,0x0b,0x14,0x40,0x5b,0xf9,0x50,0x75,0x22,0xee,0xe2,0x65,0x68,0xd2,0xc0,0xf3};
/* AES-128-GCM (tls_aes_gcm.h) and the ECDSA-P256 verify object (already linked
   for userauth's signature check). */
extern int aiueos_aes128_gcm_encrypt(const uint8_t[16], const uint8_t[12],
                                     const uint8_t *, uint32_t,
                                     const uint8_t *, uint32_t, uint8_t *, uint8_t[16]);
extern int aiueos_aes128_gcm_decrypt(const uint8_t[16], const uint8_t[12],
                                     const uint8_t *, uint32_t,
                                     const uint8_t *, uint32_t, const uint8_t[16], uint8_t *);
extern uint64_t kotoba_aiueos_ecdsa_p256_sha256_verify(const uint8_t *, const uint8_t *,
                                                       const uint8_t *, uint8_t *, uint64_t);
static const uint8_t ssh_v_s[] = "SSH-2.0-aiueos_0.1";  /* V_S without CR-LF */
#define SSH_V_S_LEN (sizeof(ssh_v_s) - 1)
static uint8_t ssh_v_c[128];       /* V_C captured from the client id line */
static uint32_t ssh_v_c_len;
static uint8_t ssh_x25519_ws[646]; /* X25519 workspace (same size tls13.c uses) */
static uint8_t ssh_sign_ws[2048];  /* ECDSA sign object workspace */
static uint8_t ssh_verify_ws[2048];/* ECDSA verify object workspace (userauth) */

/* SSH `string`: uint32 length prefix then bytes. Returns the new offset. */
static uint64_t ssh_ps(uint8_t *b, uint64_t o, const uint8_t *p, uint32_t n) {
  b[o] = (uint8_t)(n >> 24); b[o + 1] = (uint8_t)(n >> 16);
  b[o + 2] = (uint8_t)(n >> 8); b[o + 3] = (uint8_t)n;
  for (uint32_t i = 0; i < n; i++) b[o + 4 + i] = p[i];
  return o + 4 + n;
}
/* SSH `mpint`: big-endian, leading zeros stripped, 0x00 prepended if high bit set. */
static uint64_t ssh_pmp(uint8_t *b, uint64_t o, const uint8_t *p, uint32_t n) {
  uint32_t s = 0; while (s < n && p[s] == 0) s++;
  uint32_t rem = n - s;
  if (rem == 0) { b[o] = b[o + 1] = b[o + 2] = b[o + 3] = 0; return o + 4; }
  uint32_t pad = (p[s] & 0x80) ? 1u : 0u, len = rem + pad;
  b[o] = (uint8_t)(len >> 24); b[o + 1] = (uint8_t)(len >> 16);
  b[o + 2] = (uint8_t)(len >> 8); b[o + 3] = (uint8_t)len;
  uint64_t q = o + 4; if (pad) b[q++] = 0;
  for (uint32_t i = s; i < n; i++) b[q++] = p[i];
  return q;
}
/* Wrap a payload as an unencrypted binary packet (block 8, zero padding --
   content is irrelevant before NEWKEYS). Returns total wire bytes. */
static uint32_t ssh_wrap(uint8_t *out, const uint8_t *payload, uint32_t plen) {
  uint32_t pad = 8 - ((5 + plen) % 8); if (pad < 4) pad += 8;
  uint32_t pl = 1 + plen + pad;
  out[0] = (uint8_t)(pl >> 24); out[1] = (uint8_t)(pl >> 16);
  out[2] = (uint8_t)(pl >> 8);  out[3] = (uint8_t)pl;
  out[4] = (uint8_t)pad;
  for (uint32_t i = 0; i < plen; i++) out[5 + i] = payload[i];
  for (uint32_t i = 0; i < pad; i++) out[5 + plen + i] = 0;
  return 4 + pl;
}
/* Our KEXINIT payload (the profile in ssh.transport). Random cookie. */
static uint32_t ssh_build_kexinit(uint8_t *p) {
  uint64_t o = 0;
  p[o++] = 20;
  aiueos_random_bytes(p + o, 16); o += 16;
  static const char *const nl[10] = {
    "curve25519-sha256,curve25519-sha256@libssh.org",
    "ecdsa-sha2-nistp256",
    "aes128-gcm@openssh.com", "aes128-gcm@openssh.com",
    "none", "none", "none", "none", "", ""};
  for (int i = 0; i < 10; i++) {
    uint32_t n = 0; while (nl[i][n]) n++;
    o = ssh_ps(p, o, (const uint8_t *)nl[i], n);
  }
  p[o++] = 0;                                   /* first_kex_packet_follows */
  p[o++] = 0; p[o++] = 0; p[o++] = 0; p[o++] = 0; /* reserved uint32 */
  return (uint32_t)o;
}
/* The data region and length of the just-received TCP segment. */
static const uint8_t *ssh_seg_data(const uint8_t *frame, uint32_t *len) {
  uint32_t total = net_load_be16(frame + 16);
  uint32_t hdr = 4U * (uint32_t)(frame[46] >> 4);
  if (hdr < 20 || 20 + hdr > total) { *len = 0; return frame + 34 + 20; }
  *len = total - 20 - hdr;
  return frame + 34 + hdr;
}
/* The payload of a received binary packet (after the 5-byte framing), and its
   length via *plen. Returns NULL if the framing is inconsistent with dlen. */
static const uint8_t *ssh_unwrap(const uint8_t *seg, uint32_t dlen, uint32_t *plen) {
  if (dlen < 6) return 0;
  uint32_t pl = ((uint32_t)seg[0] << 24) | ((uint32_t)seg[1] << 16) |
                ((uint32_t)seg[2] << 8) | seg[3];
  uint32_t padl = seg[4];
  if (padl < 4 || pl < padl + 1 || 4 + pl > dlen) return 0;
  *plen = pl - padl - 1;
  return seg + 5;
}

/* ------------------------------------------------------------------------- */
/* Post-NEWKEYS: the aes128-gcm@openssh.com record layer and publickey userauth
   (ADR-0108). Byte layout mirrors ssh.keys / ssh.record / ssh.userauth in
   kotoba-lang/org-ietf-ssh, which an independent real-crypto client ran the whole
   login through. The crypto is the kernel's: AES-128-GCM and the ECDSA-P256
   verify object; SHA-256 for the key derivation. */
static uint32_t ssh_be32p(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
/* GCM nonce for packet `seq`: iv[0..4] fixed, iv[4..12] as a big-endian 64-bit
   counter plus seq (byte-wise carry, exact for any counter). */
static void ssh_nonce(const uint8_t iv[12], uint32_t seq, uint8_t out[12]) {
  int i; uint64_t carry;
  for (i = 0; i < 12; i++) out[i] = iv[i];
  carry = seq;
  for (i = 11; i >= 4 && carry; i--) { carry += out[i]; out[i] = (uint8_t)(carry & 0xff); carry >>= 8; }
}
/* Encrypt+frame one packet. Returns wire length, or 0 on cipher failure. */
static uint32_t ssh_seal(const uint8_t key[16], const uint8_t iv[12], uint32_t seq,
                         const uint8_t *payload, uint32_t plen, uint8_t *out) {
  uint32_t pad = 16 - ((1 + plen) % 16); if (pad < 4) pad += 16;
  uint32_t packet_length = 1 + plen + pad;
  uint8_t aad[4], nonce[12], tag[16];
  static uint8_t pt[1024];
  uint32_t i;
  aad[0] = (uint8_t)(packet_length >> 24); aad[1] = (uint8_t)(packet_length >> 16);
  aad[2] = (uint8_t)(packet_length >> 8);  aad[3] = (uint8_t)packet_length;
  pt[0] = (uint8_t)pad;
  for (i = 0; i < plen; i++) pt[1 + i] = payload[i];
  for (i = 0; i < pad; i++) pt[1 + plen + i] = 0;
  ssh_nonce(iv, seq, nonce);
  out[0] = aad[0]; out[1] = aad[1]; out[2] = aad[2]; out[3] = aad[3];
  if (!aiueos_aes128_gcm_encrypt(key, nonce, aad, 4, pt, packet_length, out + 4, tag)) return 0;
  for (i = 0; i < 16; i++) out[4 + packet_length + i] = tag[i];
  return 4 + packet_length + 16;
}
/* Deframe+decrypt one received packet from the segment. Payload to pt_out, its
   length via *plen. Returns 0 on framing or tag failure. */
static int ssh_open(const uint8_t key[16], const uint8_t iv[12], uint32_t seq,
                    const uint8_t *seg, uint32_t dlen, uint8_t *pt_out, uint32_t *plen) {
  static uint8_t pt[1024];
  uint8_t nonce[12], aad[4];
  uint32_t packet_length, padl, pl, i;
  if (dlen < 4 + 1 + 16) return 0;
  packet_length = ssh_be32p(seg);
  if (packet_length < 1 || 4 + packet_length + 16 > dlen || packet_length > sizeof(pt)) return 0;
  aad[0] = seg[0]; aad[1] = seg[1]; aad[2] = seg[2]; aad[3] = seg[3];
  ssh_nonce(iv, seq, nonce);
  if (!aiueos_aes128_gcm_decrypt(key, nonce, aad, 4, seg + 4, packet_length,
                                 seg + 4 + packet_length, pt)) return 0;
  padl = pt[0];
  if (padl < 4 || padl + 1 > packet_length) return 0;
  pl = packet_length - padl - 1;
  for (i = 0; i < pl; i++) pt_out[i] = pt[1 + i];
  *plen = pl;
  return 1;
}
/* An SSH mpint (from a signature blob) to a fixed 32-byte big-endian scalar. */
static void ssh_mpint_to_32(const uint8_t *src, uint32_t len, uint8_t out[32]) {
  uint32_t s = 0, rem, i;
  while (s < len && src[s] == 0) s++;
  rem = len - s;
  for (i = 0; i < 32; i++) out[i] = 0;
  for (i = 0; i < rem && i < 32; i++) out[32 - rem + i] = src[s + i];
}
/* Receive one PSH|ACK data segment from the peer acking `ack`, tolerant of it
   arriving late. Unlike net_tcp_receive (which gives up on the first empty
   await), this keeps posting and polling across `rounds` windows -- a userauth
   packet can arrive after a slow key-derivation or crypto step, and a single
   short spin misses it. The segment lands in rx_page for the caller to unwrap
   or decrypt. */
static int net_ssh_recv(struct net_ring *rx, uint8_t *rx_page, uint32_t ack, unsigned rounds) {
  const uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  for (unsigned r = 0; r < rounds; r++) {
    net_post(rx);
    if (!net_await(rx->used, rx->posted)) continue;   /* empty window; retry */
    uint32_t received = rx->used->ring[0].length;
    if (rx->used->ring[0].id != 0 ||
        received <= sizeof(struct virtio_net_hdr) ||
        received > sizeof(struct virtio_net_hdr) + NET_FRAME_MAX) continue;
    if (kotoba_aiueos_tcp_segment_valid((uint64_t)(uintptr_t)frame,
          received - (uint32_t)sizeof(struct virtio_net_hdr), NET_PEER_IP, ack,
          NET_TCP_PSH | NET_TCP_ACK)) return 1;
  }
  return 0;
}

/* Drive publickey userauth after NEWKEYS. Derives the session keys, receives the
   client's NEWKEYS, then the encrypted SERVICE_REQUEST / USERAUTH_REQUEST, checks
   the offered key is the authorized one and its signature over the session's
   signed-data verifies, and answers SERVICE_ACCEPT / USERAUTH_SUCCESS. Sets
   ssh_kex_stage 6..9. Returns 1 iff USERAUTH_SUCCESS was sent. */
static int net_ssh_userauth(struct net_ring *rx, struct net_ring *tx,
                            uint8_t *rx_page, uint8_t *tx_page, uint16_t cport,
                            uint32_t sseq, uint32_t pnext,
                            const uint8_t *k, const uint8_t *h) {
  const uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  static uint8_t pkt[1024];
  static uint8_t kd[128];
  static uint8_t up[1024];
  uint8_t key_cs[16], iv_cs[12], key_sc[16], iv_sc[12], d32[32];
  uint32_t uplen = 0;

  ssh_kex_stage = 6;   /* userauth entered (granular stages 6..12 below) */

  /* session keys (RFC 4253 §7.2), session_id = H:
       HASH(mpint(K) || H || letter || H), truncated. */
  {
    static const char letters[4] = {'A', 'B', 'C', 'D'};
    uint8_t *outs[4] = {iv_cs, iv_sc, key_cs, key_sc};
    uint32_t lens[4] = {12, 12, 16, 16};
    for (int li = 0; li < 4; li++) {
      uint64_t o = ssh_pmp(kd, 0, k, 32);
      for (int i = 0; i < 32; i++) kd[o + i] = h[i]; o += 32;
      kd[o++] = (uint8_t)letters[li];
      for (int i = 0; i < 32; i++) kd[o + i] = h[i]; o += 32;
      kotoba_aiueos_sha256(kd, o, d32, sha256_workspace, sizeof(sha256_workspace));
      for (uint32_t i = 0; i < lens[li]; i++) outs[li][i] = d32[i];
    }
  }

  /* 1. the client's NEWKEYS (unencrypted, msg 21). Tolerant receive: it may
        arrive after our key derivation, past a single net_tcp_receive spin. */
  if (!net_ssh_recv(rx, rx_page, sseq, 96)) return 0;
  {
    uint32_t dlen = 0; const uint8_t *seg = ssh_seg_data(frame, &dlen);
    uint32_t plen = 0; const uint8_t *pay = ssh_unwrap(seg, dlen, &plen);
    if (!pay || plen < 1 || pay[0] != 21) return 0;
    pnext += dlen; ssh_kex_stage = 7;
  }
  net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport, sseq, pnext, NET_TCP_ACK, 0, 0);

  /* 2. encrypted SERVICE_REQUEST (c->s seq 0): byte 5 + string "ssh-userauth". */
  if (!net_ssh_recv(rx, rx_page, sseq, 96)) return 0;
  {
    uint32_t dlen = 0; const uint8_t *seg = ssh_seg_data(frame, &dlen);
    if (!ssh_open(key_cs, iv_cs, 0, seg, dlen, up, &uplen)) return 0;
    if (uplen < 1 || up[0] != 5) return 0;
    pnext += dlen; ssh_kex_stage = 8;
  }

  /* 3. SERVICE_ACCEPT (s->c seq 0). */
  {
    uint8_t sa[32]; uint64_t o = 0;
    sa[o++] = 6; o = ssh_ps(sa, o, (const uint8_t *)"ssh-userauth", 12);
    uint32_t wl = ssh_seal(key_sc, iv_sc, 0, sa, (uint32_t)o, pkt);
    if (!wl || !net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                             sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl)) return 0;
    sseq += wl; ssh_kex_stage = 9;
  }

  /* 4. encrypted USERAUTH_REQUEST (c->s seq 1): byte 50, then
        string user, string "ssh-connection", string "publickey", bool 1,
        string algo, string pk-blob, string sig-blob. */
  if (!net_ssh_recv(rx, rx_page, sseq, 96)) return 0;
  {
    uint32_t dlen = 0; const uint8_t *seg = ssh_seg_data(frame, &dlen);
    if (!ssh_open(key_cs, iv_cs, 1, seg, dlen, up, &uplen)) return 0;
    pnext += dlen; ssh_kex_stage = 10;
  }
  if (uplen < 1 || up[0] != 50) return 0;

  /* Parse to the pk-blob and the sig, and find where signed-data ends (the
     request up to but not including the trailing signature string). */
  {
    uint32_t off = 1, pkblob_off, sig_off, i;
    off += 4 + ssh_be32p(up + off);   /* username */
    off += 4 + ssh_be32p(up + off);   /* service  */
    off += 4 + ssh_be32p(up + off);   /* method   */
    off += 1;                          /* bool     */
    off += 4 + ssh_be32p(up + off);   /* algo     */
    pkblob_off = off;
    off += 4 + ssh_be32p(up + off);   /* pk-blob  */
    sig_off = off;                     /* signed-data ends here */

    /* signed-data = string(H) || up[0..sig_off]; digest = SHA256(signed-data). */
    {
      static uint8_t sd[1024]; uint64_t so = 0; uint8_t digest[32];
      so = ssh_ps(sd, so, h, 32);
      for (i = 0; i < sig_off; i++) sd[so + i] = up[i]; so += sig_off;
      kotoba_aiueos_sha256(sd, so, digest, sha256_workspace, sizeof(sha256_workspace));

      /* offered public point from the pk-blob (string algo, string curve, string point). */
      {
        const uint8_t *pkb = up + pkblob_off + 4;
        uint32_t p2 = 0;
        uint8_t pub[64]; int authorized = 1;
        p2 += 4 + ssh_be32p(pkb + p2);   /* algo  */
        p2 += 4 + ssh_be32p(pkb + p2);   /* curve */
        p2 += 4;                          /* into the point string */
        /* point = 0x04 || x(32) || y(32) */
        for (i = 0; i < 32; i++) { pub[i] = pkb[p2 + 1 + i]; pub[32 + i] = pkb[p2 + 1 + 32 + i]; }
        for (i = 0; i < 32; i++) if (pub[i] != ssh_auth_x[i] || pub[32 + i] != ssh_auth_y[i]) authorized = 0;
        if (!authorized) return 0;

        /* signature r||s from the sig-blob (string algo, string (mpint r, mpint s)). */
        {
          const uint8_t *sigstr = up + sig_off + 4;   /* sig-blob content */
          uint32_t s2 = 0, innoff, rn, sn;
          uint8_t rs[64];
          s2 += 4 + ssh_be32p(sigstr + s2);            /* algo */
          s2 += 4;                                      /* into the inner string */
          innoff = s2;
          rn = ssh_be32p(sigstr + innoff); innoff += 4;
          ssh_mpint_to_32(sigstr + innoff, rn, rs);
          innoff += rn;
          sn = ssh_be32p(sigstr + innoff); innoff += 4;
          ssh_mpint_to_32(sigstr + innoff, sn, rs + 32);

          if (!kotoba_aiueos_ecdsa_p256_sha256_verify(rs, digest, pub, ssh_verify_ws, 2048)) return 0;
          ssh_kex_stage = 11;
        }
      }
    }
  }

  /* 5. USERAUTH_SUCCESS (s->c seq 1): byte 52. */
  {
    uint8_t suc = 52;
    uint32_t wl = ssh_seal(key_sc, iv_sc, 1, &suc, 1, pkt);
    if (!wl || !net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                             sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl)) return 0;
    sseq += wl; ssh_kex_stage = 12;
  }

  /* ---- the session channel (ADR-0109) ------------------------------------
     The login has succeeded; from here everything is best-effort (return 1, not
     0, so the AUTH marker still fires). Packet counters continue: c->s is at 2
     (service-request 0, userauth-request 1), s->c at 2 (service-accept 0,
     userauth-success 1). A minimal but real `exec` session: open the channel,
     accept the command, and stream one CHANNEL_DATA that echoes it. */
  {
    uint32_t client_chan = 0;

    /* 6. CHANNEL_OPEN (c->s 2): byte 90, string type, uint32 sender, window, max. */
    if (!net_ssh_recv(rx, rx_page, sseq, 96)) return 1;
    {
      uint32_t dlen = 0; const uint8_t *seg = ssh_seg_data(frame, &dlen);
      if (!ssh_open(key_cs, iv_cs, 2, seg, dlen, up, &uplen)) return 1;
      if (uplen < 1 || up[0] != 90) return 1;
      uint32_t off = 1;
      off += 4 + ssh_be32p(up + off);      /* skip channel-type string */
      client_chan = ssh_be32p(up + off);
      pnext += dlen; ssh_kex_stage = 13;
    }

    /* 7. CHANNEL_OPEN_CONFIRMATION (s->c 2): recipient, sender=0, window, max. */
    {
      uint8_t m[24]; uint64_t o = 0;
      m[o++] = 91;
      m[o++] = (uint8_t)(client_chan >> 24); m[o++] = (uint8_t)(client_chan >> 16);
      m[o++] = (uint8_t)(client_chan >> 8);  m[o++] = (uint8_t)client_chan;
      m[o++] = 0; m[o++] = 0; m[o++] = 0; m[o++] = 0;          /* sender = 0 */
      m[o++] = 0; m[o++] = 0x10; m[o++] = 0; m[o++] = 0;       /* window = 0x100000 */
      m[o++] = 0; m[o++] = 0; m[o++] = 0x80; m[o++] = 0;       /* max packet = 0x8000 */
      uint32_t wl = ssh_seal(key_sc, iv_sc, 2, m, (uint32_t)o, pkt);
      if (!wl || !net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                               sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl)) return 1;
      sseq += wl; ssh_kex_stage = 14;
    }

    /* 8. CHANNEL_REQUEST (c->s 3): byte 98, recipient, string type, bool, [string cmd]. */
    static uint8_t cmd[256]; uint32_t cmdlen = 0;
    if (!net_ssh_recv(rx, rx_page, sseq, 96)) return 1;
    {
      uint32_t dlen = 0; const uint8_t *seg = ssh_seg_data(frame, &dlen);
      if (!ssh_open(key_cs, iv_cs, 3, seg, dlen, up, &uplen)) return 1;
      if (uplen < 1 || up[0] != 98) return 1;
      uint32_t off = 1 + 4;                 /* byte + recipient */
      off += 4 + ssh_be32p(up + off);       /* skip request-type string */
      off += 1;                              /* skip bool want-reply */
      if (off + 4 <= uplen) {               /* exec carries a command string */
        cmdlen = ssh_be32p(up + off); off += 4;
        if (cmdlen > sizeof(cmd)) cmdlen = sizeof(cmd);
        for (uint32_t i = 0; i < cmdlen; i++) cmd[i] = up[off + i];
      }
      pnext += dlen; ssh_kex_stage = 15;
    }

    /* 9. CHANNEL_SUCCESS (s->c 3). */
    {
      uint8_t m[8]; uint64_t o = 0;
      m[o++] = 99;
      m[o++] = (uint8_t)(client_chan >> 24); m[o++] = (uint8_t)(client_chan >> 16);
      m[o++] = (uint8_t)(client_chan >> 8);  m[o++] = (uint8_t)client_chan;
      uint32_t wl = ssh_seal(key_sc, iv_sc, 3, m, (uint32_t)o, pkt);
      if (!wl || !net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                               sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl)) return 1;
      sseq += wl;
    }

    /* 10. CHANNEL_DATA (s->c 4): "aiueos: <command>\n" -- echoing the command
           proves the whole session round-trip (open, exec parsed, data returned). */
    {
      static uint8_t out[512]; uint32_t olen = 0;
      const char *pfx = "aiueos: "; uint32_t i;
      for (i = 0; pfx[i]; i++) out[olen++] = (uint8_t)pfx[i];
      for (i = 0; i < cmdlen && olen < sizeof(out) - 1; i++) out[olen++] = cmd[i];
      out[olen++] = '\n';
      uint8_t m[600]; uint64_t o = 0;
      m[o++] = 94;
      m[o++] = (uint8_t)(client_chan >> 24); m[o++] = (uint8_t)(client_chan >> 16);
      m[o++] = (uint8_t)(client_chan >> 8);  m[o++] = (uint8_t)client_chan;
      o = ssh_ps(m, o, out, olen);
      uint32_t wl = ssh_seal(key_sc, iv_sc, 4, m, (uint32_t)o, pkt);
      if (!wl || !net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                               sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl)) return 1;
      sseq += wl; ssh_kex_stage = 16;
    }

    /* 11. exit-status (s->c 5), CHANNEL_EOF (6), CHANNEL_CLOSE (7): best effort. */
    {
      uint8_t es[32]; uint64_t o = 0;
      es[o++] = 98;
      es[o++] = (uint8_t)(client_chan >> 24); es[o++] = (uint8_t)(client_chan >> 16);
      es[o++] = (uint8_t)(client_chan >> 8);  es[o++] = (uint8_t)client_chan;
      o = ssh_ps(es, o, (const uint8_t *)"exit-status", 11);
      es[o++] = 0;                                     /* want-reply = FALSE */
      es[o++] = 0; es[o++] = 0; es[o++] = 0; es[o++] = 0;   /* status 0 */
      uint32_t wl = ssh_seal(key_sc, iv_sc, 5, es, (uint32_t)o, pkt);
      if (wl) net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                           sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl);
      sseq += wl;
      uint8_t eof[8]; o = 0;
      eof[o++] = 96;
      eof[o++] = (uint8_t)(client_chan >> 24); eof[o++] = (uint8_t)(client_chan >> 16);
      eof[o++] = (uint8_t)(client_chan >> 8);  eof[o++] = (uint8_t)client_chan;
      wl = ssh_seal(key_sc, iv_sc, 6, eof, (uint32_t)o, pkt);
      if (wl) net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                           sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl);
      sseq += wl;
      uint8_t cls[8]; o = 0;
      cls[o++] = 97;
      cls[o++] = (uint8_t)(client_chan >> 24); cls[o++] = (uint8_t)(client_chan >> 16);
      cls[o++] = (uint8_t)(client_chan >> 8);  cls[o++] = (uint8_t)client_chan;
      wl = ssh_seal(key_sc, iv_sc, 7, cls, (uint32_t)o, pkt);
      if (wl) net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                           sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl);
    }
  }
  return 1;
}

/* Drive the server side of the kex from the point the identification strings
   have been exchanged. `sseq` is our next send sequence number, `pnext` the
   client's next (our ack). Bounded throughout; sets ssh_kex_stage as it goes so
   a boot shows exactly how far it got. Returns 1 iff KEX_ECDH_REPLY+NEWKEYS were
   sent. Cooperating segmentation (one ACK between the client's two packets)
   works within the one-buffer RX; the crypto and byte formats are real. */
static int net_ssh_kex(struct net_ring *rx, struct net_ring *tx,
                       uint8_t *rx_page, uint8_t *tx_page,
                       uint16_t cport, uint32_t sseq, uint32_t pnext) {
  const uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  static uint8_t is_payload[512];
  static uint8_t ic_payload[1024];
  static uint8_t pkt[1024];
  uint32_t is_len = ssh_build_kexinit(is_payload);

  /* 1. send our KEXINIT (I_S). */
  {
    uint32_t wl = ssh_wrap(pkt, is_payload, is_len);
    if (!net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                      sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, wl)) return 0;
    sseq += wl;
    ssh_kex_stage = 1;
  }

  /* 2. receive the client's KEXINIT (I_C). It acks our KEXINIT (expected_ack). */
  net_post(rx);
  if (!net_tcp_receive(rx, rx_page, NET_PEER_IP, sseq, NET_TCP_PSH | NET_TCP_ACK, 8))
    return 0;
  uint32_t ic_len = 0;
  {
    uint32_t dlen = 0;
    const uint8_t *seg = ssh_seg_data(frame, &dlen);
    uint32_t plen = 0;
    const uint8_t *pay = ssh_unwrap(seg, dlen, &plen);
    if (!pay || plen > sizeof(ic_payload)) return 0;
    for (uint32_t i = 0; i < plen; i++) ic_payload[i] = pay[i];
    ic_len = plen;
    pnext += dlen;
    ssh_kex_stage = 2;
  }

  /* 3. ACK the client's KEXINIT so it will send KEX_ECDH_INIT (one-buffer RX). */
  net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
               sseq, pnext, NET_TCP_ACK, 0, 0);

  /* 4. receive KEX_ECDH_INIT and extract Q_C. */
  uint8_t q_c[32];
  net_post(rx);
  if (!net_tcp_receive(rx, rx_page, NET_PEER_IP, sseq, NET_TCP_PSH | NET_TCP_ACK, 8))
    return 0;
  {
    uint32_t dlen = 0;
    const uint8_t *seg = ssh_seg_data(frame, &dlen);
    uint32_t plen = 0;
    const uint8_t *pay = ssh_unwrap(seg, dlen, &plen);
    if (!pay || plen < 37 || pay[0] != 30) return 0;   /* byte 30 + string(32) */
    for (int i = 0; i < 32; i++) q_c[i] = pay[5 + i];
    pnext += dlen;
    ssh_kex_stage = 3;
  }

  /* 5. ephemeral X25519, shared secret, host-key blob, exchange hash H. */
  uint8_t eph[32], q_s[32], k[32];
  aiueos_random_bytes(eph, 32);
  if (!kotoba_aiueos_x25519(eph, x25519_base9, q_s, ssh_x25519_ws)) return 0;
  if (!kotoba_aiueos_x25519(eph, q_c, k, ssh_x25519_ws)) return 0;

  static uint8_t ks[128];
  uint32_t kslen = 0;
  kslen = (uint32_t)ssh_ps(ks, kslen, (const uint8_t *)"ecdsa-sha2-nistp256", 19);
  kslen = (uint32_t)ssh_ps(ks, kslen, (const uint8_t *)"nistp256", 8);
  {
    uint8_t point[65];
    point[0] = 0x04;
    for (int i = 0; i < 32; i++) { point[1 + i] = ssh_host_x[i]; point[33 + i] = ssh_host_y[i]; }
    kslen = (uint32_t)ssh_ps(ks, kslen, point, 65);
  }

  static uint8_t tr[1536];
  uint64_t to = 0;
  to = ssh_ps(tr, to, ssh_v_c, ssh_v_c_len);
  to = ssh_ps(tr, to, ssh_v_s, SSH_V_S_LEN);
  to = ssh_ps(tr, to, ic_payload, ic_len);
  to = ssh_ps(tr, to, is_payload, is_len);
  to = ssh_ps(tr, to, ks, kslen);
  to = ssh_ps(tr, to, q_c, 32);
  to = ssh_ps(tr, to, q_s, 32);
  to = ssh_pmp(tr, to, k, 32);

  uint8_t h[32], e[32];
  if (!kotoba_aiueos_sha256(tr, to, h, sha256_workspace, sizeof(sha256_workspace)))
    return 0;
  /* ecdsa-sha2-nistp256 signs H as an ECDSA-with-SHA256 message: digest = SHA256(H). */
  if (!kotoba_aiueos_sha256(h, 32, e, sha256_workspace, sizeof(sha256_workspace)))
    return 0;

  /* 6. sign H's digest. k nonce: random, top bit cleared so k < 2^255 < n
        (a valid [1,n-1] nonce without an HMAC_DRBG in the kernel). Retry on the
        object's r==0/s==0 refusal (astronomically rare). */
  uint8_t rs[64];
  int signed_ok = 0;
  for (int t = 0; t < 8 && !signed_ok; t++) {
    uint8_t nonce[32];
    aiueos_random_bytes(nonce, 32);
    nonce[0] &= 0x7f;
    if (kotoba_aiueos_ecdsa_p256_sign(ssh_host_d, e, nonce, rs, ssh_sign_ws))
      signed_ok = 1;
  }
  if (!signed_ok) return 0;

  /* 7. signature blob, KEX_ECDH_REPLY, send. */
  static uint8_t sig[128];
  uint64_t so = 0;
  {
    uint8_t inner[80];
    uint64_t io = 0;
    io = ssh_pmp(inner, io, rs, 32);
    io = ssh_pmp(inner, io, rs + 32, 32);
    so = ssh_ps(sig, so, (const uint8_t *)"ecdsa-sha2-nistp256", 19);
    so = ssh_ps(sig, so, inner, (uint32_t)io);
  }
  static uint8_t rep[512];
  uint64_t ro = 0;
  rep[ro++] = 31;                                  /* SSH_MSG_KEX_ECDH_REPLY */
  ro = ssh_ps(rep, ro, ks, kslen);
  ro = ssh_ps(rep, ro, q_s, 32);
  ro = ssh_ps(rep, ro, sig, (uint32_t)so);
  /* 8. KEX_ECDH_REPLY and NEWKEYS in ONE segment, so the client acks both
        before it sends its own NEWKEYS -- otherwise it acks only the reply and
        net_ssh_userauth's first receive rejects the mismatched ack. */
  {
    uint32_t rwl = ssh_wrap(pkt, rep, (uint32_t)ro);
    uint8_t nk = 21;
    uint32_t nwl = ssh_wrap(pkt + rwl, &nk, 1);
    if (!net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                      sseq, pnext, NET_TCP_PSH | NET_TCP_ACK, pkt, rwl + nwl)) return 0;
    sseq += rwl + nwl;
    ssh_kex_stage = 5;
  }

  /* 9. the encrypted layer and publickey userauth (ADR-0108). Best effort: the
        kex reply is already the I4-critical evidence; if the client does not go
        on to authenticate, the boot still shows AIUEOS_SSH_KEX_REPLY_OK and the
        userauth stage says how far it got. */
  net_ssh_userauth(rx, tx, rx_page, tx_page, cport, sseq, pnext, k, h);
  return 1;
}

/* Accept ONE inbound SSH connection and exchange identification strings. This
   mirrors net_tcp_probe with the roles reversed: the peer opens, we answer.
   Reuses tcp-segment-valid (already linked) for inbound admission and
   net_tcp_send (self-checked) for the answers. Bounded throughout: a peer that
   never connects, or a step that never completes, returns 0 and the boot
   continues -- the listener can only ADD an evidence marker, never withhold
   one the network chain already earned. Returns 1 only when a well-formed SSH
   identification string was received over a connection we accepted. */
static int net_ssh_listen(struct net_ring *rx, struct net_ring *tx,
                          uint8_t *rx_page, uint8_t *tx_page) {
  const uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  if (!net_peer_mac_known) return 0;

  /* 1. wait for an inbound SYN. tcp-segment-valid pins src and flags; a SYN's
        acknowledgement field is zero, so expected-ack zero is exact. The peer
        is SLIRP's gateway (NET_PEER_IP) because a hostfwd connection is
        originated by SLIRP toward the guest. Unlike the client probes, we did
        not send anything to prompt this, so the SYN may not have arrived when
        we first look; SLIRP retransmits it on a ~1s/3s schedule, so we re-post
        and re-await across enough windows to catch a retransmit rather than
        giving up on the first empty poll. */
  {
    int got = 0;
    for (unsigned attempt = 0; attempt < 64 && !got; attempt++) {
      net_post(rx);
      if (!net_await(rx->used, rx->posted)) continue;       /* empty window, try again */
      uint32_t received = rx->used->ring[0].length;
      if (rx->used->ring[0].id != 0 ||
          received <= sizeof(struct virtio_net_hdr) ||
          received > sizeof(struct virtio_net_hdr) + NET_FRAME_MAX) continue;
      if (kotoba_aiueos_tcp_segment_valid(
            (uint64_t)(uintptr_t)frame,
            received - (uint32_t)sizeof(struct virtio_net_hdr),
            NET_PEER_IP, 0, NET_TCP_SYN)) got = 1;
    }
    if (!got) return 0;
  }
  ssh_listen_stage = 1;
  if (net_load_be16(frame + 36) != NET_SSH_PORT) return 0;   /* not for :22 */
  uint16_t cport = net_load_be16(frame + 34);
  uint32_t cseq = net_load_be32(frame + 38);

  /* 2. SYN|ACK: our ISN, acknowledging the client's SYN (one sequence number). */
  if (!net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                    NET_SSH_ISN, cseq + 1, NET_TCP_SYN | NET_TCP_ACK, 0, 0))
    return 0;

  /* 3. the client's bare ACK completing the handshake. A cooperating client
        waits for our banner before sending data, so this ACK arrives alone. */
  net_post(rx);
  if (!net_tcp_receive(rx, rx_page, NET_PEER_IP, NET_SSH_ISN + 1, NET_TCP_ACK, 8))
    return 0;
  ssh_listen_stage = 2;

  /* 4. send our identification string. */
  if (!net_tcp_send(tx, tx_page, NET_GUEST_IP, NET_PEER_IP, NET_SSH_PORT, cport,
                    NET_SSH_ISN + 1, cseq + 1, NET_TCP_PSH | NET_TCP_ACK,
                    net_ssh_id, NET_SSH_ID_LEN))
    return 0;
  ssh_listen_stage = 3;

  /* 5. the client's identification string, acknowledging ours. */
  net_post(rx);
  if (!net_tcp_receive(rx, rx_page, NET_PEER_IP, NET_SSH_ISN + 1 + NET_SSH_ID_LEN,
                       NET_TCP_PSH | NET_TCP_ACK, 8))
    return 0;
  ssh_listen_stage = 4;
  {
    uint32_t total = net_load_be16(frame + 16);            /* IPv4 total length */
    uint32_t hdr = 4U * (uint32_t)(frame[46] >> 4);        /* TCP data offset */
    if (hdr < 20 || 20 + hdr > total) return 0;
    uint32_t dlen = total - 20 - hdr;
    const uint8_t *data = frame + 34 + hdr;
    ssh_client_id_len = dlen;
    ssh_client_id_valid = net_ssh_id_prefix_ok(data, dlen);
    uint32_t cnext = cseq + 1 + dlen;

    /* Capture V_C (the id line WITHOUT CR-LF) for the exchange-hash transcript,
       before net_ssh_kex overwrites the RX page. */
    {
      uint32_t vlen = dlen;
      while (vlen > 0 && (data[vlen - 1] == '\r' || data[vlen - 1] == '\n')) vlen--;
      if (vlen > sizeof(ssh_v_c)) vlen = sizeof(ssh_v_c);
      for (uint32_t i = 0; i < vlen; i++) ssh_v_c[i] = data[i];
      ssh_v_c_len = vlen;
    }

    /* 6. the real curve25519-sha256 kex (KEXINIT / KEX_ECDH_REPLY / NEWKEYS).
          Our KEXINIT acks the client's id, so no separate bare ACK first. */
    if (ssh_client_id_valid)
      net_ssh_kex(rx, tx, rx_page, tx_page, cport,
                  NET_SSH_ISN + 1 + NET_SSH_ID_LEN, cnext);
  }
  return ssh_client_id_valid;
}
#endif /* AIUEOS_SSH_LISTEN */

/* ------------------------------------------------------------------------- */
/* DHCPv4: the first address this machine holds because a server said so.     */
/*                                                                           */
/* UDP APPEARS HERE AND THIS IS NOT A UDP STACK. There is no socket, no port  */
/* table, no demultiplexer and no receive path for anything that is not this  */
/* exchange: exactly the header construction and the one checksum DHCP needs, */
/* and nothing that a second protocol could reuse without being written. The  */
/* datagram is built here and verified in `dhcp-reply-valid.kotoba`, which is */
/* the only place the received checksum is ever checked.                      */
/*                                                                           */
/* This is also a PROBE and not a client. It performs one DISCOVER/OFFER/     */
/* REQUEST/ACK once, at boot, and then stops: no T1/T2 renewal timers, no     */
/* rebinding, no DECLINE when the address is already in use, no RELEASE at    */
/* shutdown, no retransmission with backoff if a datagram is lost, and no     */
/* second interface. The lease is recorded and then never renewed, so a       */
/* machine that stays up past its lease keeps using an address it no longer   */
/* holds. ADR-0081 consumes it for the DNS/cloud-TCP probes: those datagrams  */
/* take their source from dhcp_address, never from NET_GUEST_IP.              */
/* ------------------------------------------------------------------------- */

/* Fixed for the same reason NET_ICMP_IDENT is: one exchange per boot, and a
   constant keeps a capture of a failed run readable. A real client picks this
   randomly, and an attacker who can guess it can answer a DISCOVER it never
   saw -- which is why the object checks it at all, and why this constant is a
   property of a probe rather than of the protocol. */
#define NET_DHCP_XID 0xa1e05dc0U
#define NET_DHCP_DISCOVER 1
#define NET_DHCP_REQUEST 3
#define NET_DHCP_OFFER 2
#define NET_DHCP_ACK 5
/* 236 bytes of BOOTP header, 4 of magic cookie, then a fixed 64-byte options
   field zero-padded after the END marker. 304 total, over RFC 2131's 300-byte
   minimum message size, which some servers enforce and SLIRP does not. */
#define NET_DHCP_OPTIONS 64
#define NET_DHCP_BOOTP (236 + 4 + NET_DHCP_OPTIONS)

#define NET_DHCP_STAGE_IDLE 0
#define NET_DHCP_STAGE_TX_DISCOVER 1
#define NET_DHCP_STAGE_OFFER 2
#define NET_DHCP_STAGE_TX_REQUEST 3
#define NET_DHCP_STAGE_ACK 4
#define NET_DHCP_STAGE_DONE 5

/* Never touched by DMA -- only the CPU reads it -- so it is static rather than
   an allocated page, for the reason net_tcp_scratch is. */
static uint8_t net_dhcp_scratch[2048] __attribute__((aligned(16)));
static uint32_t dhcp_frame_length;

static uint64_t net_mac_value(void) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 6; i++) value = (value << 8) | net_mac[i];
  return value;
}

/* The pseudo-header cannot be overlaid on the frame -- to be contiguous with
   the UDP header it would have to start at frame offset 22, which puts its
   address fields four bytes away from where the IPv4 header carries them -- so
   the two are laid out contiguously here and summed as one range by the object
   that already computes exactly this sum for ICMP and TCP. Laying bytes out is
   mechanism; the arithmetic is not. */
static uint16_t net_udp_checksum(const uint8_t *frame, uint32_t udp_length) {
  net_store_be32(net_dhcp_scratch, net_load_be32(frame + 26));
  net_store_be32(net_dhcp_scratch + 4, net_load_be32(frame + 30));
  net_dhcp_scratch[8] = 0;
  net_dhcp_scratch[9] = 17;                                  /* protocol UDP */
  net_store_be16(net_dhcp_scratch + 10, (uint16_t)udp_length);
  for (uint32_t i = 0; i < udp_length; i++) net_dhcp_scratch[12 + i] = frame[34 + i];
  uint16_t sum = (uint16_t)kotoba_aiueos_ipv4_checksum(
    (uint64_t)(uintptr_t)net_dhcp_scratch, 12 + udp_length);
  /* RFC 768: a computed checksum of zero goes on the wire as all ones, because
     zero already means "not computed". The admission accepts both. */
  return sum ? sum : 0xffff;
}

static uint32_t net_build_dhcp(uint8_t *frame, uint8_t message_type,
                               uint32_t requested, uint32_t server) {
  uint32_t udp_length = 8 + NET_DHCP_BOOTP;
  uint32_t total = 20 + udp_length;
  for (uint32_t i = 0; i < 14 + total; i++) frame[i] = 0;
  for (unsigned i = 0; i < 6; i++) frame[i] = 0xff;          /* broadcast */
  for (unsigned i = 0; i < 6; i++) frame[6 + i] = net_mac[i];
  net_store_be16(frame + 12, 0x0800);                        /* EtherType IPv4 */
  frame[14] = 0x45;                                          /* version 4, IHL 5 */
  net_store_be16(frame + 16, (uint16_t)total);
  net_store_be16(frame + 20, 0x4000);                        /* DF, id 0 (RFC 6864) */
  frame[22] = 64;                                            /* TTL */
  frame[23] = 17;                                            /* UDP */
  /* 0.0.0.0 to 255.255.255.255: this machine has no address to send from and
     does not know who to send to, which is the whole reason the exchange
     exists. */
  net_store_be32(frame + 26, 0);
  net_store_be32(frame + 30, 0xffffffffU);
  net_store_be16(frame + 24,
    (uint16_t)kotoba_aiueos_ipv4_checksum((uint64_t)(uintptr_t)(frame + 14), 20));
  net_store_be16(frame + 34, 68);                            /* client port */
  net_store_be16(frame + 36, 67);                            /* server port */
  net_store_be16(frame + 38, (uint16_t)udp_length);
  frame[42] = 1;                                             /* BOOTREQUEST */
  frame[43] = 1;                                             /* Ethernet */
  frame[44] = 6;                                             /* 6-byte address */
  net_store_be32(frame + 46, NET_DHCP_XID);
  /* The broadcast flag. A client with no address configured cannot receive a
     unicast datagram addressed to an address it does not yet hold, so RFC 2131
     has it ask for the reply to be broadcast. */
  net_store_be16(frame + 52, 0x8000);
  for (unsigned i = 0; i < 6; i++) frame[70 + i] = net_mac[i];
  net_store_be32(frame + 278, 0x63825363U);                  /* magic cookie */
  uint32_t at = 282;
  frame[at++] = 53; frame[at++] = 1; frame[at++] = message_type;
  if (message_type == NET_DHCP_REQUEST) {
    /* Both are required in a REQUEST that answers an OFFER: the address being
       accepted, and which server offered it, so that a second server's offer is
       declined by omission rather than by silence. */
    frame[at++] = 50; frame[at++] = 4; net_store_be32(frame + at, requested); at += 4;
    frame[at++] = 54; frame[at++] = 4; net_store_be32(frame + at, server); at += 4;
  }
  /* Parameter request list: subnet mask, router, DNS, lease time. Asking is not
     receiving -- the admission requires the mask, the server identifier and the
     lease regardless of what was asked for. */
  frame[at++] = 55; frame[at++] = 4;
  frame[at++] = 1; frame[at++] = 3; frame[at++] = 6; frame[at++] = 51;
  frame[at++] = 255;                                         /* END */
  net_store_be16(frame + 40, net_udp_checksum(frame, udp_length));
  return 14 + total;
}

static int net_dhcp_send(struct net_ring *tx, uint8_t *tx_page,
                         uint8_t message_type, uint32_t requested, uint32_t server) {
  uint8_t *frame = tx_page + sizeof(struct virtio_net_hdr);
  for (unsigned i = 0; i < sizeof(struct virtio_net_hdr); i++) tx_page[i] = 0;
  uint32_t frame_length = net_build_dhcp(frame, message_type, requested, server);
  tx->desc[0].length = (uint32_t)sizeof(struct virtio_net_hdr) + frame_length;
  net_post(tx);
  if (!net_await(tx->used, tx->posted) || tx->used->ring[0].id != 0) return 0;
  return 1;
}

#if AIUEOS_DHCP_TAMPER
/* TEST-ONLY. Breaks a received reply in exactly one way so the gate can show
   that the object refuses it, and refuses it for the reason that was broken
   rather than for some other reason that happened to fire first. Compiled in
   only under -DAIUEOS_DHCP_TAMPER.
   The UDP checksum is recomputed afterwards with the same helper the transmit
   path uses, so the tampered datagram is well-formed in every respect except
   the one defect. Without that the object would refuse at the checksum (3) and
   the run would be red for a reason nobody chose. */
static int dhcp_tamper_applied;
int aiueos_dhcp_tamper_applied(void) { return dhcp_tamper_applied; }

static void net_dhcp_tamper(uint8_t *frame, uint32_t length) {
  uint32_t total = net_load_be16(frame + 16);
  uint32_t limit = 14 + total;
  if (length < 283 || limit > length || limit < 300) return;
  if (frame[23] != 17 || net_load_be16(frame + 34) != 67) return;
#if AIUEOS_DHCP_TAMPER == 1
  /* A reply carrying somebody else's transaction id. */
  frame[49] ^= 0xff;
  dhcp_tamper_applied = 1;
#elif AIUEOS_DHCP_TAMPER == 2
  /* A reply of the wrong message type: the OFFER becomes an ACK, which is a
     perfectly well-formed DHCP message and is not what was asked for. Guarded
     on finding the type option exactly where the server put it, so a layout
     change reports "not applied" instead of quietly passing. */
  if (frame[282] == 53 && frame[283] == 1 && frame[284] == NET_DHCP_OFFER) {
    frame[284] = NET_DHCP_ACK;
    dhcp_tamper_applied = 1;
  }
#elif AIUEOS_DHCP_TAMPER == 3
  /* An options field whose last record claims 255 bytes that are not there.
     The field is filled with PAD so the walk is forced to reach the record
     rather than stopping at an END before it; the record then starts ten bytes
     from the end of the datagram and claims to run 247 bytes past it. */
  for (uint32_t i = 282; i < limit - 10; i++) frame[i] = 0;
  frame[limit - 10] = 1;
  frame[limit - 9] = 255;
  dhcp_tamper_applied = 1;
#endif
  net_store_be16(frame + 40, 0);
  net_store_be16(frame + 40, net_udp_checksum(frame, total - 20));
}
#endif

/* One admitted reply of the expected type, or nothing. The receive ring holds
   exactly one buffer, so the alternation is post-one-consume-one; the gateway's
   own ARP traffic lands here too and is refused by the object at its first
   clause. */
static int net_dhcp_receive(struct net_ring *rx, uint8_t *rx_page,
                            uint8_t expected_type) {
  uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  for (unsigned attempt = 0; attempt < 8; attempt++) {
    if (attempt) net_post(rx);
    if (!net_await(rx->used, rx->posted)) return 0;
    uint32_t received = rx->used->ring[0].length;
    if (rx->used->ring[0].id != 0 ||
        received <= sizeof(struct virtio_net_hdr) ||
        received > sizeof(struct virtio_net_hdr) + NET_FRAME_MAX) continue;
    uint32_t payload = received - (uint32_t)sizeof(struct virtio_net_hdr);
#if AIUEOS_DHCP_TAMPER
    net_dhcp_tamper(frame, payload);
#endif
    uint64_t reason = kotoba_aiueos_dhcp_reply_valid(
      (uint64_t)(uintptr_t)frame, payload, NET_DHCP_XID, net_mac_value(),
      expected_type);
    /* ZERO ADMITS. The object returns a reason code, so the natural
       `if (kotoba_...)` would accept every frame it just rejected. */
    if (reason == 0) { dhcp_frame_length = payload; return 1; }
    /* Which refusal to PRINT is a diagnostic choice and not a decision about
       admission: the codes ascend with how far into the frame the check
       reaches, so keeping the maximum names the candidate that got furthest.
       Without it an ARP frame refused at clause 1 could be the last thing seen
       and would hide a DHCP reply that failed deep. */
    if ((unsigned)reason > dhcp_reason) dhcp_reason = (unsigned)reason;
  }
  return 0;
}

static int net_dhcp_exchange(struct net_ring *rx, struct net_ring *tx,
                             uint8_t *rx_page, uint8_t *tx_page) {
  const uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  uint32_t offered, server;

  net_post(rx);
  if (!net_dhcp_send(tx, tx_page, NET_DHCP_DISCOVER, 0, 0)) {
    dhcp_stage = NET_DHCP_STAGE_TX_DISCOVER; return 0;
  }
  dhcp_stage = NET_DHCP_STAGE_OFFER;
  if (!net_dhcp_receive(rx, rx_page, NET_DHCP_OFFER)) return 0;

  /* Deriving from an ADMITTED frame, the same rule under which the peer's MAC
     is lifted out of an admitted ARP reply. `yiaddr` is at a constant offset so
     C reads it; the server identifier is not, so the object finds it. */
  offered = net_load_be32(frame + 58);
  server = (uint32_t)kotoba_aiueos_dhcp_option_u32(
    (uint64_t)(uintptr_t)frame, dhcp_frame_length, 54);

  net_post(rx);
  if (!net_dhcp_send(tx, tx_page, NET_DHCP_REQUEST, offered, server)) {
    dhcp_stage = NET_DHCP_STAGE_TX_REQUEST; return 0;
  }
  dhcp_stage = NET_DHCP_STAGE_ACK;
  if (!net_dhcp_receive(rx, rx_page, NET_DHCP_ACK)) return 0;

  dhcp_address = net_load_be32(frame + 58);
  dhcp_mask = (uint32_t)kotoba_aiueos_dhcp_option_u32(
    (uint64_t)(uintptr_t)frame, dhcp_frame_length, 1);
  dhcp_router = (uint32_t)kotoba_aiueos_dhcp_option_u32(
    (uint64_t)(uintptr_t)frame, dhcp_frame_length, 3);
  dhcp_server = (uint32_t)kotoba_aiueos_dhcp_option_u32(
    (uint64_t)(uintptr_t)frame, dhcp_frame_length, 54);
  dhcp_lease_seconds = (uint32_t)kotoba_aiueos_dhcp_option_u32(
    (uint64_t)(uintptr_t)frame, dhcp_frame_length, 51);
  /* Option 6 is DNS. Presence is not required by dhcp-reply-valid, so 0 here
     means absent-or-zero and the probe falls back to SLIRP's compiled-in
     nameserver the same way ARP falls back to NET_PEER_IP. */
  dhcp_dns = (uint32_t)kotoba_aiueos_dhcp_option_u32(
    (uint64_t)(uintptr_t)frame, dhcp_frame_length, 6);
  dhcp_stage = NET_DHCP_STAGE_DONE;
  return 1;
}

/* ------------------------------------------------------------------------- */
/* DNS stub + TCP:443 + TLS 1.3 + HTTP GET (ADR-0082). QNAME is compiled in. */
/* Source address is dhcp_address: that is what consuming the lease means.    */
/* AES-GCM record layer is mechanism (tls_aes_gcm.c). X25519 / SHA-256 /      */
/* digest_equal stay Kotoba. CID admission is Kotoba SHA-256 of the body.     */
/* ------------------------------------------------------------------------- */

#define NET_DNS_IP 0x0a000203U
#define NET_DNS_CLIENT_PORT 49154
#define NET_DNS_XID 0xa1e0
#define NET_DNS_STAGE_IDLE 0
#define NET_DNS_STAGE_TX 1
#define NET_DNS_STAGE_NO_ANSWER 2
#define NET_DNS_STAGE_DONE 3

#define NET_CLOUD_PORT 443
#define NET_CLOUD_LOCAL_PORT 49153
#define NET_CLOUD_ISN 0xa1e01000U

static const uint8_t net_dns_question[18] = {
  8,'k','o','t','o','b','a','s','e',3,'n','e','t',0,0,1,0,1
};

static const uint8_t http_empty_sha256[32] = {
  0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
  0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
};
static const char http_expected_cid[] =
  "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku";

static uint32_t net_dns_server(void) {
  return dhcp_dns ? dhcp_dns : NET_DNS_IP;
}

static uint32_t net_build_dns_query(uint8_t *frame, uint32_t src, uint32_t dst) {
  uint32_t udp_length = 8 + 12 + 18;
  uint32_t total = 20 + udp_length;
  for (uint32_t i = 0; i < 14 + total; i++) frame[i] = 0;
  for (unsigned i = 0; i < 6; i++) frame[i] = net_peer_mac[i];
  for (unsigned i = 0; i < 6; i++) frame[6 + i] = net_mac[i];
  net_store_be16(frame + 12, 0x0800);
  frame[14] = 0x45;
  net_store_be16(frame + 16, (uint16_t)total);
  net_store_be16(frame + 20, 0x4000);
  frame[22] = 64;
  frame[23] = 17;
  net_store_be32(frame + 26, src);
  net_store_be32(frame + 30, dst);
  net_store_be16(frame + 24,
    (uint16_t)kotoba_aiueos_ipv4_checksum((uint64_t)(uintptr_t)(frame + 14), 20));
  net_store_be16(frame + 34, NET_DNS_CLIENT_PORT);
  net_store_be16(frame + 36, 53);
  net_store_be16(frame + 38, (uint16_t)udp_length);
  net_store_be16(frame + 42, NET_DNS_XID);
  net_store_be16(frame + 44, 0x0100);                        /* RD */
  net_store_be16(frame + 46, 1);                             /* QDCOUNT */
  for (unsigned i = 0; i < 18; i++) frame[54 + i] = net_dns_question[i];
  net_store_be16(frame + 40, net_udp_checksum(frame, udp_length));
  return 14 + total;
}

static int net_udp_rx_checksum_ok(const uint8_t *frame, uint32_t udp_length) {
  net_store_be32(net_dhcp_scratch, net_load_be32(frame + 26));
  net_store_be32(net_dhcp_scratch + 4, net_load_be32(frame + 30));
  net_dhcp_scratch[8] = 0;
  net_dhcp_scratch[9] = 17;
  net_store_be16(net_dhcp_scratch + 10, (uint16_t)udp_length);
  for (uint32_t i = 0; i < udp_length; i++) net_dhcp_scratch[12 + i] = frame[34 + i];
  return (uint16_t)kotoba_aiueos_ipv4_checksum(
    (uint64_t)(uintptr_t)net_dhcp_scratch, 12 + udp_length) == 0;
}

/* Constant-offset A admission for one compiled QNAME. Two answer layouts:
   RFC 1035 pointer 0xc00c, or the question copied uncompressed. Anything with
   an options-style walk stays unwritten -- that would need a Kotoba object
   and a kotoba-native export that this pin does not list. */
static int net_dns_answer_ok(const uint8_t *frame, uint32_t length, uint32_t src,
                             uint32_t dst, uint32_t *out_a) {
  uint32_t total, udp_length;
  unsigned i;
  if (length < 88) return 0;
  if (net_load_be16(frame + 12) != 0x0800) return 0;
  if (frame[14] != 0x45 || frame[23] != 17) return 0;
  total = net_load_be16(frame + 16);
  if (total < 58 || 14 + total > length) return 0;
  if (kotoba_aiueos_ipv4_checksum((uint64_t)(uintptr_t)(frame + 14), 20) != 0)
    return 0;
  if (net_load_be32(frame + 26) != src) return 0;
  if (net_load_be32(frame + 30) != dst) return 0;
  if (net_load_be16(frame + 34) != 53) return 0;
  if (net_load_be16(frame + 36) != NET_DNS_CLIENT_PORT) return 0;
  udp_length = net_load_be16(frame + 38);
  if (udp_length < 38 || 20 + udp_length != total) return 0;
  if (net_load_be16(frame + 40) != 0 && !net_udp_rx_checksum_ok(frame, udp_length))
    return 0;
  if (net_load_be16(frame + 42) != NET_DNS_XID) return 0;
  if ((net_load_be16(frame + 44) & 0x8000) == 0) return 0;   /* QR */
  if ((net_load_be16(frame + 44) & 0x000f) != 0) return 0;   /* RCODE */
  if (net_load_be16(frame + 46) != 1) return 0;              /* QDCOUNT */
  if (net_load_be16(frame + 48) == 0) return 0;              /* ANCOUNT */
  for (i = 0; i < 18; i++) {
    if (frame[54 + i] != net_dns_question[i]) return 0;
  }
  if (frame[72] == 0xc0 && frame[73] == 0x0c) {
    if (net_load_be16(frame + 74) != 1) return 0;
    if (net_load_be16(frame + 76) != 1) return 0;
    if (net_load_be16(frame + 82) != 4) return 0;
    *out_a = net_load_be32(frame + 84);
    return *out_a != 0;
  }
  if (length < 104) return 0;
  for (i = 0; i < 18; i++) {
    if (frame[72 + i] != net_dns_question[i]) return 0;
  }
  if (net_load_be16(frame + 90) != 1) return 0;
  if (net_load_be16(frame + 92) != 1) return 0;
  if (net_load_be16(frame + 98) != 4) return 0;
  *out_a = net_load_be32(frame + 100);
  return *out_a != 0;
}

static int net_dns_probe(struct net_ring *rx, struct net_ring *tx,
                         uint8_t *rx_page, uint8_t *tx_page) {
  uint8_t *frame = tx_page + sizeof(struct virtio_net_hdr);
  const uint8_t *rx_frame = rx_page + sizeof(struct virtio_net_hdr);
  uint32_t src = dhcp_address;
  uint32_t dst = net_dns_server();
  uint32_t frame_length;
  if (!dhcp_ready || !src || !net_peer_mac_known) {
    dns_stage = NET_DNS_STAGE_IDLE;
    return 0;
  }
  net_post(rx);
  for (unsigned i = 0; i < sizeof(struct virtio_net_hdr); i++) tx_page[i] = 0;
  frame_length = net_build_dns_query(frame, src, dst);
  tx->desc[0].length = (uint32_t)sizeof(struct virtio_net_hdr) + frame_length;
  net_post(tx);
  dns_stage = NET_DNS_STAGE_TX;
  if (!net_await(tx->used, tx->posted) || tx->used->ring[0].id != 0) return 0;
  dhcp_consumed = 1;
  dns_stage = NET_DNS_STAGE_NO_ANSWER;
  for (unsigned attempt = 0; attempt < 4; attempt++) {
    uint32_t received, payload, a;
    if (attempt) net_post(rx);
    if (!net_await(rx->used, rx->posted)) return 0;
    received = rx->used->ring[0].length;
    if (rx->used->ring[0].id != 0 ||
        received <= sizeof(struct virtio_net_hdr) ||
        received > sizeof(struct virtio_net_hdr) + NET_FRAME_MAX) continue;
    payload = received - (uint32_t)sizeof(struct virtio_net_hdr);
    if (net_dns_answer_ok(rx_frame, payload, dst, src, &a)) {
      dns_a = a;
      dns_stage = NET_DNS_STAGE_DONE;
      return 1;
    }
  }
  return 0;
}

static int net_tcp_data(const uint8_t *frame, const uint8_t **payload,
                           uint32_t *plen) {
  uint32_t total = net_load_be16(frame + 16);
  uint32_t header = 4U * (uint32_t)(frame[46] >> 4);
  if (header < 20 || 20 + header > total) return 0;
  *payload = frame + 34 + header;
  *plen = total - 20 - header;
  return 1;
}

static int net_http_cid_admit(const uint8_t *app, uint32_t n) {
  uint32_t i, body = 0;
  uint8_t digest[32];
  /* Mechanism: find the compiled status line and header terminator. */
  if (n < 12) return 0;
  if (app[0] != 'H' || app[1] != 'T' || app[2] != 'T' || app[3] != 'P') return 0;
  if (app[5] != '1' || app[9] != '2' || app[10] != '0' || app[11] != '0') return 0;
  for (i = 0; i + 3 < n; i++) {
    if (app[i] == '\r' && app[i + 1] == '\n' &&
        app[i + 2] == '\r' && app[i + 3] == '\n') {
      body = i + 4;
      break;
    }
  }
  if (body == 0) return 0;
  {
    static const uint8_t empty[1] = {0};
    uint32_t body_len = n - body;
    const uint8_t *p = (body_len == 0) ? empty : (app + body);
    if (!sha256(p, body_len, digest)) return 0;
  }
  if (!kotoba_aiueos_digest_equal(digest, http_empty_sha256, 32)) return 0;
  http_cid_text = http_expected_cid;
  http_cid_ready = 1;
  return 1;
}

static int net_tcp_cloud_seg_ok(const uint8_t *frame, uint32_t ip_len,
                                uint32_t dst, uint32_t ack_hi, uint32_t ack_lo) {
  uint32_t ack = ack_hi;
  for (;;) {
    if (kotoba_aiueos_tcp_segment_valid(
          (uint64_t)(uintptr_t)frame, ip_len, dst, ack, NET_TCP_PSH | NET_TCP_ACK) ||
        kotoba_aiueos_tcp_segment_valid(
          (uint64_t)(uintptr_t)frame, ip_len, dst, ack, NET_TCP_ACK) ||
        kotoba_aiueos_tcp_segment_valid(
          (uint64_t)(uintptr_t)frame, ip_len, dst, ack, NET_TCP_FIN | NET_TCP_ACK))
      return 1;
    if (ack == ack_lo) return 0;
    ack = ack_lo;
  }
}

/* One receive buffer: ACK each segment before the peer sends the next.
   Post the next RX before the ACK so the reply has somewhere to land.
   ack_lo is the sequence the peer may still be ACKing (ClientHello) while
   ack_hi is our_next after ClientFinished+GET. */
static int net_tcp_cloud_pump(struct net_ring *rx, struct net_ring *tx,
                              uint8_t *rx_page, uint8_t *tx_page,
                              uint32_t src, uint32_t dst,
                              uint32_t *our_next, uint32_t *peer_next,
                              uint32_t ack_lo,
                              unsigned attempts, int want_http) {
  const uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  unsigned attempt;
  for (attempt = 0; attempt < attempts; attempt++) {
    const uint8_t *payload;
    uint32_t plen = 0, received, ip_len;
    if (!net_await(rx->used, rx->posted)) return 0;
    received = rx->used->ring[0].length;
    if (rx->used->ring[0].id != 0 ||
        received <= sizeof(struct virtio_net_hdr) ||
        received > sizeof(struct virtio_net_hdr) + NET_FRAME_MAX) {
      net_post(rx);
      continue;
    }
    ip_len = received - (uint32_t)sizeof(struct virtio_net_hdr);
    if (!net_tcp_cloud_seg_ok(frame, ip_len, dst, *our_next, ack_lo)) {
      net_post(rx);
      continue;
    }
    if (!net_tcp_data(frame, &payload, &plen)) {
      net_post(rx);
      continue;
    }
    if (plen) {
      if (!aiueos_tls13_feed(payload, plen)) {
        if (!want_http) return 0;
        net_post(rx);
        continue;
      }
      *peer_next += plen;
      if (aiueos_tls13_saw_record()) {
        tls_record_ready = 1;
        tls_record_type = aiueos_tls13_first_record_type();
      }
    }
    if (!want_http && aiueos_tls13_handshake_ready()) {
      tls_handshake_ready = 1;
      net_post(rx);
      if (!net_tcp_send(tx, tx_page, src, dst, NET_CLOUD_LOCAL_PORT, NET_CLOUD_PORT,
                        *our_next, *peer_next, NET_TCP_ACK, 0, 0))
        return 0;
      return 1;
    }
    if (want_http && net_http_cid_admit(aiueos_tls13_app(), aiueos_tls13_app_len()))
      return 1;
    net_post(rx);
    if (!net_tcp_send(tx, tx_page, src, dst, NET_CLOUD_LOCAL_PORT, NET_CLOUD_PORT,
                      *our_next, *peer_next, NET_TCP_ACK, 0, 0))
      return 0;
  }
  if (!want_http && aiueos_tls13_handshake_ready()) {
    tls_handshake_ready = 1;
    return 1;
  }
  return want_http ? http_cid_ready : tls_handshake_ready;
}

static int net_tcp_cloud_probe(struct net_ring *rx, struct net_ring *tx,
                               uint8_t *rx_page, uint8_t *tx_page) {
  const uint8_t *frame = rx_page + sizeof(struct virtio_net_hdr);
  uint32_t src = dhcp_address;
  uint32_t dst = dns_a;
  uint32_t peer_next, our_next;
  uint8_t ch[160], flight[512];
  uint32_t ch_len = 0;
  int got_synack = 0;
  unsigned syn;
  if (!dns_ready || !src || !dst || !net_peer_mac_known) {
    tcp_cloud_stage = NET_TCP_STAGE_IDLE;
    return 0;
  }
  net_tx_window = NET_CLOUD_WINDOW;
  aiueos_tls13_reset();
  tcp_cloud_stage = NET_TCP_STAGE_SYN_ACK;
  for (syn = 0; syn < 2 && !got_synack; syn++) {
    net_post(rx);
    if (!net_tcp_send(tx, tx_page, src, dst, NET_CLOUD_LOCAL_PORT, NET_CLOUD_PORT,
                      NET_CLOUD_ISN, 0, NET_TCP_SYN, 0, 0)) {
      net_tx_window = NET_TCP_WINDOW;
      return 0;
    }
    got_synack = net_tcp_receive(rx, rx_page, dst, NET_CLOUD_ISN + 1,
                                 NET_TCP_SYN | NET_TCP_ACK, 4);
  }
  if (!got_synack) {
    net_tx_window = NET_TCP_WINDOW;
    return 0;
  }
  peer_next = net_load_be32(frame + 38) + 1;
  tcp_cloud_ready = 1;
  if (!aiueos_tls13_clienthello(ch, &ch_len)) {
    net_tx_window = NET_TCP_WINDOW;
    return 1;
  }
  our_next = NET_CLOUD_ISN + 1 + ch_len;
  net_post(rx);
  /* Handshake ACK carries ClientHello so the peer's next flight is one
     segment, not ACK-then-record on a one-buffer queue. */
  if (!net_tcp_send(tx, tx_page, src, dst, NET_CLOUD_LOCAL_PORT, NET_CLOUD_PORT,
                    NET_CLOUD_ISN + 1, peer_next,
                    NET_TCP_PSH | NET_TCP_ACK, ch, ch_len)) {
    net_tx_window = NET_TCP_WINDOW;
    return 1;
  }
  tcp_cloud_stage = NET_TCP_STAGE_ECHO;
  if (!net_tcp_cloud_pump(rx, tx, rx_page, tx_page, src, dst,
                          &our_next, &peer_next, our_next, 24, 0)) {
    net_tx_window = NET_TCP_WINDOW;
    tcp_cloud_stage = NET_TCP_STAGE_DONE;
    return 1;
  }
  /* Handshake pump consumed the ServerHello flight but did not ACK; this
     segment is the ACK plus ClientFinished+GET. Post RX first. */
  {
    uint32_t fin_len = 0, get_len = 0, ack_lo = our_next;
    if (!aiueos_tls13_run_certverify() ||
        !aiueos_tls13_take_finished(flight, &fin_len)) {
      net_tx_window = NET_TCP_WINDOW;
      tcp_cloud_stage = NET_TCP_STAGE_DONE;
      return 1;
    }
    tls_finished_sent = 1;
    if (!aiueos_tls13_take_http(flight + fin_len, &get_len)) {
      net_tx_window = NET_TCP_WINDOW;
      tcp_cloud_stage = NET_TCP_STAGE_DONE;
      return 1;
    }
    net_post(rx);
    if (!net_tcp_send(tx, tx_page, src, dst, NET_CLOUD_LOCAL_PORT, NET_CLOUD_PORT,
                      our_next, peer_next,
                      NET_TCP_PSH | NET_TCP_ACK, flight, fin_len + get_len)) {
      net_tx_window = NET_TCP_WINDOW;
      tcp_cloud_stage = NET_TCP_STAGE_DONE;
      return 1;
    }
    tls_http_sent = 1;
    our_next += fin_len + get_len;
    net_tcp_cloud_pump(rx, tx, rx_page, tx_page, src, dst,
                       &our_next, &peer_next, ack_lo, 96, 1);
  }
  tcp_cloud_stage = NET_TCP_STAGE_DONE;
  net_tx_window = NET_TCP_WINDOW;
  return 1;
}

static int virtio_net(uint8_t b, uint8_t d, uint8_t f) {
  struct virtio_caps caps;
  volatile struct virtio_common_cfg *cfg;
  uint64_t notify_base;
  if (!find_virtio_caps(b,d,f,&caps) || !map_transport(b,d,f,&caps,&cfg,&notify_base) ||
      !negotiate(cfg)) return 0;
  if (cfg->num_queues < 2) return 0;
  struct virtq_desc *rx_desc = aiueos_allocate_physical_page();
  struct virtq_avail *rx_avail = aiueos_allocate_physical_page();
  struct virtq_used *rx_used = aiueos_allocate_physical_page();
  struct virtq_desc *tx_desc = aiueos_allocate_physical_page();
  struct virtq_avail *tx_avail = aiueos_allocate_physical_page();
  struct virtq_used *tx_used = aiueos_allocate_physical_page();
  uint8_t *rx_page = aiueos_allocate_physical_page();
  uint8_t *tx_page = aiueos_allocate_physical_page();
  if (!rx_desc || !rx_avail || !rx_used || !tx_desc || !tx_avail || !tx_used ||
      !rx_page || !tx_page) return 0;

  /* The receive buffer is posted BEFORE the device is told it may run, so a
     reply cannot arrive with no buffer to land in. */
  rx_desc[0].address = (uint64_t)(uintptr_t)rx_page;
  rx_desc[0].length = sizeof(struct virtio_net_hdr) + NET_FRAME_MAX;
  rx_desc[0].flags = VIRTQ_DESC_F_WRITE; rx_desc[0].next = 0;
  rx_avail->ring[0] = 0; __asm__ volatile("" ::: "memory"); rx_avail->index = 1;
  volatile uint16_t *rx_doorbell =
    prepare_queue_index(cfg,&caps,notify_base,0,1,rx_desc,rx_avail,rx_used);
  if (!rx_doorbell) return 0;

  uint32_t frame_length = net_build_arp_request(tx_page + sizeof(struct virtio_net_hdr));
  for (unsigned i = 0; i < sizeof(struct virtio_net_hdr); i++) tx_page[i] = 0;
  tx_desc[0].address = (uint64_t)(uintptr_t)tx_page;
  tx_desc[0].length = sizeof(struct virtio_net_hdr) + frame_length;
  tx_desc[0].flags = 0; tx_desc[0].next = 0;
  tx_avail->ring[0] = 0; __asm__ volatile("" ::: "memory"); tx_avail->index = 1;
  volatile uint16_t *tx_doorbell =
    prepare_queue_index(cfg,&caps,notify_base,1,1,tx_desc,tx_avail,tx_used);
  if (!tx_doorbell) return 0;

  /* Both rings were already filled and their avail index set to 1 above, so
     they start life one buffer in. */
  struct net_ring rx = {rx_desc, rx_avail, rx_used, rx_doorbell, 0, 1};
  struct net_ring tx = {tx_desc, tx_avail, tx_used, tx_doorbell, 1, 1};

  cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
  *rx.doorbell = rx.queue;
  *tx.doorbell = tx.queue;

  if (!net_await(tx.used, 1) || !net_await(rx.used, 1)) return 0;
  if (tx_used->ring[0].id != 0) return 0;

  uint32_t received = rx_used->ring[0].length;
  if (rx_used->ring[0].id != 0 ||
      received <= sizeof(struct virtio_net_hdr) ||
      received > sizeof(struct virtio_net_hdr) + NET_FRAME_MAX) return 0;
  uint32_t payload = received - (uint32_t)sizeof(struct virtio_net_hdr);
  if (!kotoba_aiueos_net_arp_reply_valid(
        (uint64_t)(uintptr_t)(rx_page + sizeof(struct virtio_net_hdr)),
        payload, NET_PEER_IP)) return 0;
  net_rx_length = payload;
  net_ready = 1;

  /* Bytes 22..27 of an ARP packet are the sender's hardware address. They are
     read here and not earlier because only an ADMITTED reply is worth caching:
     an unvalidated frame would put a sender-chosen address in the one slot that
     every unicast frame this OS sends is addressed to. */
  const uint8_t *arp = rx_page + sizeof(struct virtio_net_hdr);
  for (unsigned i = 0; i < 6; i++) net_peer_mac[i] = arp[22 + i];
  net_peer_mac_known = 1;

  ipv4_ready = net_ipv4_echo(&rx, &tx, rx_page, tx_page);
  /* TCP rides on IPv4 the way IPv4 rides on the link layer, so it is attempted
     only where the layer below produced evidence. It also matters mechanically:
     a failed echo can return with a receive buffer still outstanding, and the
     strict post-one-consume-one alternation below assumes none is. */
  if (ipv4_ready) tcp_ready = net_tcp_probe(&rx, &tx, rx_page, tx_page);
  /* DHCP needs only the link layer -- it is broadcast, so it uses neither the
     ARP cache nor anything ICMP proved -- and in a real boot it would come
     first, before anything has an address to send from. It is run LAST anyway,
     so that a failure here cannot retract the evidence three exchanges above it
     have already earned. The cost is the other direction: a failure in those
     exchanges can return with a receive buffer still outstanding, and this one
     then fails in whatever state it was left, which is reported as its own
     stage rather than as a DHCP defect. */
  if (net_ready) dhcp_ready = net_dhcp_exchange(&rx, &tx, rx_page, tx_page);
  /* Lease consumption and the cloud path sit after DHCP so a failed lease
     cannot pretend to have been used, and so ARP/ICMP/guestfwd-TCP stay on
     compiled-in 10.0.2.15 for the four-boot DHCP tamper gate. */
  if (dhcp_ready) dns_ready = net_dns_probe(&rx, &tx, rx_page, tx_page);
  if (dns_ready) net_tcp_cloud_probe(&rx, &tx, rx_page, tx_page);
#ifdef AIUEOS_SSH_LISTEN
  /* Passive open runs last, after every client probe has earned its evidence,
     so a peer that never connects cannot retract what the chain above proved.
     This is the OS's first post-evidence service step -- it accepts an inbound
     connection instead of opening one. */
  net_ssh_listen(&rx, &tx, rx_page, tx_page);
#endif
  return 1;
}

int aiueos_pci_enumerate(void) {
  net_ready = 0;
  net_rx_length = 0;
  net_peer_mac_known = 0;
  ipv4_ready = 0;
  tcp_ready = 0;
  tcp_stage = NET_TCP_STAGE_IDLE;
  dhcp_ready = 0;
  dhcp_stage = NET_DHCP_STAGE_IDLE;
  dhcp_reason = 0;
  dhcp_address = 0; dhcp_mask = 0; dhcp_router = 0; dhcp_server = 0;
  dhcp_lease_seconds = 0;
  dhcp_dns = 0;
  dhcp_consumed = 0;
  dhcp_frame_length = 0;
  dns_ready = 0;
  dns_stage = NET_DNS_STAGE_IDLE;
  dns_a = 0;
  tcp_cloud_ready = 0;
  tcp_cloud_stage = NET_TCP_STAGE_IDLE;
  tls_record_ready = 0;
  tls_record_type = 0;
  tls_handshake_ready = 0;
  tls_finished_sent = 0;
  tls_http_sent = 0;
  http_cid_ready = 0;
  http_cid_text = 0;
  object_store_ready = 0;
  kotoba_app_count=0; for(unsigned app=0;app<KOTOBA_APP_CAPACITY;app++)kotoba_apps[app].ready=0;
  journal_ready = 0;
  journal_recovered = 0;
  journal_sequence = 0;
  journal_recovered_sequence = 0;
  journal_slot = 0;
  object_transaction_replayed = 0;
  object_transaction_sequence = 0;
  service_registry_ready = 0;
  service_registry_replayed = 0;
  recovered_service_registry_ready=0;
  recovered_service_registry_states[0]=recovered_service_registry_states[1]=0;
  persisted_service_registry_states[0]=persisted_service_registry_states[1]=0;
  blk_backend=(struct aiueos_blk_backend){0};
  user_object_sequence[0]=user_object_sequence[1]=0;
  user_object_slot[0]=user_object_slot[1]=0;
  user_object_value[0]=user_object_value[1]=0;
  user_object_ready=user_object_write_evidence=user_object_replay_evidence=0;
  user_object_pending[0]=user_object_pending[1]=0;
  gpu_scanout_width = gpu_scanout_height = 0;
  gpu_enabled_scanouts = 0;
  gpu_2d_create_ok = gpu_2d_flush_ok = gpu_2d_two_ok = gpu_2d_scanout_two_ok = 0;
  if (!aiueos_dma_test_policy_allows_unisolated()) return 0;
  if (!cap_selftest()) return 0;
  uint32_t present = 0, virtio = 0;
  int rng_ok = 0, blk_ok = 0, input_ok = 0, gpu_ok = 0;
  desktop_input_ready = 0;
  desktop_input_from_eventq = 0;
  desktop_input_eventq_empty = 0;
  for (uint16_t bus = 0; bus < 256; bus++) for (uint8_t dev = 0; dev < 32; dev++) {
    uint32_t id0 = config_read((uint8_t)bus,dev,0,0);
    if ((id0 & 0xffffU) == 0xffffU) continue;
    uint8_t functions = (config8((uint8_t)bus,dev,0,0x0e) & 0x80) ? 8 : 1;
    for (uint8_t fn = 0; fn < functions; fn++) {
      uint32_t id = config_read((uint8_t)bus,dev,fn,0);
      if ((id & 0xffffU) == 0xffffU) continue; present++;
      if ((id & 0xffffU) == VIRTIO_VENDOR_ID) {
        virtio++;
        uint16_t device_id = (uint16_t)(id >> 16);
        if ((device_id == VIRTIO_RNG_MODERN_ID || device_id == VIRTIO_RNG_TRANSITIONAL_ID) &&
            virtio_rng((uint8_t)bus,dev,fn)) rng_ok = 1;
        if ((device_id == VIRTIO_BLK_MODERN_ID || device_id == VIRTIO_BLK_TRANSITIONAL_ID) &&
            virtio_blk((uint8_t)bus,dev,fn)) blk_ok = 1;
        if ((device_id == VIRTIO_INPUT_MODERN_ID || device_id == VIRTIO_INPUT_TRANSITIONAL_ID) &&
            virtio_input((uint8_t)bus,dev,fn)) input_ok = 1;
        if ((device_id == VIRTIO_GPU_MODERN_ID || device_id == VIRTIO_GPU_TRANSITIONAL_ID) &&
            virtio_gpu((uint8_t)bus,dev,fn)) gpu_ok = 1;
        /* Reported through `aiueos_virtio_net_ready` rather than the return
           cascade below, which main.c reads bit by bit: a NIC is optional, and
           folding it into that cascade would make every existing gate that
           boots without one start failing. */
        if (device_id == VIRTIO_NET_MODERN_ID || device_id == VIRTIO_NET_TRANSITIONAL_ID)
          virtio_net((uint8_t)bus,dev,fn);
      }
    }
  }
  if (rng_ok && blk_ok && input_ok && gpu_ok) return 15;
  if (rng_ok && blk_ok && input_ok) return 7;
  if (rng_ok && blk_ok) return 3;
  if (rng_ok) return 2;
  return present && virtio ? 1 : 0;
}
