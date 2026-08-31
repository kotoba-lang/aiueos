/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtl8125.h"

struct fake_mmio { uint8_t bytes[0x1000]; unsigned writes; };
static uint8_t r8(void *p,uint32_t o){return ((struct fake_mmio*)p)->bytes[o];}
static uint16_t r16(void *p,uint32_t o){uint16_t v;memcpy(&v,((struct fake_mmio*)p)->bytes+o,2);return v;}
static uint32_t r32(void *p,uint32_t o){uint32_t v;memcpy(&v,((struct fake_mmio*)p)->bytes+o,4);return v;}
static void w8(void *p,uint32_t o,uint8_t v){struct fake_mmio*m=p;m->bytes[o]=v;m->writes++;}
static void w16(void *p,uint32_t o,uint16_t v){struct fake_mmio*m=p;memcpy(m->bytes+o,&v,2);m->writes++;}
static void w32(void *p,uint32_t o,uint32_t v){struct fake_mmio*m=p;memcpy(m->bytes+o,&v,4);m->writes++;}
static void put16(struct fake_mmio*m,uint32_t o,uint16_t v){memcpy(m->bytes+o,&v,2);}
static void put32(struct fake_mmio*m,uint32_t o,uint32_t v){memcpy(m->bytes+o,&v,4);}
static void be16(uint8_t*p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void be32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line=%d expr=%s\n",__LINE__,#x); return 1; } } while(0)

int main(void) {
  struct fake_mmio mmio={0};
  put32(&mmio,0x00,0x0bfc7070U);put16(&mmio,0x04,0x32b6U);
  put32(&mmio,0x40,0x64100000U);put16(&mmio,0x6c,0x0013U);
  mmio.bytes[0x37]=0x0c;mmio.bytes[0xd3]=0x30;
  struct aiueos_rtl8125_io io={&mmio,r8,r16,r32,w8,w16,w32};
  _Alignas(256) struct aiueos_rtl8125_tx_desc tx;
  _Alignas(256) struct aiueos_rtl8125_rx_desc rx;
  _Alignas(64) uint8_t tx_frame[AIUEOS_RTL8125_FRAME_CAPACITY]={0};
  _Alignas(64) uint8_t rx_frame[AIUEOS_RTL8125_FRAME_CAPACITY]={0};
  struct aiueos_rtl8125 device;
  CHECK(aiueos_rtl8125_takeover(&device,&io,&tx,0x100000,&rx,0x101000,
        tx_frame,0x102000,rx_frame,0x103000)==AIUEOS_RTL8125_OK);
  CHECK(device.ready && device.revision==AIUEOS_RTL8125_REV_8125B);
  CHECK(!memcmp(device.mac,(uint8_t[]){0x70,0x70,0xfc,0x0b,0xb6,0x32},6));
  CHECK(aiueos_rtl8125_link_up(&device));
  CHECK(r32(&mmio,0x20)==0x100000 && r32(&mmio,0xe4)==0x101000);
  CHECK((rx.command&0x80000000U) && rx.address==0x103000);
  CHECK(aiueos_rtl8125_tx_submit(&device,60)==AIUEOS_RTL8125_OK);
  CHECK((tx.command&0xb0000000U)==0xb0000000U && (tx.command&0xffffU)==60);
  CHECK(r16(&mmio,0x90)==1 && !aiueos_rtl8125_tx_complete(&device));
  tx.command&=~0x80000000U;
  CHECK(aiueos_rtl8125_tx_complete(&device));
  for(unsigned i=0;i<64;i++)rx_frame[i]=(uint8_t)i;
  rx.command=0x43000000U|68U;
  uint32_t received=0;
  CHECK(aiueos_rtl8125_rx_poll(&device,&received)==AIUEOS_RTL8125_OK);
  CHECK(received==64);
  aiueos_rtl8125_rx_rearm(&device);
  CHECK((rx.command&0xc0000000U)==0xc0000000U);

  /* Model a late frame and a still-owned TX descriptor from the prior TLS
     four-tuple.  A connection restart must drain and reinstall both rings
     while preserving the takeover-time hardware identity. */
  struct aiueos_rtl8125_io saved_io=device.io;
  enum aiueos_rtl8125_revision saved_revision=device.revision;
  uint8_t saved_mac[6];memcpy(saved_mac,device.mac,sizeof(saved_mac));
  tx.command=0xffffffffU;tx.extension=0xffffffffU;tx.address=0;
  rx.command=0x43000044U;rx.extension=0xffffffffU;rx.address=0;
  CHECK(aiueos_rtl8125_restart(&device)==AIUEOS_RTL8125_OK);
  CHECK(device.ready&&device.revision==saved_revision);
  CHECK(device.io.context==saved_io.context&&!memcmp(device.mac,saved_mac,6));
  CHECK(tx.command==0x40000000U&&tx.extension==0&&tx.address==0x102000);
  CHECK(rx.command==(0xc0000000U|AIUEOS_RTL8125_FRAME_CAPACITY)&&
        rx.extension==0&&rx.address==0x103000);
  CHECK(r32(&mmio,0x20)==0x100000&&r32(&mmio,0xe4)==0x101000);
  CHECK(r32(&mmio,0x40)==0x03000700U&&r32(&mmio,0x44)==0x41000c0aU);
  CHECK(mmio.bytes[0x37]==0x0c&&aiueos_rtl8125_link_up(&device));
  CHECK(mmio.writes>=12);

  const uint8_t local_mac[6]={0x70,0x70,0xfc,0x0b,0xb6,0x32};
  const uint8_t peer_mac[6]={0x80,0x69,0x1a,0x17,0x4f,0x15};
  uint8_t arp[64]={0};
  memset(arp,0xff,6);memcpy(arp+6,peer_mac,6);
  be16(arp+12,0x0806);be16(arp+14,1);be16(arp+16,0x0800);
  arp[18]=6;arp[19]=4;be16(arp+20,1);memcpy(arp+22,peer_mac,6);
  be32(arp+28,0x0a4d0001U);be32(arp+38,0x0a4d000aU);
  CHECK(aiueos_rtl8125_direct_arp_request(
        arp,42,local_mac,peer_mac,0x0a4d000aU,0x0a4d0001U));
  CHECK(!aiueos_rtl8125_direct_arp_request(
        arp,41,local_mac,peer_mac,0x0a4d000aU,0x0a4d0001U));
  arp[21]=2;
  CHECK(!aiueos_rtl8125_direct_arp_request(
        arp,42,local_mac,peer_mac,0x0a4d000aU,0x0a4d0001U));
  arp[21]=1;arp[41]=2;
  CHECK(!aiueos_rtl8125_direct_arp_request(
        arp,42,local_mac,peer_mac,0x0a4d000aU,0x0a4d0001U));
  arp[41]=10;
  CHECK(aiueos_rtl8125_direct_arp_reply(
        arp,sizeof(arp),local_mac,peer_mac,0x0a4d000aU,0x0a4d0001U)==42);
  CHECK(!memcmp(arp,peer_mac,6)&&!memcmp(arp+6,local_mac,6));
  CHECK(arp[20]==0&&arp[21]==2&&!memcmp(arp+22,local_mac,6));
  CHECK(arp[28]==10&&arp[29]==77&&arp[30]==0&&arp[31]==10);
  CHECK(!memcmp(arp+32,peer_mac,6));
  CHECK(arp[38]==10&&arp[39]==77&&arp[40]==0&&arp[41]==1);
  CHECK(aiueos_rtl8125_direct_arp_reply(
        arp,41,local_mac,peer_mac,0x0a4d000aU,0x0a4d0001U)==0);

  put32(&mmio,0x40,0x12300000U);
  CHECK(aiueos_rtl8125_takeover(&device,&io,&tx,0x100000,&rx,0x101000,
        tx_frame,0x102000,rx_frame,0x103000)==AIUEOS_RTL8125_UNSUPPORTED_REVISION);
  puts("AIUEOS_RTL8125_MODEL_OK handoff=pxe rings=1x1 tx=bounded rx=fcs-stripped revision=8125b arp=peer-bound-reply restart=bounded-fifo-flush");
  return 0;
}
