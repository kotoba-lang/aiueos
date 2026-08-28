#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/job_protocol.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"check failed: %s\n",#x); return 1; } } while(0)

int main(void) {
  static const char job[]="AIUEOS_JOB_V1 boot=0123456789abcdef id=209 kind=aiueos-micro-infer prompt=6d7572616b756d";
  static const char result[]="AIUEOS_JOB_RESULT_V1 boot=0123456789abcdef id=209 model=aiueos-char-bigram-v1 token=6f score=2 total=5";
  static const char commit[]="AIUEOS_JOB_COMMIT_V1 boot=0123456789abcdef id=209 state=recorded";
  struct aiueos_job_request request;
  struct aiueos_micro_infer_result inferred;
  uint8_t out[AIUEOS_JOB_RESULT_CAPACITY];
  CHECK(aiueos_job_request_parse((const uint8_t *)job,sizeof(job)-1,
                                 0x0123456789abcdefULL,&request));
  CHECK(!strcmp((const char *)request.job_id,"209"));
  CHECK(request.prompt_length==7&&!memcmp(request.prompt,"murakum",7));
  CHECK(aiueos_micro_infer_next(request.prompt,request.prompt_length,&inferred));
  uint32_t n=aiueos_job_result_payload(out,sizeof(out),request.boot_nonce,
                                       request.job_id,&inferred);
  CHECK(n==sizeof(result)-1&&!memcmp(out,result,n));
  CHECK(aiueos_job_commit_valid((const uint8_t *)commit,sizeof(commit)-1,
                                request.boot_nonce,request.job_id));
  CHECK(!aiueos_job_request_parse((const uint8_t *)job,sizeof(job)-1,
                                  0xfedcba9876543210ULL,&request));
  CHECK(!aiueos_job_request_parse((const uint8_t *)"AIUEOS_JOB_V1 boot=0123456789abcdef id=x kind=aiueos-micro-infer prompt=61",77,
                                  0x0123456789abcdefULL,&request));
  CHECK(!aiueos_job_commit_valid((const uint8_t *)commit,sizeof(commit)-2,
                                 0x0123456789abcdefULL,(const uint8_t *)"209"));
  puts("AIUEOS_JOB_PROTOCOL_MODEL_OK request=boot+job-bound result=model+score commit=persisted");
  return 0;
}
