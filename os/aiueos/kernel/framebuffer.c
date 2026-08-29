#include <stdint.h>
#include "inference_status.h"

struct aiueos_boot_info {
  uint64_t magic, version;
  void *memory_map; uint64_t memory_map_size, descriptor_size, descriptor_version;
  void *acpi_rsdp;
  uint64_t framebuffer_base, framebuffer_size;
  uint32_t framebuffer_width, framebuffer_height, framebuffer_stride, framebuffer_format;
};

extern int aiueos_map_framebuffer(uint64_t address, uint64_t length);

#ifdef AIUEOS_PHYSICAL_QUALIFICATION
extern int aiueos_qualification_progress(uint32_t code);
#define FRAMEBUFFER_PROGRESS(code) aiueos_qualification_progress(code)
#else
#define FRAMEBUFFER_PROGRESS(code) ((void)0)
#endif

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

/* Physical qualification UI.  The normal desktop remains unchanged; this
   deliberately tiny built-in font is used only by the read-only K16 USB
   profile after the kernel owns the GOP aperture.  A photographed screen is
   useful evidence on machines without an exposed serial port. */
static const uint8_t *qualification_glyph(char c) {
  static const uint8_t blank[5] = {0,0,0,0,0};
  static const uint8_t a[5] = {0x7e,0x11,0x11,0x11,0x7e};
  static const uint8_t b[5] = {0x7f,0x49,0x49,0x49,0x36};
  static const uint8_t c_[5] = {0x3e,0x41,0x41,0x41,0x22};
  static const uint8_t d[5] = {0x7f,0x41,0x41,0x22,0x1c};
  static const uint8_t e[5] = {0x7f,0x49,0x49,0x49,0x41};
  static const uint8_t f[5] = {0x7f,0x09,0x09,0x09,0x01};
  static const uint8_t g[5] = {0x3e,0x41,0x49,0x49,0x7a};
  static const uint8_t h[5] = {0x7f,0x08,0x08,0x08,0x7f};
  static const uint8_t i[5] = {0x41,0x41,0x7f,0x41,0x41};
  static const uint8_t j[5] = {0x20,0x40,0x41,0x3f,0x01};
  static const uint8_t k[5] = {0x7f,0x08,0x14,0x22,0x41};
  static const uint8_t l[5] = {0x7f,0x40,0x40,0x40,0x40};
  static const uint8_t m[5] = {0x7f,0x02,0x0c,0x02,0x7f};
  static const uint8_t n[5] = {0x7f,0x04,0x08,0x10,0x7f};
  static const uint8_t o[5] = {0x3e,0x41,0x41,0x41,0x3e};
  static const uint8_t p[5] = {0x7f,0x09,0x09,0x09,0x06};
  static const uint8_t q[5] = {0x3e,0x41,0x51,0x21,0x5e};
  static const uint8_t r[5] = {0x7f,0x09,0x19,0x29,0x46};
  static const uint8_t s[5] = {0x46,0x49,0x49,0x49,0x31};
  static const uint8_t t[5] = {0x01,0x01,0x7f,0x01,0x01};
  static const uint8_t u[5] = {0x3f,0x40,0x40,0x40,0x3f};
  static const uint8_t v[5] = {0x1f,0x20,0x40,0x20,0x1f};
  static const uint8_t w[5] = {0x7f,0x20,0x18,0x20,0x7f};
  static const uint8_t x[5] = {0x63,0x14,0x08,0x14,0x63};
  static const uint8_t y[5] = {0x07,0x08,0x70,0x08,0x07};
  static const uint8_t z[5] = {0x61,0x51,0x49,0x45,0x43};
  static const uint8_t zero[5] = {0x3e,0x51,0x49,0x45,0x3e};
  static const uint8_t one[5] = {0x00,0x42,0x7f,0x40,0x00};
  static const uint8_t two[5] = {0x62,0x51,0x49,0x49,0x46};
  static const uint8_t three[5] = {0x22,0x41,0x49,0x49,0x36};
  static const uint8_t four[5] = {0x18,0x14,0x12,0x7f,0x10};
  static const uint8_t five[5] = {0x2f,0x49,0x49,0x49,0x31};
  static const uint8_t six[5] = {0x3c,0x4a,0x49,0x49,0x30};
  static const uint8_t seven[5] = {0x01,0x71,0x09,0x05,0x03};
  static const uint8_t eight[5] = {0x36,0x49,0x49,0x49,0x36};
  static const uint8_t nine[5] = {0x06,0x49,0x49,0x29,0x1e};
  static const uint8_t dot[5] = {0x00,0x60,0x60,0x00,0x00};
  static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
  static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
  static const uint8_t slash[5] = {0x60,0x18,0x06,0x01,0x00};
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  switch (c) {
    case 'A': return a; case 'B': return b; case 'C': return c_;
    case 'D': return d; case 'E': return e; case 'F': return f;
    case 'G': return g; case 'H': return h; case 'I': return i;
    case 'J': return j; case 'K': return k; case 'L': return l;
    case 'M': return m; case 'N': return n; case 'O': return o;
    case 'P': return p; case 'Q': return q; case 'R': return r;
    case 'S': return s; case 'T': return t; case 'U': return u;
    case 'V': return v; case 'W': return w; case 'X': return x;
    case 'Y': return y; case 'Z': return z;
    case '0': return zero; case '1': return one; case '2': return two;
    case '3': return three; case '4': return four; case '5': return five;
    case '6': return six; case '7': return seven; case '8': return eight;
    case '9': return nine; case '.': return dot; case ':': return colon;
    case '-': return dash; case '/': return slash;
    default: return blank;
  }
}

static void qualification_text(const char *text, uint32_t x, uint32_t y,
                               uint32_t scale, uint32_t color) {
  if (!text) return;
  while (*text) {
    const uint8_t *glyph = qualification_glyph(*text++);
    for (uint32_t column = 0; column < 5; column++)
      for (uint32_t row = 0; row < 7; row++)
        if (glyph[column] & (1U << row))
          rectangle(desktop_surface_pixels, desktop_surface.stride,
                    desktop_surface.pixel_format,
                    x + column * scale, y + row * scale, scale, scale, color);
    x += 6 * scale;
  }
}

static uint32_t qualification_text_width(const char *text, uint32_t scale) {
  uint32_t length = 0;
  if (!text) return 0;
  while (text[length] && length <= AIUEOS_INFERENCE_STATUS_TEXT_MAX) length++;
  return length * 6U * scale;
}

static uint32_t inference_u64(uint64_t value, char *out, uint32_t capacity) {
  char reverse[20]; uint32_t count = 0, written = 0;
  if (!out || !capacity) return 0;
  do { reverse[count++] = (char)('0' + value % 10U); value /= 10U; }
  while (value && count < sizeof(reverse));
  if (count + 1U > capacity) return 0;
  while (count) out[written++] = reverse[--count];
  out[written] = 0;
  return written;
}

static void inference_number(uint64_t value, uint32_t x, uint32_t y,
                             uint32_t scale, uint32_t color) {
  char number[21];
  if (inference_u64(value, number, sizeof(number)))
    qualification_text(number, x, y, scale, color);
}

static void inference_na(uint32_t x, uint32_t y, uint32_t scale,
                         uint32_t color) {
  qualification_text("N/A", x, y, scale, color);
}

static void inference_millis(uint64_t nanoseconds, uint32_t x, uint32_t y,
                             uint32_t scale, uint32_t color) {
  if (nanoseconds == AIUEOS_INFERENCE_UNMEASURED) {
    inference_na(x, y, scale, color); return;
  }
  inference_number(nanoseconds / 1000000ULL, x, y, scale, color);
}

static void inference_rate(uint32_t tokens, uint64_t nanoseconds,
                           uint32_t x, uint32_t y, uint32_t scale,
                           uint32_t color) {
  uint64_t rate = aiueos_inference_milli_tokens_per_second(tokens, nanoseconds);
  if (rate == AIUEOS_INFERENCE_UNMEASURED) {
    inference_na(x, y, scale, color); return;
  }
  char whole[21];
  uint32_t whole_length = inference_u64(rate / 1000ULL, whole, sizeof(whole));
  qualification_text(whole, x, y, scale, color);
  x += whole_length * 6U * scale;
  qualification_text(".", x, y, scale, color); x += 6U * scale;
  char decimal[4] = {(char)('0' + (rate / 100ULL) % 10ULL),
                     (char)('0' + (rate / 10ULL) % 10ULL),
                     (char)('0' + rate % 10ULL), 0};
  qualification_text(decimal, x, y, scale, color);
}

static const char *inference_phase(enum aiueos_inference_phase phase) {
  switch (phase) {
    case AIUEOS_INFERENCE_ADMISSION: return "ADMISSION";
    case AIUEOS_INFERENCE_LOADING: return "LOADING";
    case AIUEOS_INFERENCE_PREFILL: return "PREFILL";
    case AIUEOS_INFERENCE_DECODING: return "DECODING";
    case AIUEOS_INFERENCE_COMPLETE: return "COMPLETE";
    case AIUEOS_INFERENCE_BLOCKED: return "BLOCKED";
    case AIUEOS_INFERENCE_ERROR: return "ERROR";
    default: return "INVALID";
  }
}

static void framebuffer_commit(void) {
  desktop_surface.generation += 1;
  desktop_surface.content_hash =
    sample_hash(desktop_surface_pixels, desktop_surface.width,
                desktop_surface.height, desktop_surface.stride);
  desktop_surface.damage_x = 0; desktop_surface.damage_y = 0;
  desktop_surface.damage_width = desktop_surface.width;
  desktop_surface.damage_height = desktop_surface.height;
}

int aiueos_framebuffer_inference_screen(
    const struct aiueos_inference_status *status) {
  if (!desktop_surface_ready || !aiueos_inference_status_valid(status)) return 0;
  uint32_t scale = desktop_surface.width >= 1280 ? 4U :
                   desktop_surface.width >= 800 ? 3U : 2U;
  uint32_t margin = desktop_surface.width / 18U;
  uint32_t value_x = desktop_surface.width / 2U;
  uint32_t y = margin + 12U * scale;
  uint32_t row = 10U * scale;
  uint32_t accent = 0x3b82f6U;
  if (status->phase == AIUEOS_INFERENCE_COMPLETE) accent = 0x35d07fU;
  if (status->phase == AIUEOS_INFERENCE_BLOCKED ||
      status->phase == AIUEOS_INFERENCE_ERROR) accent = 0xff6b6bU;
  rectangle(desktop_surface_pixels, desktop_surface.stride,
            desktop_surface.pixel_format, 0, 0, desktop_surface.width,
            desktop_surface.height, 0x0b1220U);
  rectangle(desktop_surface_pixels, desktop_surface.stride,
            desktop_surface.pixel_format, margin, margin,
            desktop_surface.width - 2U * margin, 3U * scale, accent);
  qualification_text("AIUEOS INFERENCE", margin, y, scale, 0xf4f7f9U);
  y += row;
  qualification_text(status->model, margin, y, scale, 0xf4f7f9U);
  qualification_text(status->quant, value_x, y, scale, 0x94a3b8U);
  y += row;
  qualification_text("STATE", margin, y, scale, 0x94a3b8U);
  qualification_text(inference_phase(status->phase), value_x, y, scale, accent);
  y += row;
  qualification_text("DETAIL", margin, y, scale, 0x94a3b8U);
  qualification_text(status->detail ? status->detail : "N/A",
                     value_x, y, scale, 0xf4f7f9U);
  y += row;
  qualification_text("LOAD MS", margin, y, scale, 0x94a3b8U);
  inference_millis(status->load_ns, value_x, y, scale, 0xf4f7f9U);
  y += row;
  qualification_text("PREFILL TOK/S", margin, y, scale, 0x94a3b8U);
  inference_rate(status->prompt_tokens, status->prefill_ns,
                 value_x, y, scale, 0xf4f7f9U);
  y += row;
  qualification_text("DECODE TOK/S", margin, y, scale, 0x94a3b8U);
  inference_rate(status->generated_tokens, status->decode_ns,
                 value_x, y, scale, 0xf4f7f9U);
  y += row;
  qualification_text("TOKENS", margin, y, scale, 0x94a3b8U);
  inference_number(status->generated_tokens, value_x, y, scale, 0xf4f7f9U);
  if (status->target_tokens) {
    uint32_t offset = qualification_text_width("00000000", scale);
    qualification_text("/", value_x + offset, y, scale, 0x94a3b8U);
    inference_number(status->target_tokens, value_x + offset + 6U * scale,
                     y, scale, 0xf4f7f9U);
  }
  y += row;
  qualification_text("RESIDENT MIB", margin, y, scale, 0x94a3b8U);
  if (status->resident_bytes == AIUEOS_INFERENCE_UNMEASURED)
    inference_na(value_x, y, scale, 0xf4f7f9U);
  else
    inference_number(status->resident_bytes / (1024ULL * 1024ULL),
                     value_x, y, scale, 0xf4f7f9U);
  y += row;
  qualification_text("TTFT MS", margin, y, scale, 0x94a3b8U);
  inference_millis(status->time_to_first_token_ns,
                   value_x, y, scale, 0xf4f7f9U);
  y += row;
  qualification_text("CYCLES", margin, y, scale, 0x94a3b8U);
  if (status->compute_cycles) inference_number(status->compute_cycles,
                                               value_x, y, scale, 0xf4f7f9U);
  else inference_na(value_x, y, scale, 0xf4f7f9U);

  uint32_t bar_y = desktop_surface.height - margin - 6U * scale;
  uint32_t bar_width = desktop_surface.width - 2U * margin;
  rectangle(desktop_surface_pixels, desktop_surface.stride,
            desktop_surface.pixel_format, margin, bar_y, bar_width,
            6U * scale, 0x1e293bU);
  uint32_t fill = 0;
  if (status->target_tokens)
    fill = (uint32_t)(((uint64_t)bar_width * status->generated_tokens) /
                      status->target_tokens);
  else if (status->phase == AIUEOS_INFERENCE_COMPLETE) fill = bar_width;
  else if (status->phase >= AIUEOS_INFERENCE_LOADING &&
           status->phase <= AIUEOS_INFERENCE_DECODING)
    fill = bar_width * (uint32_t)status->phase / 5U;
  if (fill) rectangle(desktop_surface_pixels, desktop_surface.stride,
                      desktop_surface.pixel_format, margin, bar_y, fill,
                      6U * scale, accent);
  framebuffer_commit();
  return 1;
}

void aiueos_framebuffer_qualification_screen(const char *line1,
                                             const char *line2,
                                             const char *line3,
                                             int success) {
  if (!desktop_surface_ready) return;
  uint32_t scale = desktop_surface.width >= 1280 ? 6 : 4;
  uint32_t margin = desktop_surface.width / 18;
  uint32_t top = desktop_surface.height / 5;
  uint32_t background = success ? 0x083f2e : 0x681c28;
  uint32_t accent = success ? 0x35d07f : 0xff6b6b;
  rectangle(desktop_surface_pixels, desktop_surface.stride,
            desktop_surface.pixel_format, 0, 0, desktop_surface.width,
            desktop_surface.height, background);
  rectangle(desktop_surface_pixels, desktop_surface.stride,
            desktop_surface.pixel_format, margin, margin,
            desktop_surface.width - 2 * margin, scale * 2, accent);
  qualification_text(line1, margin, top, scale, 0xf4f7f9);
  qualification_text(line2, margin, top + 12 * scale, scale, 0xf4f7f9);
  qualification_text(line3, margin, top + 24 * scale, scale, accent);
  framebuffer_commit();
}

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
  FRAMEBUFFER_PROGRESS(250);
  if (!boot || !boot->framebuffer_base || !boot->framebuffer_size ||
      boot->framebuffer_width < 320 || boot->framebuffer_height < 200 ||
      boot->framebuffer_stride < boot->framebuffer_width ||
      boot->framebuffer_format > 1 ||
      (uint64_t)boot->framebuffer_stride * boot->framebuffer_height >
        boot->framebuffer_size / 4)
    return 0;
  FRAMEBUFFER_PROGRESS(251);
  if (!aiueos_map_framebuffer(boot->framebuffer_base, boot->framebuffer_size))
    return 0;
  FRAMEBUFFER_PROGRESS(252);

  volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)boot->framebuffer_base;
  desktop_surface_pixels = fb;
  rectangle(fb, boot->framebuffer_stride, boot->framebuffer_format, 0, 0,
            boot->framebuffer_width, boot->framebuffer_height, 0x101827);
  FRAMEBUFFER_PROGRESS(253);
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
  FRAMEBUFFER_PROGRESS(254);
  uint64_t first = sample_hash(fb, boot->framebuffer_width,
                               boot->framebuffer_height, boot->framebuffer_stride);
  FRAMEBUFFER_PROGRESS(255);
  uint64_t second = sample_hash(fb, boot->framebuffer_width,
                                boot->framebuffer_height, boot->framebuffer_stride);
  FRAMEBUFFER_PROGRESS(256);
  if (!first || first != second) return 0;
  desktop_surface = (struct aiueos_desktop_surface){
    1, sizeof(desktop_surface), 1, 1, first,
    boot->framebuffer_width, boot->framebuffer_height, boot->framebuffer_stride,
    boot->framebuffer_format, 0, 0, boot->framebuffer_width, boot->framebuffer_height};
  desktop_surface_ready = 1;
  FRAMEBUFFER_PROGRESS(257);
  return 1;
}
