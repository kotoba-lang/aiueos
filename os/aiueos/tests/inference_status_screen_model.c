#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/inference_status.h"

#define WIDTH 800U
#define HEIGHT 600U
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"check failed: %s\n",#x); return 1; } } while(0)

struct aiueos_boot_info {
  uint64_t magic, version;
  void *memory_map; uint64_t memory_map_size, descriptor_size, descriptor_version;
  void *acpi_rsdp;
  uint64_t framebuffer_base, framebuffer_size;
  uint32_t framebuffer_width, framebuffer_height, framebuffer_stride, framebuffer_format;
};
struct aiueos_desktop_surface {
  uint32_t abi_version, byte_size;
  uint64_t surface_id, generation, content_hash;
  uint32_t width, height, stride, pixel_format;
  uint32_t damage_x, damage_y, damage_width, damage_height;
} __attribute__((packed));

static uint32_t pixels[WIDTH * HEIGHT];
int aiueos_map_framebuffer(uint64_t address, uint64_t length) {
  return address == (uint64_t)(uintptr_t)pixels && length == sizeof(pixels);
}
int aiueos_framebuffer_initialize(const struct aiueos_boot_info *boot);
const struct aiueos_desktop_surface *aiueos_desktop_surface(void);

static int write_ppm(const char *path) {
  FILE *output = fopen(path, "wb");
  if (!output) return 0;
  fprintf(output, "P6\n%u %u\n255\n", WIDTH, HEIGHT);
  for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) {
    uint8_t rgb[3] = {(uint8_t)(pixels[i] >> 16),
                      (uint8_t)(pixels[i] >> 8), (uint8_t)pixels[i]};
    if (fwrite(rgb, 1, sizeof(rgb), output) != sizeof(rgb)) {
      fclose(output); return 0;
    }
  }
  return fclose(output) == 0;
}

int main(int argc, char **argv) {
  struct aiueos_boot_info boot = {
    .framebuffer_base = (uint64_t)(uintptr_t)pixels,
    .framebuffer_size = sizeof(pixels),
    .framebuffer_width = WIDTH, .framebuffer_height = HEIGHT,
    .framebuffer_stride = WIDTH, .framebuffer_format = 0
  };
  CHECK(aiueos_framebuffer_initialize(&boot));
  const struct aiueos_desktop_surface *surface = aiueos_desktop_surface();
  CHECK(surface && surface->generation == 1 && surface->content_hash);
  uint64_t initial_hash = surface->content_hash;

  struct aiueos_inference_status blocked = {
    AIUEOS_INFERENCE_STATUS_ABI_VERSION, sizeof(blocked),
    AIUEOS_INFERENCE_BLOCKED, "QWEN3.8 27B", "UD IQ3 XXS",
    "RUNTIME NOT PRESENT", 0, 0, 64, 10934860704ULL,
    AIUEOS_INFERENCE_UNMEASURED, AIUEOS_INFERENCE_UNMEASURED,
    AIUEOS_INFERENCE_UNMEASURED, AIUEOS_INFERENCE_UNMEASURED,
    AIUEOS_INFERENCE_UNMEASURED, 0
  };
  CHECK(aiueos_inference_status_valid(&blocked));
  CHECK(aiueos_framebuffer_inference_screen(&blocked));
  CHECK(surface->generation == 2 && surface->content_hash != initial_hash);

  struct aiueos_inference_status measured = {
    AIUEOS_INFERENCE_STATUS_ABI_VERSION, sizeof(measured),
    AIUEOS_INFERENCE_COMPLETE, "QWEN3.8 27B", "UD IQ3 XXS",
    "QEMU UI TEST", 128, 32, 32, 10934860704ULL, 13958643712ULL,
    53000000000ULL, 3200000000ULL, 8000000000ULL, 3450000000ULL, 0
  };
  CHECK(aiueos_inference_status_valid(&measured));
  CHECK(aiueos_inference_milli_tokens_per_second(128, 3200000000ULL) == 40000);
  CHECK(aiueos_inference_milli_tokens_per_second(32, 8000000000ULL) == 4000);
  CHECK(aiueos_framebuffer_inference_screen(&measured));
  CHECK(surface->generation == 3 && surface->damage_width == WIDTH &&
        surface->damage_height == HEIGHT);

  struct aiueos_inference_status invalid = measured;
  invalid.decode_ns = AIUEOS_INFERENCE_UNMEASURED;
  CHECK(!aiueos_inference_status_valid(&invalid));
  CHECK(argc < 2 || write_ppm(argv[1]));
  puts("AIUEOS_INFERENCE_STATUS_SCREEN_OK model=QWEN3.8-27B metrics=load,prefill,decode,tokens,resident phase=complete evidence=qemu-ui-test-only");
  return 0;
}
