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
static uint64_t loader_evidence;

static uint64_t load_u32(const uint8_t *value) {
  return (uint64_t)value[0] | ((uint64_t)value[1]<<8) |
    ((uint64_t)value[2]<<16) | ((uint64_t)value[3]<<24);
}

int aiueos_load_object_store_kotoba_process(unsigned process,const uint8_t app_id[16],uint64_t *entry,
                                         uint64_t **result) {
  const uint8_t *image=0; uint64_t size=0;
  if (!aiueos_kotoba_app_object(app_id,&image,&size) ||
      !kotoba_aiueos_user_elf_valid(image,size)) return 0;
  uint64_t text_size=load_u32(image+96);
  if (!aiueos_address_space_map_user_image(process,image+ELF_TEXT_OFFSET,text_size,
        image+ELF_DATA_OFFSET,88)) return 0;
  *entry=ELF_TEXT_VA;
  *result=aiueos_address_space_user_data_backing(process);
  loader_evidence=*result!=0;
  return (int)loader_evidence;
}
int aiueos_kotoba_process_loader_evidence_ready(void) { return (int)loader_evidence; }
