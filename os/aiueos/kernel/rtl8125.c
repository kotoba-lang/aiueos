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

  /* Quiesce the firmware-owned rings but retain its PHY/MCU calibration. */
  io->write8(io->context,RGE_CMD,
             (uint8_t)(io->read8(io->context,RGE_CMD)|RGE_CMD_STOP));
  unsigned budget;
  for (budget=0;budget<300000U;budget++)
    if ((io->read8(io->context,RGE_MCUCMD)&RGE_FIFO_EMPTY)==RGE_FIFO_EMPTY)
      break;
  if (budget==300000U) return AIUEOS_RTL8125_FIFO_TIMEOUT;
  io->write8(io->context,RGE_CMD,0);

  bytes_zero(tx,sizeof(*tx));
  bytes_zero(rx,sizeof(*rx));
  tx->command = RGE_DESC_EOR;
  tx->address = tx_frame_physical;
  rx->address = rx_frame_physical;
  rx->command = RGE_DESC_OWN|RGE_DESC_EOR|AIUEOS_RTL8125_FRAME_CAPACITY;
  dma_release();

  io->write32(io->context,RGE_IMR,0);
  io->write32(io->context,RGE_ISR,0xffffffffU);
  io->write32(io->context,RGE_TXDESC_LO,(uint32_t)tx_desc_physical);
  io->write32(io->context,RGE_TXDESC_HI,(uint32_t)(tx_desc_physical>>32));
  io->write32(io->context,RGE_RXDESC_LO,(uint32_t)rx_desc_physical);
  io->write32(io->context,RGE_RXDESC_HI,(uint32_t)(rx_desc_physical>>32));
  io->write16(io->context,RGE_RXMAXSIZE,AIUEOS_RTL8125_FRAME_CAPACITY);
  io->write32(io->context,RGE_TXCFG,RGE_TXCFG_CONFIG);
  io->write32(io->context,RGE_RXCFG,receive_config(revision)|
              RGE_RXCFG_INDIVIDUAL|RGE_RXCFG_BROADCAST);
  io->write8(io->context,RGE_CMD,RGE_CMD_TX|RGE_CMD_RX);

  *device=(struct aiueos_rtl8125){0};
  device->io=*io;device->tx=tx;device->rx=rx;
  device->tx_frame=tx_frame;device->rx_frame=rx_frame;
  device->tx_desc_physical=tx_desc_physical;
  device->rx_desc_physical=rx_desc_physical;
  device->tx_frame_physical=tx_frame_physical;
  device->rx_frame_physical=rx_frame_physical;
  for (unsigned i=0;i<6;i++) device->mac[i]=mac[i];
  device->revision=revision;device->ready=1;
  return AIUEOS_RTL8125_OK;
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
