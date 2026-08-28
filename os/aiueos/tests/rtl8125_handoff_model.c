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
  CHECK(mmio.writes>=12);

  put32(&mmio,0x40,0x12300000U);
  CHECK(aiueos_rtl8125_takeover(&device,&io,&tx,0x100000,&rx,0x101000,
        tx_frame,0x102000,rx_frame,0x103000)==AIUEOS_RTL8125_UNSUPPORTED_REVISION);
  puts("AIUEOS_RTL8125_MODEL_OK handoff=pxe rings=1x1 tx=bounded rx=fcs-stripped revision=8125b");
  return 0;
}
