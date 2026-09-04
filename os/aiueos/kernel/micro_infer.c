/* SPDX-License-Identifier: Apache-2.0 */
#include "micro_infer.h"

/* Frozen transition counts for the qualification corpus whose SHA-256 is
   6433aeadc179877f103fdc87672d967d5c68d3f531f8b2bf156e377ad84058cb.
   Vocabulary index 0 is space and 1..26 are a..z.  The model is intentionally
   tiny: it proves that the physical K16 executed a deterministic inference
   job, not that the production LLM/GPU runtime is qualified. */
static const uint8_t transitions[27][27]={
  {0,3,0,0,0,0,0,0,0,3,1,0,0,2,2,1,0,0,0,1,0,0,0,0,0,0,0},
  {1,0,0,0,0,0,0,0,0,2,0,2,0,0,1,0,0,0,0,0,2,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {2,0,0,0,0,0,0,0,0,0,0,0,0,1,1,2,0,0,2,0,1,0,0,0,0,0,0},
  {0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,2,0,2,1,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,2,0,0,0,0,0},
  {1,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0},
  {2,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,1,2,0,0,0,0,0,0,0},
  {0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,3,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0},
  {0,0,0,0,0,1,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0},
  {0,0,0,0,0,2,0,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static int vocabulary_index(uint8_t value) {
  if(value==' ')return 0;
  if(value>='a'&&value<='z')return (int)(value-'a')+1;
  return -1;
}

static uint8_t vocabulary_value(unsigned index) {
  return index? (uint8_t)('a'+index-1U):(uint8_t)' ';
}

int aiueos_micro_infer_next(
    const uint8_t *prompt,uint32_t length,
    struct aiueos_micro_infer_result *result) {
  if(!prompt||!result||!length||length>AIUEOS_MICRO_INFER_PROMPT_MAX)return 0;
  int input=-1;
  for(uint32_t i=0;i<length;i++) {
    input=vocabulary_index(prompt[i]);
    if(input<0)return 0;
  }
  unsigned best=0,total=0;
  uint8_t score=transitions[input][0];
  for(unsigned i=0;i<27;i++) {
    uint8_t count=transitions[input][i];
    total+=count;
    if(count>score) { score=count;best=i; }
  }
  if(!total||!score)return 0;
  *result=(struct aiueos_micro_infer_result){
    vocabulary_value(best),(uint8_t)input,(uint8_t)best,score,(uint16_t)total};
  return 1;
}
