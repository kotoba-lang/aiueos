/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIUEOS_RELAY_PROTOCOL_H
#define AIUEOS_RELAY_PROTOCOL_H

#include <stdint.h>

#define AIUEOS_RELAY_HELLO_CAPACITY 160U
#define AIUEOS_RELAY_ACK_CAPACITY 96U

uint32_t aiueos_relay_hello_payload(
  uint8_t *out,uint32_t capacity,uint64_t nonce,const uint8_t mac[6]);
uint32_t aiueos_relay_ack_payload(
  uint8_t *out,uint32_t capacity,uint64_t nonce);
int aiueos_relay_ack_payload_valid(
  const uint8_t *payload,uint32_t length,uint64_t nonce);

#endif
