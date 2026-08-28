#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_VARIABLE_NON_VOLATILE 1U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 2U
#define EFI_VARIABLE_RUNTIME_ACCESS 4U

typedef uint64_t efi_status;
typedef uint16_t char16;

struct efi_guid { uint32_t a; uint16_t b, c; uint8_t d[8]; };
struct efi_table_header {
  uint64_t signature;
  uint32_t revision, header_size, crc32, reserved;
};
struct efi_time {
  uint16_t year;
  uint8_t month, day, hour, minute, second, pad1;
  uint32_t nanosecond;
  int16_t timezone;
  uint8_t daylight, pad2;
};
typedef efi_status(EFIAPI *efi_get_time)(struct efi_time *, void *);
typedef efi_status(EFIAPI *efi_set_variable)(const char16 *, const struct efi_guid *,
                                             uint32_t, uint64_t, void *);
typedef void(EFIAPI *efi_reset_system)(uint32_t, efi_status, uint64_t, void *);
struct efi_runtime_services {
  struct efi_table_header header;
  efi_get_time get_time;
  void *set_time, *get_wakeup_time, *set_wakeup_time;
  void *set_virtual_address_map, *convert_pointer;
  void *get_variable, *get_next_variable_name;
  efi_set_variable set_variable;
  void *get_next_high_monotonic_count;
  efi_reset_system reset_system;
};

struct aiueos_qualification_record {
  uint32_t magic;
  uint16_t version, state;
  uint32_t code, reserved;
};

static const struct efi_guid qualification_guid =
  {0x73953a72,0x6627,0x4b62,{0x9a,0x9c,0x10,0x38,0xd9,0x20,0x9a,0x16}};
static const char16 qualification_name[] = u"AIUEOSQualificationResult";
static struct efi_runtime_services *qualification_runtime;
static uint64_t qualification_firmware_cr3;

void aiueos_qualification_runtime_initialize(void *runtime_services,
                                              uint64_t firmware_cr3) {
  qualification_runtime = runtime_services;
  qualification_firmware_cr3 = firmware_cr3;
}

static uint64_t read_cr3(void) {
  uint64_t value;
  __asm__ volatile("mov %%cr3, %0" : "=r"(value));
  return value;
}

static void write_cr3(uint64_t value) {
  __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

/* Keep the last entered physical-qualification stage across a hang or manual
   power cycle. This has the same 16-byte NVRAM-only boundary as finalize, but
   deliberately does not reset the machine or touch a block device. */
int aiueos_qualification_progress(uint32_t code) {
  struct efi_runtime_services *runtime = qualification_runtime;
  if (!runtime || !qualification_firmware_cr3 || !runtime->set_variable) return 0;
  struct aiueos_qualification_record record = {
    0x514b3241U, 2, 0, code, 0
  };
  uint64_t kernel_cr3 = read_cr3();
  __asm__ volatile("cli");
  write_cr3(qualification_firmware_cr3);
  efi_status status = runtime->set_variable(
      qualification_name, &qualification_guid,
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
        EFI_VARIABLE_RUNTIME_ACCESS,
      sizeof(record), &record);
  write_cr3(kernel_cr3);
  return status == EFI_SUCCESS;
}

/* Persist only a bounded result code in firmware NVRAM, then warm-reset into
   the one-shot BootNext entry installed by the USB probe.  The probe, still
   under UEFI Boot Services, is the only component that writes RESULT.LOG to
   the removable medium.  Neither this function nor the qualification kernel
   reaches a block driver or an internal-disk path. */
int aiueos_qualification_finalize(uint16_t state, uint32_t code) {
  struct efi_runtime_services *runtime = qualification_runtime;
  if (!runtime || !qualification_firmware_cr3 || !runtime->set_variable ||
      !runtime->reset_system) return 0;

  struct aiueos_qualification_record record = {
    0x514b3241U, 2, state, code, 0
  };
  uint64_t kernel_cr3 = read_cr3();
  __asm__ volatile("cli");
  write_cr3(qualification_firmware_cr3);
  efi_status status = runtime->set_variable(
      qualification_name, &qualification_guid,
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
        EFI_VARIABLE_RUNTIME_ACCESS,
      sizeof(record), &record);
  if (status != EFI_SUCCESS) {
    write_cr3(kernel_cr3);
    return 0;
  }
  runtime->reset_system(1, EFI_SUCCESS, 0, 0);
  write_cr3(kernel_cr3);
  return 0;
}
