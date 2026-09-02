/* SPDX-License-Identifier: Apache-2.0 */
#include "rtl8125.h"

/* Register facts and descriptor layouts are cross-checked against the
   ISC-licensed OpenBSD rge(4) interface (if_rgereg.h).  This implementation is
   an AIUEOS-specific, bounded PXE-handoff driver: it keeps the firmware's PHY
   and MCU setup and replaces only the DMA rings after ExitBootServices. */

#define RGE_MAC0 0x0000U
#define RGE_MAC4 0x0004U
#define RGE_TXDESC_LO 0x0020U
#define RGE_TXDESC_HI 0x0024U
#define RGE_CMD 0x0037U
#define RGE_IMR 0x0038U
#define RGE_ISR 0x003cU
#define RGE_TXCFG 0x0040U
#define RGE_RXCFG 0x0044U
#define RGE_PHYSTAT 0x006cU
#define RGE_TXSTART 0x0090U
#define RGE_MCUCMD 0x00d3U
#define RGE_RXMAXSIZE 0x00daU
#define RGE_RXDESC_LO 0x00e4U
#define RGE_RXDESC_HI 0x00e8U

#define RGE_CMD_TX 0x04U
#define RGE_CMD_RX 0x08U
#define RGE_CMD_STOP 0x80U
#define RGE_FIFO_EMPTY 0x30U
#define RGE_PHYSTAT_LINK 0x0002U
#define RGE_RXCFG_INDIVIDUAL 0x00000002U
#define RGE_RXCFG_BROADCAST 0x00000008U
#define RGE_TXCFG_HWREV 0x7cf00000U
#define RGE_TXCFG_CONFIG 0x03000700U
#define RGE_RXCFG_8125 0x41000700U
#define RGE_RXCFG_8125B 0x41000c00U
#define RGE_RXCFG_8125D 0x41200c00U

#define RGE_DESC_OWN 0x80000000U
#define RGE_DESC_EOR 0x40000000U
#define RGE_TX_SOF 0x20000000U
#define RGE_TX_EOF 0x10000000U
#define RGE_RX_SOF 0x02000000U
#define RGE_RX_EOF 0x01000000U
#define RGE_RX_ERROR 0x00100000U
#define RGE_RX_LENGTH 0x00003fffU

_Static_assert(sizeof(struct aiueos_rtl8125_tx_desc)==32,"RTL8125 TX descriptor");
_Static_assert(sizeof(struct aiueos_rtl8125_rx_desc)==32,"RTL8125 RX descriptor");

static void bytes_zero(void *value, uint32_t bytes) {
  uint8_t *p = value;
  while (bytes--) *p++ = 0;
}

static uint16_t load_be16(const uint8_t *at) {
  return (uint16_t)(((uint16_t)at[0] << 8) | at[1]);
}

static uint32_t load_be32(const uint8_t *at) {
  return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) |
         ((uint32_t)at[2] << 8) | at[3];
}

static void store_be16(uint8_t *at, uint16_t value) {
  at[0] = (uint8_t)(value >> 8); at[1] = (uint8_t)value;
}

static void store_be32(uint8_t *at, uint32_t value) {
  at[0] = (uint8_t)(value >> 24); at[1] = (uint8_t)(value >> 16);
  at[2] = (uint8_t)(value >> 8); at[3] = (uint8_t)value;
}

static int mac_equal(const uint8_t *left, const uint8_t *right) {
  uint8_t difference = 0;
  for (unsigned i = 0; i < 6; i++) difference |= left[i] ^ right[i];
  return difference == 0;
}

static int mac_broadcast(const uint8_t *mac) {
  uint8_t all = 0xff;
  for (unsigned i = 0; i < 6; i++) all &= mac[i];
  return all == 0xff;
}

int aiueos_rtl8125_direct_arp_request(
    const uint8_t *frame, uint32_t frame_length,
    const uint8_t local_mac[6], const uint8_t peer_mac[6],
    uint32_t local_ip, uint32_t peer_ip) {
  if (!frame || !local_mac || !peer_mac || frame_length < 42U ||
      !local_ip || !peer_ip) return 0;
  if ((!mac_broadcast(frame) && !mac_equal(frame, local_mac)) ||
      !mac_equal(frame + 6, peer_mac) ||
      load_be16(frame + 12) != 0x0806U ||
      load_be16(frame + 14) != 1U ||
      load_be16(frame + 16) != 0x0800U ||
      frame[18] != 6U || frame[19] != 4U ||
      load_be16(frame + 20) != 1U ||
      !mac_equal(frame + 22, peer_mac) ||
      load_be32(frame + 28) != peer_ip ||
      load_be32(frame + 38) != local_ip) return 0;
  return 1;
}

uint32_t aiueos_rtl8125_direct_arp_reply(
    uint8_t *frame, uint32_t capacity,
    const uint8_t local_mac[6], const uint8_t peer_mac[6],
    uint32_t local_ip, uint32_t peer_ip) {
  if (!frame || capacity < 42U || !local_mac || !peer_mac ||
      !local_ip || !peer_ip) return 0;
  bytes_zero(frame, 42U);
  for (unsigned i = 0; i < 6; i++) {
    frame[i] = peer_mac[i]; frame[6 + i] = local_mac[i];
    frame[22 + i] = local_mac[i]; frame[32 + i] = peer_mac[i];
  }
  store_be16(frame + 12, 0x0806U);
  store_be16(frame + 14, 1U);
  store_be16(frame + 16, 0x0800U);
  frame[18] = 6U; frame[19] = 4U;
  store_be16(frame + 20, 2U);
  store_be32(frame + 28, local_ip);
  store_be32(frame + 38, peer_ip);
  return 42U;
}

static int io_valid(const struct aiueos_rtl8125_io *io) {
  return io && io->read8 && io->read16 && io->read32 && io->write8 &&
         io->write16 && io->write32;
}

static enum aiueos_rtl8125_revision revision_from_txcfg(uint32_t value) {
  switch (value & RGE_TXCFG_HWREV) {
    case 0x60900000U: return AIUEOS_RTL8125_REV_8125;
    case 0x64100000U: return AIUEOS_RTL8125_REV_8125B;
    case 0x68800000U: return AIUEOS_RTL8125_REV_8125D_1;
    case 0x68900000U: return AIUEOS_RTL8125_REV_8125D_2;
    default: return AIUEOS_RTL8125_REV_NONE;
  }
}

static uint32_t receive_config(enum aiueos_rtl8125_revision revision) {
  if (revision == AIUEOS_RTL8125_REV_8125) return RGE_RXCFG_8125;
  if (revision == AIUEOS_RTL8125_REV_8125B) return RGE_RXCFG_8125B;
  return RGE_RXCFG_8125D;
}

static int mac_valid(const uint8_t mac[6]) {
  uint8_t any = 0, all_ff = 0xff;
  for (unsigned i = 0; i < 6; i++) { any |= mac[i]; all_ff &= mac[i]; }
  return any && all_ff != 0xff && !(mac[0] & 1U);
}

static void dma_release(void) {
  __atomic_thread_fence(__ATOMIC_RELEASE);
}

static void dma_acquire(void) {
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

static enum aiueos_rtl8125_result rings_restart(
    struct aiueos_rtl8125 *device) {
  const struct aiueos_rtl8125_io *io = &device->io;

  /* Each worker POST is a separate short TLS connection.  Stop and drain the
     already-owned engine before reusing its single descriptor so late frames
     from the previous four-tuple cannot hide the next SYN-ACK in the RTL FIFO.
     Keep the firmware-owned PHY/MCU calibration and the revision captured at
     takeover; TXCFG no longer contains usable revision bits after takeover. */
  io->write8(io->context,RGE_CMD,
             (uint8_t)(io->read8(io->context,RGE_CMD)|RGE_CMD_STOP));
  unsigned budget;
  for (budget=0;budget<300000U;budget++)
    if ((io->read8(io->context,RGE_MCUCMD)&RGE_FIFO_EMPTY)==RGE_FIFO_EMPTY)
      break;
  if (budget==300000U) {
    device->ready=0;
    return AIUEOS_RTL8125_FIFO_TIMEOUT;
  }
  io->write8(io->context,RGE_CMD,0);

  bytes_zero(device->tx,sizeof(*device->tx));
  bytes_zero(device->rx,sizeof(*device->rx));
  device->tx->command = RGE_DESC_EOR;
  device->tx->address = device->tx_frame_physical;
  device->rx->address = device->rx_frame_physical;
  device->rx->command = RGE_DESC_OWN|RGE_DESC_EOR|
                        AIUEOS_RTL8125_FRAME_CAPACITY;
  dma_release();

  io->write32(io->context,RGE_IMR,0);
  io->write32(io->context,RGE_ISR,0xffffffffU);
  io->write32(io->context,RGE_TXDESC_LO,(uint32_t)device->tx_desc_physical);
  io->write32(io->context,RGE_TXDESC_HI,
              (uint32_t)(device->tx_desc_physical>>32));
  io->write32(io->context,RGE_RXDESC_LO,(uint32_t)device->rx_desc_physical);
  io->write32(io->context,RGE_RXDESC_HI,
              (uint32_t)(device->rx_desc_physical>>32));
  io->write16(io->context,RGE_RXMAXSIZE,AIUEOS_RTL8125_FRAME_CAPACITY);
  io->write32(io->context,RGE_TXCFG,RGE_TXCFG_CONFIG);
  io->write32(io->context,RGE_RXCFG,receive_config(device->revision)|
              RGE_RXCFG_INDIVIDUAL|RGE_RXCFG_BROADCAST);
  io->write8(io->context,RGE_CMD,RGE_CMD_TX|RGE_CMD_RX);
  return AIUEOS_RTL8125_OK;
}

enum aiueos_rtl8125_result aiueos_rtl8125_takeover(
    struct aiueos_rtl8125 *device,
    const struct aiueos_rtl8125_io *io,
    struct aiueos_rtl8125_tx_desc *tx, uint64_t tx_desc_physical,
    struct aiueos_rtl8125_rx_desc *rx, uint64_t rx_desc_physical,
    uint8_t *tx_frame, uint64_t tx_frame_physical,
    uint8_t *rx_frame, uint64_t rx_frame_physical) {
  if (!device || !io_valid(io) || !tx || !rx || !tx_frame || !rx_frame ||
      !tx_desc_physical || !rx_desc_physical || !tx_frame_physical ||
      !rx_frame_physical || (tx_desc_physical & 0xffU) ||
      (rx_desc_physical & 0xffU) || ((uintptr_t)tx & 0xffU) ||
      ((uintptr_t)rx & 0xffU)) return AIUEOS_RTL8125_INVALID;

  enum aiueos_rtl8125_revision revision =
    revision_from_txcfg(io->read32(io->context,RGE_TXCFG));
  if (revision == AIUEOS_RTL8125_REV_NONE)
    return AIUEOS_RTL8125_UNSUPPORTED_REVISION;

  uint32_t mac0 = io->read32(io->context,RGE_MAC0);
  uint16_t mac4 = io->read16(io->context,RGE_MAC4);
  uint8_t mac[6] = {(uint8_t)mac0,(uint8_t)(mac0>>8),(uint8_t)(mac0>>16),
                    (uint8_t)(mac0>>24),(uint8_t)mac4,(uint8_t)(mac4>>8)};
  if (!mac_valid(mac)) return AIUEOS_RTL8125_INVALID_MAC;

  *device=(struct aiueos_rtl8125){0};
  device->io=*io;device->tx=tx;device->rx=rx;
  device->tx_frame=tx_frame;device->rx_frame=rx_frame;
  device->tx_desc_physical=tx_desc_physical;
  device->rx_desc_physical=rx_desc_physical;
  device->tx_frame_physical=tx_frame_physical;
  device->rx_frame_physical=rx_frame_physical;
  for (unsigned i=0;i<6;i++) device->mac[i]=mac[i];
  device->revision=revision;device->ready=1;
  return rings_restart(device);
}

enum aiueos_rtl8125_result aiueos_rtl8125_restart(
    struct aiueos_rtl8125 *device) {
  if (!device || !device->ready || !io_valid(&device->io) ||
      !device->tx || !device->rx || !device->tx_frame || !device->rx_frame ||
      !device->tx_desc_physical || !device->rx_desc_physical ||
      !device->tx_frame_physical || !device->rx_frame_physical ||
      device->revision < AIUEOS_RTL8125_REV_8125 ||
      device->revision > AIUEOS_RTL8125_REV_8125D_2)
    return AIUEOS_RTL8125_INVALID;
  return rings_restart(device);
}

int aiueos_rtl8125_link_up(const struct aiueos_rtl8125 *device) {
  return device && device->ready &&
    (device->io.read16(device->io.context,RGE_PHYSTAT)&RGE_PHYSTAT_LINK);
}

enum aiueos_rtl8125_result aiueos_rtl8125_tx_submit(
    struct aiueos_rtl8125 *device, uint32_t frame_length) {
  if (!device || !device->ready || frame_length<14 ||
      frame_length>AIUEOS_RTL8125_FRAME_CAPACITY)
    return AIUEOS_RTL8125_INVALID;
  dma_acquire();
  if (device->tx->command&RGE_DESC_OWN) return AIUEOS_RTL8125_TX_BUSY;
  device->tx->extension=0;
  device->tx->address=device->tx_frame_physical;
  dma_release();
  device->tx->command=RGE_DESC_OWN|RGE_DESC_EOR|RGE_TX_SOF|RGE_TX_EOF|
                      frame_length;
  dma_release();
  device->io.write16(device->io.context,RGE_TXSTART,1);
  return AIUEOS_RTL8125_OK;
}

int aiueos_rtl8125_tx_complete(const struct aiueos_rtl8125 *device) {
  if (!device || !device->ready) return 0;
  dma_acquire();
  return !(device->tx->command&RGE_DESC_OWN);
}

enum aiueos_rtl8125_result aiueos_rtl8125_rx_poll(
    struct aiueos_rtl8125 *device, uint32_t *frame_length) {
  if (!device || !device->ready || !frame_length)
    return AIUEOS_RTL8125_INVALID;
  dma_acquire();
  uint32_t command=device->rx->command;
  if (command&RGE_DESC_OWN) { *frame_length=0; return AIUEOS_RTL8125_OK; }
  uint32_t bytes=command&RGE_RX_LENGTH;
  if ((command&RGE_RX_ERROR) || !(command&RGE_RX_SOF) ||
      !(command&RGE_RX_EOF) || bytes<18 ||
      bytes>AIUEOS_RTL8125_FRAME_CAPACITY)
    return AIUEOS_RTL8125_RX_INVALID;
  *frame_length=bytes-4; /* hardware includes the Ethernet FCS */
  return AIUEOS_RTL8125_OK;
}

void aiueos_rtl8125_rx_rearm(struct aiueos_rtl8125 *device) {
  if (!device || !device->ready) return;
  device->rx->extension=0;
  device->rx->address=device->rx_frame_physical;
  dma_release();
  device->rx->command=RGE_DESC_OWN|RGE_DESC_EOR|
                      AIUEOS_RTL8125_FRAME_CAPACITY;
  dma_release();
}

/* ==========================================================================
   THE KOTOBA PARITY SELF-TEST (ADR-0137)
   ==========================================================================
   Six Kotoba objects now express this driver's register sequence, its
   descriptor layouts and its status decisions.  QEMU has no RTL8125 model, so
   the evidence that they say what the C above says is this: run BOTH against a
   SOFTWARE MODEL of the BAR -- 4,096 bytes of ordinary memory seeded so that
   each register read returns what a real RTL8125B returns at that point -- and
   compare the bytes each leaves behind.

   That catches width and offset errors, which is most of what a driver gets
   wrong: a `write16` where the hardware wants a `write32` leaves two neighbours
   different, and an offset off by four writes into a register nobody meant.

   WHAT IT DOES NOT CATCH, said plainly rather than left to be discovered.  A
   memory model has no time in it, so the ORDER two writes happen in is
   invisible whenever both survive to the end.  Three things recover part of
   that and none of them recovers all of it:

     * The FIFO-drain stage seeds MCUCMD so the engine never reports empty.
       Both implementations must then return FIFO_TIMEOUT and must leave
       RGE_CMD at 0x8c -- the STOP bit set and the register NOT cleared.  That
       is an ordering assertion: the drain gate happens after the stop and
       before everything downstream of it.
     * The TX_BUSY stage submits twice.  The second must refuse, which is only
       true if the first actually set the OWN bit before the doorbell.
     * The store fences themselves are checked as EMITTED BYTES, not here:
       `verify-kotoba-kernel-object.py` sees the objects, and
       `rtl8125-ring-build.o` / `-tx-submit.o` contain `sfence` (0f ae f8).
       A fence writes no memory, so no memory comparison can ever see one.

   The seed values are not invented here.  `tests/rtl8125_handoff_model.c`:23-25
   already seeds MAC0 = 0x0bfc7070, MAC4 = 0x32b6, TXCFG = 0x64100000 and
   PHYSTAT = 0x0013, and asserts the resulting register file at :63-68.  So the
   fixture is the repository's own and a disagreement is a disagreement with a
   test that predates these objects.
   ========================================================================== */

extern uint64_t kotoba_aiueos_rtl8125_identify(uint64_t, uint64_t, uint64_t,
                                               uint64_t);
extern uint64_t kotoba_aiueos_rtl8125_link_up(uint64_t, uint64_t);
extern uint64_t kotoba_aiueos_rtl8125_ring_build(uint64_t, uint64_t, uint64_t,
                                                 uint64_t, uint64_t);
extern uint64_t kotoba_aiueos_rtl8125_program(uint64_t, uint64_t, uint64_t,
                                              uint64_t, uint64_t);
extern uint64_t kotoba_aiueos_rtl8125_tx_submit(uint64_t, uint64_t, uint64_t,
                                                uint64_t, uint64_t);
extern int64_t kotoba_aiueos_rtl8125_rx_poll(uint64_t, uint64_t);

#define RTL_PARITY_BAR_BYTES 4096U
#define RTL_PARITY_DESC_BYTES 32U
/* The frames are never read or written by anything -- no DMA engine exists in
   this model -- so only their ADDRESSES matter, and 64 bytes is enough to have
   one.  Sized down from AIUEOS_RTL8125_FRAME_CAPACITY deliberately: 4 KiB of
   .bss for a buffer nothing touches is 4 KiB the kernel does not have. */
#define RTL_PARITY_FRAME_BYTES 64U

static uint8_t rtl_parity_bar[RTL_PARITY_BAR_BYTES] __attribute__((aligned(4096)));
static uint8_t rtl_parity_expect[RTL_PARITY_BAR_BYTES];
static struct aiueos_rtl8125_tx_desc rtl_parity_tx __attribute__((aligned(256)));
static struct aiueos_rtl8125_rx_desc rtl_parity_rx __attribute__((aligned(256)));
static uint8_t rtl_parity_tx_expect[RTL_PARITY_DESC_BYTES];
static uint8_t rtl_parity_rx_expect[RTL_PARITY_DESC_BYTES];
static uint8_t rtl_parity_txframe[RTL_PARITY_FRAME_BYTES] __attribute__((aligned(64)));
static uint8_t rtl_parity_rxframe[RTL_PARITY_FRAME_BYTES] __attribute__((aligned(64)));
static struct aiueos_rtl8125 rtl_parity_device;

/* The stage that failed, 0 when every stage passed, and a byte offset when the
   failure was a byte comparison.  Globals rather than out-parameters so main.c
   can print them without this file owning a printer. */
unsigned aiueos_rtl8125_parity_stage;
unsigned aiueos_rtl8125_parity_detail;

static uint8_t rtl_parity_r8(void *c, uint32_t o) {
  return ((const volatile uint8_t *)c)[o];
}
static uint16_t rtl_parity_r16(void *c, uint32_t o) {
  const volatile uint8_t *p = (const volatile uint8_t *)c + o;
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rtl_parity_r32(void *c, uint32_t o) {
  const volatile uint8_t *p = (const volatile uint8_t *)c + o;
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static void rtl_parity_w8(void *c, uint32_t o, uint8_t v) {
  ((volatile uint8_t *)c)[o] = v;
}
static void rtl_parity_w16(void *c, uint32_t o, uint16_t v) {
  volatile uint8_t *p = (volatile uint8_t *)c + o;
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void rtl_parity_w32(void *c, uint32_t o, uint32_t v) {
  volatile uint8_t *p = (volatile uint8_t *)c + o;
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void rtl_parity_seed(void) {
  bytes_zero(rtl_parity_bar, RTL_PARITY_BAR_BYTES);
  rtl_parity_bar[0x00] = 0x70; rtl_parity_bar[0x01] = 0x70;
  rtl_parity_bar[0x02] = 0xfc; rtl_parity_bar[0x03] = 0x0b; /* MAC0 */
  rtl_parity_bar[0x04] = 0xb6; rtl_parity_bar[0x05] = 0x32; /* MAC4 */
  rtl_parity_bar[0x42] = 0x10; rtl_parity_bar[0x43] = 0x64; /* TXCFG 8125B */
  rtl_parity_bar[0x6c] = 0x13;                              /* PHYSTAT: link */
  rtl_parity_bar[0x37] = 0x0c;                              /* CMD: TX|RX */
  rtl_parity_bar[0xd3] = 0x30;                              /* MCUCMD: FIFO empty */
}

static void rtl_parity_fill(uint8_t *at, unsigned bytes, uint8_t value) {
  for (unsigned i = 0; i < bytes; i++) at[i] = value;
}

static void rtl_parity_record(uint8_t *to, const uint8_t *from, unsigned bytes) {
  for (unsigned i = 0; i < bytes; i++) to[i] = from[i];
}

static int rtl_parity_agree(const uint8_t *a, const uint8_t *b, unsigned bytes,
                            unsigned stage) {
  for (unsigned i = 0; i < bytes; i++)
    if (a[i] != b[i]) {
      aiueos_rtl8125_parity_stage = stage;
      aiueos_rtl8125_parity_detail = i;
      return 0;
    }
  return 1;
}

/* The register file this driver leaves behind, written out.
 *
 * It exists so the parity above is not the ONLY evidence.  A comparison
 * between two implementations says they agree; it does not say they are right,
 * and once the C bodies delegate to these objects the comparison would be
 * between an implementation and itself.  This table is checked against BOTH
 * while both still exist, so the day the C goes away it is a claim that was
 * measured against the C rather than transcribed from the Kotoba.
 *
 * Only the fixed bytes are here.  The four ring-address registers hold
 * addresses this kernel chooses at link time and are checked separately, and
 * every byte not named here or in that range must be zero -- which is the half
 * that catches a stray write into a register nobody meant to touch.
 */
struct rtl_parity_pin { uint16_t offset; uint8_t value; };
static const struct rtl_parity_pin rtl_parity_pins[] = {
  {0x00, 0x70}, {0x01, 0x70}, {0x02, 0xfc},     /* MAC0, seeded, never written */
  {0x03, 0x0b}, {0x04, 0xb6}, {0x05, 0x32},     /* MAC4 */
  {0x37, 0x0c},                                 /* RGE_CMD = TX|RX, written last */
  {0x3c, 0xff}, {0x3d, 0xff},                   /* RGE_ISR = 0xffffffff */
  {0x3e, 0xff}, {0x3f, 0xff},
  {0x41, 0x07}, {0x43, 0x03},                   /* RGE_TXCFG = 0x03000700 */
  {0x44, 0x0a}, {0x45, 0x0c}, {0x47, 0x41},     /* RGE_RXCFG = 0x41000c0a */
  {0x6c, 0x13},                                 /* RGE_PHYSTAT, seeded */
  {0xd3, 0x30},                                 /* RGE_MCUCMD, seeded */
  {0xdb, 0x08}                                  /* RGE_RXMAXSIZE = 0x0800 */
};
/* RGE_IMR (0x38) is deliberately absent: it is written, and written to ZERO,
   so the "everything else is zero" sweep is what asserts it. */
#define RTL_PARITY_ADDRESS_REGISTER(offset) \
  (((offset) >= 0x20 && (offset) < 0x28) || ((offset) >= 0xe4 && (offset) < 0xec))

static int rtl_parity_pinned(uint64_t txd, uint64_t rxd, unsigned stage) {
  for (unsigned i = 0; i < sizeof(rtl_parity_pins)/sizeof(rtl_parity_pins[0]); i++)
    if (rtl_parity_bar[rtl_parity_pins[i].offset] != rtl_parity_pins[i].value) {
      aiueos_rtl8125_parity_stage = stage;
      aiueos_rtl8125_parity_detail = rtl_parity_pins[i].offset;
      return 0;
    }
  for (unsigned offset = 0; offset < RTL_PARITY_BAR_BYTES; offset++) {
    unsigned named = 0;
    if (RTL_PARITY_ADDRESS_REGISTER(offset)) continue;
    for (unsigned i = 0; i < sizeof(rtl_parity_pins)/sizeof(rtl_parity_pins[0]); i++)
      if (rtl_parity_pins[i].offset == offset) { named = 1; break; }
    if (!named && rtl_parity_bar[offset]) {
      aiueos_rtl8125_parity_stage = stage + 1;
      aiueos_rtl8125_parity_detail = offset;
      return 0;
    }
  }
  if (rtl_parity_r32(rtl_parity_bar, 0x20) != (uint32_t)txd ||
      rtl_parity_r32(rtl_parity_bar, 0x24) != (uint32_t)(txd >> 32) ||
      rtl_parity_r32(rtl_parity_bar, 0xe4) != (uint32_t)rxd ||
      rtl_parity_r32(rtl_parity_bar, 0xe8) != (uint32_t)(rxd >> 32)) {
    aiueos_rtl8125_parity_stage = stage + 2;
    return 0;
  }
  /* The descriptors, in full.  A transmit descriptor is RGE_DESC_EOR and the
     frame address; a receive descriptor is OWN|EOR|capacity and its own. */
  if (rtl_parity_tx.command != RGE_DESC_EOR ||
      rtl_parity_tx.extension != 0 ||
      rtl_parity_tx.address != (uint64_t)(uintptr_t)rtl_parity_txframe) {
    aiueos_rtl8125_parity_stage = stage + 3;
    return 0;
  }
  if (rtl_parity_rx.command !=
        (RGE_DESC_OWN | RGE_DESC_EOR | AIUEOS_RTL8125_FRAME_CAPACITY) ||
      rtl_parity_rx.extension != 0 ||
      rtl_parity_rx.address != (uint64_t)(uintptr_t)rtl_parity_rxframe ||
      rtl_parity_rx.low0 != 0 || rtl_parity_rx.low1 != 0) {
    aiueos_rtl8125_parity_stage = stage + 4;
    return 0;
  }
  return 1;
}

#define RTL_PARITY_REQUIRE(condition, stage_number)      \
  do {                                                   \
    if (!(condition)) {                                  \
      aiueos_rtl8125_parity_stage = (stage_number);      \
      return 0;                                          \
    }                                                    \
  } while (0)

int aiueos_rtl8125_kotoba_selftest(void) {
  const uint64_t bar = (uint64_t)(uintptr_t)rtl_parity_bar;
  const uint64_t txd = (uint64_t)(uintptr_t)&rtl_parity_tx;
  const uint64_t rxd = (uint64_t)(uintptr_t)&rtl_parity_rx;
  const uint64_t txf = (uint64_t)(uintptr_t)rtl_parity_txframe;
  const uint64_t rxf = (uint64_t)(uintptr_t)rtl_parity_rxframe;
  struct aiueos_rtl8125_io io = {
    rtl_parity_bar, rtl_parity_r8, rtl_parity_r16, rtl_parity_r32,
    rtl_parity_w8, rtl_parity_w16, rtl_parity_w32
  };
  _Alignas(16) uint8_t identity[16];
  uint16_t doorbell;
  uint32_t received;

  aiueos_rtl8125_parity_stage = 0;
  aiueos_rtl8125_parity_detail = 0;

  /* ---- 1. the C takes over, and its register file is recorded ---------- */
  rtl_parity_seed();
  RTL_PARITY_REQUIRE(
    aiueos_rtl8125_takeover(&rtl_parity_device, &io, &rtl_parity_tx, txd,
                            &rtl_parity_rx, rxd, rtl_parity_txframe, txf,
                            rtl_parity_rxframe, rxf) == AIUEOS_RTL8125_OK, 1);
  /* The pinned table, checked against the C WHILE THE C STILL EXISTS.  When
     the bodies above delegate to these objects the comparison below becomes a
     comparison of one implementation with itself; this one does not. */
  if (!rtl_parity_pinned(txd, rxd, 36)) return 0;
  rtl_parity_record(rtl_parity_expect, rtl_parity_bar, RTL_PARITY_BAR_BYTES);
  rtl_parity_record(rtl_parity_tx_expect, (const uint8_t *)&rtl_parity_tx,
                    RTL_PARITY_DESC_BYTES);
  rtl_parity_record(rtl_parity_rx_expect, (const uint8_t *)&rtl_parity_rx,
                    RTL_PARITY_DESC_BYTES);

  /* ---- 2. the objects do it again, from the same seed ------------------ */
  /* The descriptors are filled with 0xff rather than zeroed, so a Kotoba path
     that failed to write a field would leave 0xff where the C left 0 -- a
     zeroed start would let an unwritten field agree with the C by accident. */
  rtl_parity_seed();
  rtl_parity_fill((uint8_t *)&rtl_parity_tx, RTL_PARITY_DESC_BYTES, 0xff);
  rtl_parity_fill((uint8_t *)&rtl_parity_rx, RTL_PARITY_DESC_BYTES, 0xff);
  rtl_parity_fill(identity, sizeof(identity), 0);

  RTL_PARITY_REQUIRE(
    kotoba_aiueos_rtl8125_identify(bar, RTL_PARITY_BAR_BYTES,
                                   (uint64_t)(uintptr_t)identity,
                                   sizeof(identity)) == AIUEOS_RTL8125_OK, 2);
  RTL_PARITY_REQUIRE(identity[0] == (uint8_t)rtl_parity_device.revision &&
                     !identity[1] && !identity[2] && !identity[3], 3);
  if (!rtl_parity_agree(identity + 8, rtl_parity_device.mac, 6, 4)) return 0;
  RTL_PARITY_REQUIRE(
    kotoba_aiueos_rtl8125_link_up(bar, RTL_PARITY_BAR_BYTES) ==
      (uint64_t)(unsigned)aiueos_rtl8125_link_up(&rtl_parity_device), 5);

  /* Stop, then build, then program -- the C's own order, expressed as two
     calls because a Kotoba object cannot call another one. */
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_program(bar, RTL_PARITY_BAR_BYTES,
                                                   0, 0, 0) ==
                     AIUEOS_RTL8125_OK, 6);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_ring_build(
                       txd, RTL_PARITY_DESC_BYTES, txf, 0,
                       AIUEOS_RTL8125_FRAME_CAPACITY) == AIUEOS_RTL8125_OK, 7);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_ring_build(
                       rxd, RTL_PARITY_DESC_BYTES, rxf, 1,
                       AIUEOS_RTL8125_FRAME_CAPACITY) == AIUEOS_RTL8125_OK, 8);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_program(bar, RTL_PARITY_BAR_BYTES,
                                                   txd, rxd, identity[0]) ==
                     AIUEOS_RTL8125_OK, 9);

  /* ---- 3. every byte of the register file and both rings must agree ---- */
  if (!rtl_parity_agree(rtl_parity_bar, rtl_parity_expect,
                        RTL_PARITY_BAR_BYTES, 10)) return 0;
  if (!rtl_parity_agree((const uint8_t *)&rtl_parity_tx, rtl_parity_tx_expect,
                        RTL_PARITY_DESC_BYTES, 11)) return 0;
  if (!rtl_parity_agree((const uint8_t *)&rtl_parity_rx, rtl_parity_rx_expect,
                        RTL_PARITY_DESC_BYTES, 12)) return 0;
  /* And against the objects' own result, so the table is measured twice. */
  if (!rtl_parity_pinned(txd, rxd, 41)) return 0;

  /* ---- 4. transmit submission ------------------------------------------ */
  RTL_PARITY_REQUIRE(aiueos_rtl8125_tx_submit(&rtl_parity_device, 60) ==
                     AIUEOS_RTL8125_OK, 13);
  rtl_parity_record(rtl_parity_tx_expect, (const uint8_t *)&rtl_parity_tx,
                    RTL_PARITY_DESC_BYTES);
  doorbell = rtl_parity_r16(rtl_parity_bar, 0x90);
  RTL_PARITY_REQUIRE(doorbell == 1, 14);

  rtl_parity_w16(rtl_parity_bar, 0x90, 0);
  rtl_parity_fill((uint8_t *)&rtl_parity_tx, RTL_PARITY_DESC_BYTES, 0xff);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_ring_build(
                       txd, RTL_PARITY_DESC_BYTES, txf, 0,
                       AIUEOS_RTL8125_FRAME_CAPACITY) == AIUEOS_RTL8125_OK, 15);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_tx_submit(
                       bar, RTL_PARITY_BAR_BYTES, txd, txf, 60) ==
                     AIUEOS_RTL8125_OK, 16);
  RTL_PARITY_REQUIRE(rtl_parity_r16(rtl_parity_bar, 0x90) == doorbell, 17);
  if (!rtl_parity_agree((const uint8_t *)&rtl_parity_tx, rtl_parity_tx_expect,
                        RTL_PARITY_DESC_BYTES, 18)) return 0;

  /* The descriptor is now owned by the device, so a second submission must
     refuse -- which is only true if the first one really set the OWN bit
     before ringing the doorbell.  Both must refuse, and with the same code. */
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_tx_submit(
                       bar, RTL_PARITY_BAR_BYTES, txd, txf, 60) ==
                     AIUEOS_RTL8125_TX_BUSY, 19);
  RTL_PARITY_REQUIRE(aiueos_rtl8125_tx_submit(&rtl_parity_device, 60) ==
                     AIUEOS_RTL8125_TX_BUSY, 20);

  /* ---- 5. receive completion ------------------------------------------- */
  received = 0xffffffffU;
  RTL_PARITY_REQUIRE(aiueos_rtl8125_rx_poll(&rtl_parity_device, &received) ==
                       AIUEOS_RTL8125_OK && received == 0, 21);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_rx_poll(
                       rxd, AIUEOS_RTL8125_FRAME_CAPACITY) == 0, 22);

  /* A 68-byte completion: 64 bytes of frame plus the four FCS bytes the
     hardware leaves in the buffer.  tests/rtl8125_handoff_model.c:45 uses this
     exact command word and :48 asserts the C reports 64. */
  rtl_parity_rx.command = 0x43000000U | 68U;
  received = 0;
  RTL_PARITY_REQUIRE(aiueos_rtl8125_rx_poll(&rtl_parity_device, &received) ==
                       AIUEOS_RTL8125_OK && received == 64, 23);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_rx_poll(
                       rxd, AIUEOS_RTL8125_FRAME_CAPACITY) ==
                     (int64_t)received, 24);

  /* RGE_RX_ERROR set: refused by both, and the object's negative reason is the
     C's enumerator negated. */
  rtl_parity_rx.command = 0x43100044U;
  RTL_PARITY_REQUIRE(aiueos_rtl8125_rx_poll(&rtl_parity_device, &received) ==
                     AIUEOS_RTL8125_RX_INVALID, 25);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_rx_poll(
                       rxd, AIUEOS_RTL8125_FRAME_CAPACITY) ==
                     -(int64_t)AIUEOS_RTL8125_RX_INVALID, 26);

  aiueos_rtl8125_rx_rearm(&rtl_parity_device);
  rtl_parity_record(rtl_parity_rx_expect, (const uint8_t *)&rtl_parity_rx,
                    RTL_PARITY_DESC_BYTES);
  rtl_parity_fill((uint8_t *)&rtl_parity_rx, RTL_PARITY_DESC_BYTES, 0xff);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_ring_build(
                       rxd, RTL_PARITY_DESC_BYTES, rxf, 1,
                       AIUEOS_RTL8125_FRAME_CAPACITY) == AIUEOS_RTL8125_OK, 27);
  if (!rtl_parity_agree((const uint8_t *)&rtl_parity_rx, rtl_parity_rx_expect,
                        RTL_PARITY_DESC_BYTES, 28)) return 0;

  /* ---- 6. the drain gate, which is the one ordering assertion ---------- */
  /* Seeded so the transmit FIFO never reports empty.  Both implementations
     must spend their budget and refuse, and both must leave RGE_CMD at 0x8c:
     the STOP bit set by the first write and NOT cleared by the write that
     follows a successful drain.  A port that cleared RGE_CMD before waiting,
     or waited before stopping, produces 0x00 or 0x0c here. */
  rtl_parity_seed();
  rtl_parity_bar[0xd3] = 0x00;
  RTL_PARITY_REQUIRE(aiueos_rtl8125_restart(&rtl_parity_device) ==
                     AIUEOS_RTL8125_FIFO_TIMEOUT, 29);
  RTL_PARITY_REQUIRE(rtl_parity_bar[0x37] == 0x8c, 30);

  rtl_parity_seed();
  rtl_parity_bar[0xd3] = 0x00;
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_program(bar, RTL_PARITY_BAR_BYTES,
                                                   0, 0, 0) ==
                     AIUEOS_RTL8125_FIFO_TIMEOUT, 31);
  RTL_PARITY_REQUIRE(rtl_parity_bar[0x37] == 0x8c, 32);

  /* ---- 7. the argument refusals ---------------------------------------- */
  /* A half-page window and a misaligned descriptor.  Both are AIUEOS_RTL8125_
     INVALID, and both leave the model untouched -- the descriptor fill is
     0xff, so a refusal that wrote anything would show. */
  rtl_parity_fill((uint8_t *)&rtl_parity_tx, RTL_PARITY_DESC_BYTES, 0xff);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_identify(
                       bar, 2048, (uint64_t)(uintptr_t)identity,
                       sizeof(identity)) == AIUEOS_RTL8125_INVALID, 33);
  RTL_PARITY_REQUIRE(kotoba_aiueos_rtl8125_ring_build(
                       txd + 16, RTL_PARITY_DESC_BYTES, txf, 0,
                       AIUEOS_RTL8125_FRAME_CAPACITY) ==
                     AIUEOS_RTL8125_INVALID, 34);
  for (unsigned i = 0; i < RTL_PARITY_DESC_BYTES; i++)
    RTL_PARITY_REQUIRE(((const uint8_t *)&rtl_parity_tx)[i] == 0xff, 35);

  return 1;
}
