/* SPDX-License-Identifier: Apache-2.0 */
#include "relay_protocol.h"

static uint32_t append_text(
    uint8_t *out,uint32_t capacity,uint32_t at,const char *text) {
  while(*text) {
    if(at>=capacity)return capacity+1U;
    out[at++]=(uint8_t)*text++;
  }
  return at;
}

static uint32_t append_hex64(
    uint8_t *out,uint32_t capacity,uint32_t at,uint64_t value) {
  static const uint8_t hex[]="0123456789abcdef";
  for(unsigned i=0;i<16;i++) {
    if(at>=capacity)return capacity+1U;
    out[at++]=hex[(value>>(60U-4U*i))&15U];
  }
  return at;
}

static uint32_t append_mac(
    uint8_t *out,uint32_t capacity,uint32_t at,const uint8_t mac[6]) {
  static const uint8_t hex[]="0123456789abcdef";
  if(!mac)return capacity+1U;
  for(unsigned i=0;i<6;i++) {
    if(i) { if(at>=capacity)return capacity+1U;out[at++]='-'; }
    if(at+2U>capacity)return capacity+1U;
    out[at++]=hex[mac[i]>>4];out[at++]=hex[mac[i]&15U];
  }
  return at;
}

uint32_t aiueos_relay_hello_payload(
    uint8_t *out,uint32_t capacity,uint64_t nonce,const uint8_t mac[6]) {
  if(!out||!capacity||!mac)return 0;
  uint32_t n=append_text(out,capacity,0,"AIUEOS_NODE_HELLO_V1 boot=");
  n=append_hex64(out,capacity,n,nonce);
  n=append_text(out,capacity,n," mac=");
  n=append_mac(out,capacity,n,mac);
  n=append_text(out,capacity,n," profile=rtl8125-relay-test");
  return n<=capacity?n:0;
}

uint32_t aiueos_relay_ack_payload(
    uint8_t *out,uint32_t capacity,uint64_t nonce) {
  if(!out||!capacity)return 0;
  uint32_t n=append_text(out,capacity,0,"AIUEOS_NODE_ACK_V1 boot=");
  n=append_hex64(out,capacity,n,nonce);
  n=append_text(out,capacity,n," state=accepted");
  return n<=capacity?n:0;
}

int aiueos_relay_ack_payload_valid(
    const uint8_t *payload,uint32_t length,uint64_t nonce) {
  uint8_t expected[AIUEOS_RELAY_ACK_CAPACITY];
  uint32_t expected_length=aiueos_relay_ack_payload(
    expected,sizeof(expected),nonce);
  if(!payload||!expected_length||length!=expected_length)return 0;
  for(uint32_t i=0;i<length;i++)if(payload[i]!=expected[i])return 0;
  return 1;
}
