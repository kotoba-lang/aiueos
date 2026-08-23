#include <stdint.h>

struct aiueos_boot_info {
  uint64_t magic, version;
  void *memory_map; uint64_t memory_map_size, descriptor_size, descriptor_version;
  void *acpi_rsdp;
  uint64_t framebuffer_base, framebuffer_size;
  uint32_t framebuffer_width, framebuffer_height, framebuffer_stride, framebuffer_format;
};

extern int aiueos_map_framebuffer(uint64_t address, uint64_t length);

/* Browser desktop output ABI. surface_id is an opaque kernel-owned handle;
   physical framebuffer addresses are intentionally absent from the contract. */
struct aiueos_desktop_surface {
  uint32_t abi_version, byte_size;
  uint64_t surface_id, generation, content_hash;
  uint32_t width, height, stride, pixel_format;
  uint32_t damage_x, damage_y, damage_width, damage_height;
} __attribute__((packed));
static struct aiueos_desktop_surface desktop_surface;
static int desktop_surface_ready;
static volatile uint32_t *desktop_surface_pixels;
int aiueos_desktop_surface_ready(void) { return desktop_surface_ready; }
const struct aiueos_desktop_surface *aiueos_desktop_surface(void) {
  return desktop_surface_ready ? &desktop_surface : 0;
}
int aiueos_desktop_surface_copy(uint64_t generation, uint32_t x, uint32_t y,
    uint32_t width, uint32_t height, uint32_t *destination, uint64_t capacity) {
  if (!desktop_surface_ready || !destination || generation != desktop_surface.generation ||
      !width || !height || x >= desktop_surface.width || y >= desktop_surface.height ||
      width > desktop_surface.width - x || height > desktop_surface.height - y ||
      (uint64_t)width * height > capacity / sizeof(uint32_t)) return 0;
  for (uint32_t row = 0; row < height; row++)
    for (uint32_t column = 0; column < width; column++)
      destination[(uint64_t)row * width + column] =
        desktop_surface_pixels[(uint64_t)(y + row) * desktop_surface.stride + x + column];
  return 1;
}
int aiueos_desktop_surface_bind_scanout(uint32_t width, uint32_t height) {
  return desktop_surface_ready && desktop_surface.width == width && desktop_surface.height == height;
}

static uint32_t pixel(uint32_t rgb, uint32_t format);
static void rectangle(volatile uint32_t *fb, uint32_t stride, uint32_t format,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color);
static uint64_t sample_hash(volatile uint32_t *fb, uint32_t width,
                            uint32_t height, uint32_t stride);

/* Boot-desktop WM rects (ADR-0091 hit geometry). C fills and samples;
   Kotoba names which id is front. RGB survives format-0 vs BGR swap as
   distinct stored values. */
#define AIUEOS_WM_WIN1_X 32U
#define AIUEOS_WM_WIN1_Y 32U
#define AIUEOS_WM_WIN1_W 720U
#define AIUEOS_WM_WIN1_H 540U
#define AIUEOS_WM_WIN1_RGB 0x00aa2222U
#define AIUEOS_WM_WIN2_X 96U
#define AIUEOS_WM_WIN2_Y 72U
#define AIUEOS_WM_WIN2_W 640U
#define AIUEOS_WM_WIN2_H 480U
#define AIUEOS_WM_WIN2_RGB 0x0022aa55U

static int wm_rect_fits(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
  return desktop_surface_ready && w && h &&
    x < desktop_surface.width && y < desktop_surface.height &&
    w <= desktop_surface.width - x && h <= desktop_surface.height - y;
}

int aiueos_desktop_wm_rects_fit(void) {
  return wm_rect_fits(AIUEOS_WM_WIN1_X, AIUEOS_WM_WIN1_Y,
                      AIUEOS_WM_WIN1_W, AIUEOS_WM_WIN1_H) &&
    wm_rect_fits(AIUEOS_WM_WIN2_X, AIUEOS_WM_WIN2_Y,
                 AIUEOS_WM_WIN2_W, AIUEOS_WM_WIN2_H);
}

uint32_t aiueos_desktop_wm_stored_color(uint64_t window_id) {
  uint32_t rgb = 0;
  if (window_id == 1) rgb = AIUEOS_WM_WIN1_RGB;
  else if (window_id == 2) rgb = AIUEOS_WM_WIN2_RGB;
  else return 0;
  if (!desktop_surface_ready) return 0;
  return pixel(rgb, desktop_surface.pixel_format);
}

static void paint_wm_window(uint64_t window_id) {
  if (window_id == 1)
    rectangle(desktop_surface_pixels, desktop_surface.stride,
              desktop_surface.pixel_format, AIUEOS_WM_WIN1_X, AIUEOS_WM_WIN1_Y,
              AIUEOS_WM_WIN1_W, AIUEOS_WM_WIN1_H, AIUEOS_WM_WIN1_RGB);
  else if (window_id == 2)
    rectangle(desktop_surface_pixels, desktop_surface.stride,
              desktop_surface.pixel_format, AIUEOS_WM_WIN2_X, AIUEOS_WM_WIN2_Y,
              AIUEOS_WM_WIN2_W, AIUEOS_WM_WIN2_H, AIUEOS_WM_WIN2_RGB);
}

int aiueos_desktop_wm_paint(uint64_t front) {
  uint64_t back;
  if (!aiueos_desktop_wm_rects_fit()) return 0;
  if (front != 1 && front != 2) return 0;
  back = (front == 2) ? 1 : 2;
  paint_wm_window(back);
  paint_wm_window(front);
  desktop_surface.generation += 1;
  desktop_surface.content_hash =
    sample_hash(desktop_surface_pixels, desktop_surface.width,
                desktop_surface.height, desktop_surface.stride);
  desktop_surface.damage_x = AIUEOS_WM_WIN1_X;
  desktop_surface.damage_y = AIUEOS_WM_WIN1_Y;
  desktop_surface.damage_width = AIUEOS_WM_WIN1_W;
  desktop_surface.damage_height = AIUEOS_WM_WIN1_H;
  return 1;
}

uint32_t aiueos_desktop_sample_pixel(uint32_t x, uint32_t y) {
  if (!desktop_surface_ready || x >= desktop_surface.width ||
      y >= desktop_surface.height)
    return 0;
  return desktop_surface_pixels[(uint64_t)y * desktop_surface.stride + x];
}

static uint32_t pixel(uint32_t rgb, uint32_t format) {
  if (format == 0) return rgb;
  return ((rgb & 0xffU) << 16) | (rgb & 0xff00U) | ((rgb >> 16) & 0xffU);
}

static void rectangle(volatile uint32_t *fb, uint32_t stride, uint32_t format,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color) {
  color = pixel(color, format);
  for (uint32_t row = y; row < y + h; row++)
    for (uint32_t column = x; column < x + w; column++)
      fb[(uint64_t)row * stride + column] = color;
}

static uint64_t sample_hash(volatile uint32_t *fb, uint32_t width,
                            uint32_t height, uint32_t stride) {
  uint64_t hash = 1469598103934665603ULL;
  uint32_t step_x = width / 16; if (!step_x) step_x = 1;
  uint32_t step_y = height / 16; if (!step_y) step_y = 1;
  for (uint32_t y = 0; y < height; y += step_y)
    for (uint32_t x = 0; x < width; x += step_x) {
      hash ^= fb[(uint64_t)y * stride + x];
      hash *= 1099511628211ULL;
    }
  return hash;
}

int aiueos_framebuffer_initialize(const struct aiueos_boot_info *boot) {
  desktop_surface_ready = 0;
  if (!boot || !boot->framebuffer_base || !boot->framebuffer_size ||
      boot->framebuffer_width < 320 || boot->framebuffer_height < 200 ||
      boot->framebuffer_stride < boot->framebuffer_width ||
      boot->framebuffer_format > 1 ||
      (uint64_t)boot->framebuffer_stride * boot->framebuffer_height >
        boot->framebuffer_size / 4 ||
      !aiueos_map_framebuffer(boot->framebuffer_base, boot->framebuffer_size))
    return 0;

  volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)boot->framebuffer_base;
  desktop_surface_pixels = fb;
  rectangle(fb, boot->framebuffer_stride, boot->framebuffer_format, 0, 0,
            boot->framebuffer_width, boot->framebuffer_height, 0x101827);
  uint32_t margin = boot->framebuffer_width / 16;
  uint32_t top = boot->framebuffer_height / 12;
  rectangle(fb, boot->framebuffer_stride, boot->framebuffer_format,
            margin, top, boot->framebuffer_width - 2 * margin,
            boot->framebuffer_height / 10, 0x2557a7);
  rectangle(fb, boot->framebuffer_stride, boot->framebuffer_format,
            margin, top + boot->framebuffer_height / 7,
            (boot->framebuffer_width - 3 * margin) / 2,
            boot->framebuffer_height * 2 / 3, 0xf2f5f9);
  rectangle(fb, boot->framebuffer_stride, boot->framebuffer_format,
            boot->framebuffer_width / 2 + margin / 2,
            top + boot->framebuffer_height / 7,
            (boot->framebuffer_width - 3 * margin) / 2,
            boot->framebuffer_height * 2 / 3, 0x35b779);
  uint64_t first = sample_hash(fb, boot->framebuffer_width,
                               boot->framebuffer_height, boot->framebuffer_stride);
  uint64_t second = sample_hash(fb, boot->framebuffer_width,
                                boot->framebuffer_height, boot->framebuffer_stride);
  if (!first || first != second) return 0;
  desktop_surface = (struct aiueos_desktop_surface){
    1, sizeof(desktop_surface), 1, 1, first,
    boot->framebuffer_width, boot->framebuffer_height, boot->framebuffer_stride,
    boot->framebuffer_format, 0, 0, boot->framebuffer_width, boot->framebuffer_height};
  desktop_surface_ready = 1;
  return 1;
}
