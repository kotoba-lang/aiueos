/* SPDX-License-Identifier: Apache-2.0 */
#include "job_protocol.h"

static uint32_t text_length(const uint8_t *text,uint32_t bound) {
  uint32_t n=0;
  if(!text)return 0;
  while(n<bound&&text[n])n++;
  return n;
}

static int take_text(
    const uint8_t *payload,uint32_t length,uint32_t *at,const char *text) {
  while(*text) {
    if(*at>=length||payload[*at]!=(uint8_t)*text)return 0;
    (*at)++;text++;
  }
  return 1;
}

static int hex_digit(uint8_t value) {
  if(value>='0'&&value<='9')return value-'0';
  if(value>='a'&&value<='f')return value-'a'+10;
  return -1;
}

static int take_hex64(
    const uint8_t *payload,uint32_t length,uint32_t *at,uint64_t *value) {
  uint64_t result=0;
  for(unsigned i=0;i<16;i++) {
    if(*at>=length)return 0;
    int digit=hex_digit(payload[(*at)++]);
    if(digit<0)return 0;
    result=(result<<4)|(unsigned)digit;
  }
  *value=result;return 1;
}

static uint32_t append_text(
    uint8_t *out,uint32_t capacity,uint32_t at,const char *text) {
  while(*text) {
    if(at>=capacity)return capacity+1U;
    out[at++]=(uint8_t)*text++;
  }
  return at;
}

static uint32_t append_bytes(
    uint8_t *out,uint32_t capacity,uint32_t at,const uint8_t *bytes,uint32_t n) {
  if(!bytes||at+n>capacity)return capacity+1U;
  for(uint32_t i=0;i<n;i++)out[at++]=bytes[i];
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

static uint32_t append_hex8(
    uint8_t *out,uint32_t capacity,uint32_t at,uint8_t value) {
  static const uint8_t hex[]="0123456789abcdef";
  if(at+2U>capacity)return capacity+1U;
  out[at++]=hex[value>>4];out[at++]=hex[value&15U];
  return at;
}

static uint32_t append_decimal(
    uint8_t *out,uint32_t capacity,uint32_t at,uint32_t value) {
  uint8_t digits[10];unsigned n=0;
  do { digits[n++]=(uint8_t)('0'+value%10U);value/=10U; } while(value&&n<10);
  if(at+n>capacity)return capacity+1U;
  while(n)out[at++]=digits[--n];
  return at;
}

int aiueos_job_request_parse(
    const uint8_t *payload,uint32_t length,uint64_t expected_boot,
    struct aiueos_job_request *request) {
  if(!payload||!request||!length||length>AIUEOS_JOB_REQUEST_CAPACITY)return 0;
  uint32_t at=0,id_length=0,prompt_length=0;
  uint64_t boot=0;
  if(!take_text(payload,length,&at,"AIUEOS_JOB_V1 boot=")||
     !take_hex64(payload,length,&at,&boot)||boot!=expected_boot||
     !take_text(payload,length,&at," id="))return 0;
  while(at<length&&payload[at]>='0'&&payload[at]<='9') {
    if(id_length>=AIUEOS_JOB_ID_MAX)return 0;
    request->job_id[id_length++]=payload[at++];
  }
  if(!id_length)return 0;
  request->job_id[id_length]=0;
  if(!take_text(payload,length,&at," kind=aiueos-micro-infer prompt="))return 0;
  while(at<length) {
    if(prompt_length>=AIUEOS_MICRO_INFER_PROMPT_MAX||at+2U>length)return 0;
    int high=hex_digit(payload[at++]),low=hex_digit(payload[at++]);
    if(high<0||low<0)return 0;
    request->prompt[prompt_length++]=(uint8_t)((high<<4)|low);
  }
  if(!prompt_length)return 0;
  request->boot_nonce=boot;request->prompt_length=prompt_length;
  return 1;
}

uint32_t aiueos_job_result_payload(
    uint8_t *out,uint32_t capacity,uint64_t boot_nonce,const uint8_t *job_id,
    const struct aiueos_micro_infer_result *result) {
  uint32_t id_length=text_length(job_id,AIUEOS_JOB_ID_MAX+1U);
  if(!out||!capacity||!result||!id_length||id_length>AIUEOS_JOB_ID_MAX)return 0;
  for(uint32_t i=0;i<id_length;i++)if(job_id[i]<'0'||job_id[i]>'9')return 0;
  uint32_t n=append_text(out,capacity,0,"AIUEOS_JOB_RESULT_V1 boot=");
  n=append_hex64(out,capacity,n,boot_nonce);
  n=append_text(out,capacity,n," id=");
  n=append_bytes(out,capacity,n,job_id,id_length);
  n=append_text(out,capacity,n," model=" AIUEOS_MICRO_INFER_MODEL " token=");
  n=append_hex8(out,capacity,n,result->token);
  n=append_text(out,capacity,n," score=");
  n=append_decimal(out,capacity,n,result->score);
  n=append_text(out,capacity,n," total=");
  n=append_decimal(out,capacity,n,result->total);
  return n<=capacity?n:0;
}

int aiueos_job_commit_valid(
    const uint8_t *payload,uint32_t length,uint64_t boot_nonce,
    const uint8_t *job_id) {
  uint8_t expected[AIUEOS_JOB_COMMIT_CAPACITY];
  uint32_t id_length=text_length(job_id,AIUEOS_JOB_ID_MAX+1U);
  if(!payload||!job_id||!id_length||id_length>AIUEOS_JOB_ID_MAX)return 0;
  uint32_t n=append_text(expected,sizeof(expected),0,"AIUEOS_JOB_COMMIT_V1 boot=");
  n=append_hex64(expected,sizeof(expected),n,boot_nonce);
  n=append_text(expected,sizeof(expected),n," id=");
  n=append_bytes(expected,sizeof(expected),n,job_id,id_length);
  n=append_text(expected,sizeof(expected),n," state=recorded");
  if(n>sizeof(expected)||n!=length)return 0;
  for(uint32_t i=0;i<n;i++)if(expected[i]!=payload[i])return 0;
  return 1;
}

int aiueos_node_ping_parse(
    const uint8_t *payload,uint32_t length,uint64_t expected_boot,
    uint32_t *sequence) {
  if(!payload||!sequence||!length||length>AIUEOS_NODE_LIVENESS_CAPACITY)return 0;
  uint32_t at=0,value=0,digits=0;uint64_t boot=0;
  if(!take_text(payload,length,&at,"AIUEOS_NODE_PING_V1 boot=")||
     !take_hex64(payload,length,&at,&boot)||boot!=expected_boot||
     !take_text(payload,length,&at," seq="))return 0;
  while(at<length&&payload[at]>='0'&&payload[at]<='9') {
    uint32_t digit=(uint32_t)(payload[at++]-'0');
    if(digits++>=10U||value>429496729U||
       (value==429496729U&&digit>5U))return 0;
    value=value*10U+digit;
  }
  if(!digits||at!=length)return 0;
  *sequence=value;return 1;
}

uint32_t aiueos_node_pong_payload(
    uint8_t *out,uint32_t capacity,uint64_t boot_nonce,uint32_t sequence) {
  if(!out||!capacity)return 0;
  uint32_t n=append_text(out,capacity,0,"AIUEOS_NODE_PONG_V1 boot=");
  n=append_hex64(out,capacity,n,boot_nonce);
  n=append_text(out,capacity,n," seq=");
  n=append_decimal(out,capacity,n,sequence);
  n=append_text(out,capacity,n," state=ready");
  return n<=capacity?n:0;
}
