#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/relay_protocol.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"check failed: %s\n",#x); return 1; } } while(0)

int main(void) {
  static const uint8_t mac[6]={0x70,0x70,0xfc,0x0b,0xb6,0x32};
  static const char hello[]="AIUEOS_NODE_HELLO_V1 boot=0123456789abcdef mac=70-70-fc-0b-b6-32 profile=rtl8125-relay-test";
  static const char ack[]="AIUEOS_NODE_ACK_V1 boot=0123456789abcdef state=accepted";
  uint8_t buffer[AIUEOS_RELAY_HELLO_CAPACITY];
  uint32_t n=aiueos_relay_hello_payload(buffer,sizeof(buffer),0x0123456789abcdefULL,mac);
  CHECK(n==sizeof(hello)-1&&memcmp(buffer,hello,n)==0);
  n=aiueos_relay_ack_payload(buffer,sizeof(buffer),0x0123456789abcdefULL);
  CHECK(n==sizeof(ack)-1&&memcmp(buffer,ack,n)==0);
  CHECK(aiueos_relay_ack_payload_valid(buffer,n,0x0123456789abcdefULL));
  buffer[n-1]^=1;
  CHECK(!aiueos_relay_ack_payload_valid(buffer,n,0x0123456789abcdefULL));
  CHECK(!aiueos_relay_ack_payload_valid((const uint8_t *)ack,n-1,0x0123456789abcdefULL));
  CHECK(!aiueos_relay_ack_payload_valid((const uint8_t *)ack,n,0xfedcba9876543210ULL));
  CHECK(!aiueos_relay_hello_payload(buffer,8,0,mac));
  CHECK(!aiueos_relay_ack_payload(buffer,8,0));
  puts("AIUEOS_RELAY_PROTOCOL_MODEL_OK hello=mac+nonce ack=request-bound malformed=refused");
  return 0;
}
