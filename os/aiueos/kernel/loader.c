#include <stdint.h>
#include <stddef.h>

#define ELF_TEXT_VA 0x1e1000ULL
#define ELF_TEXT_OFFSET 4096ULL
#define ELF_DATA_OFFSET 8192ULL

extern int aiueos_kotoba_app_object(const uint8_t id[16],const uint8_t **data,uint64_t *length);
extern int aiueos_address_space_map_user_image(unsigned process,
  const uint8_t *text,uint64_t text_size,const uint8_t *data,uint64_t data_size);
extern void *aiueos_address_space_user_data_backing(unsigned process);
extern uint64_t kotoba_aiueos_user_elf_valid(const uint8_t *image,uint64_t length);
extern uint64_t kotoba_aiueos_sha256(const uint8_t *,uint64_t,uint8_t *,uint8_t *,uint64_t);
extern uint64_t kotoba_aiueos_digest_equal(const uint8_t *,const uint8_t *,uint64_t);
static uint64_t loader_evidence;

static uint64_t load_u32(const uint8_t *value) {
  return (uint64_t)value[0] | ((uint64_t)value[1]<<8) |
    ((uint64_t)value[2]<<16) | ((uint64_t)value[3]<<24);
}

static int load_kotoba_process(unsigned process,const uint8_t *image,uint64_t size,
                               uint64_t *entry,uint64_t **result) {
  if (!image || !kotoba_aiueos_user_elf_valid(image,size)) return 0;
  uint64_t text_size=load_u32(image+96);
  if (!aiueos_address_space_map_user_image(process,image+ELF_TEXT_OFFSET,text_size,
        image+ELF_DATA_OFFSET,88)) return 0;
  *entry=ELF_TEXT_VA;
  *result=aiueos_address_space_user_data_backing(process);
  loader_evidence=*result!=0;
  return (int)loader_evidence;
}
int aiueos_load_object_store_kotoba_process(unsigned process,const uint8_t app_id[16],uint64_t *entry,
                                         uint64_t **result) {
  const uint8_t *image=0; uint64_t size=0;
  if (!aiueos_kotoba_app_object(app_id,&image,&size)) return 0;
  return load_kotoba_process(process,image,size,entry,result);
}
#ifdef AIUEOS_PLC_RT_SMOKE
extern const uint8_t aiueos_plc_elf_start[],aiueos_plc_elf_end[];
extern const uint8_t aiueos_plc_elf_sha256[32],aiueos_plc_receipt_sha256[32];
extern const uint8_t aiueos_plc_signature[64],aiueos_plc_public_key[64];
extern uint64_t kotoba_aiueos_ecdsa_p256_sha256_verify(
  const uint8_t *,const uint8_t *,const uint8_t *,uint8_t *,uint64_t);
static uint8_t plc_signature_workspace[2048];
volatile uint64_t aiueos_plc_loader_stage;
int aiueos_load_embedded_plc_process(unsigned process,uint64_t *entry,uint64_t **result) {
  uint8_t actual[32],workspace[512],nonzero=0;
  uint64_t size=(uint64_t)(aiueos_plc_elf_end-aiueos_plc_elf_start);
  aiueos_plc_loader_stage=1;
  if (!size || size>12288) return 0;
  if (!kotoba_aiueos_sha256(aiueos_plc_elf_start,size,actual,workspace,sizeof(workspace))) return 0;
  aiueos_plc_loader_stage=2;
  if (!kotoba_aiueos_digest_equal(actual,aiueos_plc_elf_sha256,32)) return 0;
  aiueos_plc_loader_stage=3;
  uint8_t invalid_signature[64];
  for (unsigned i=0;i<64;i++) invalid_signature[i]=aiueos_plc_signature[i];
  invalid_signature[0]^=1;
  if (kotoba_aiueos_ecdsa_p256_sha256_verify(invalid_signature,actual,
      aiueos_plc_public_key,plc_signature_workspace,sizeof(plc_signature_workspace))) return 0;
  if (!kotoba_aiueos_ecdsa_p256_sha256_verify(aiueos_plc_signature,actual,
      aiueos_plc_public_key,plc_signature_workspace,sizeof(plc_signature_workspace))) return 0;
  aiueos_plc_loader_stage=4;
  for (unsigned i=0;i<32;i++) nonzero|=aiueos_plc_receipt_sha256[i];
  if (!nonzero) return 0;
  aiueos_plc_loader_stage=5;
  if (!kotoba_aiueos_user_elf_valid(aiueos_plc_elf_start,size)) return 0;
  aiueos_plc_loader_stage=6;
  uint64_t text_size=load_u32(aiueos_plc_elf_start+96);
  if (!aiueos_address_space_map_user_image(process,
      aiueos_plc_elf_start+ELF_TEXT_OFFSET,text_size,
      aiueos_plc_elf_start+ELF_DATA_OFFSET,88)) return 0;
  aiueos_plc_loader_stage=7;
  *entry=ELF_TEXT_VA;
  *result=aiueos_address_space_user_data_backing(process);
  return *result!=0;
}
int aiueos_reset_embedded_plc_context(uint64_t *result,uint64_t runtime_handle) {
  if (!result || !runtime_handle) return 0;
  for (unsigned i=0;i<88;i++)
    ((uint8_t *)result)[i]=aiueos_plc_elf_start[ELF_DATA_OFFSET+i];
  result[10]=runtime_handle;
  return 1;
}
#endif
int aiueos_kotoba_process_loader_evidence_ready(void) { return (int)loader_evidence; }
