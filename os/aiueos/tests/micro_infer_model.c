#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/micro_infer.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"check failed: %s\n",#x); return 1; } } while(0)

int main(void) {
  struct aiueos_micro_infer_result result;
  static const uint8_t prompt[]="murakum";
  CHECK(aiueos_micro_infer_next(prompt,sizeof(prompt)-1,&result));
  CHECK(result.token=='o'&&result.input_index==13&&result.output_index==15);
  CHECK(result.score==2&&result.total==5);
  CHECK(!aiueos_micro_infer_next((const uint8_t *)"M",1,&result));
  CHECK(!aiueos_micro_infer_next((const uint8_t *)"b",1,&result));
  CHECK(!aiueos_micro_infer_next(prompt,0,&result));
  CHECK(!aiueos_micro_infer_next(prompt,AIUEOS_MICRO_INFER_PROMPT_MAX+1,&result));
  CHECK(!aiueos_micro_infer_next(0,sizeof(prompt)-1,&result));
  puts("AIUEOS_MICRO_INFER_MODEL_OK model=aiueos-char-bigram-v1 prompt=murakum token=o score=2/5");
  return 0;
}
