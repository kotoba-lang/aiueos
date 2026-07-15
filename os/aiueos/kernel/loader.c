#include <stdint.h>
#include <stddef.h>

#define PT_LOAD 1U
#define ELF_TEXT_VA 0x1e1000ULL
#define ELF_DATA_VA 0x1e2000ULL
#define PAGE_SIZE 4096ULL
#define USER_CONTEXT_SIZE 88ULL
#define USER_RUNTIME_CALLBACK 0x1e1020ULL

struct __attribute__((packed)) elf_header {
  uint8_t ident[16]; uint16_t type,machine; uint32_t version;
  uint64_t entry,phoff,shoff; uint32_t flags;
  uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx;
};
struct __attribute__((packed)) program_header {
  uint32_t type,flags; uint64_t offset,vaddr,paddr,filesz,memsz,align;
};
extern int aiueos_kotoba_app_object(const uint8_t id[16],const uint8_t **data,uint64_t *length);
extern int aiueos_address_space_map_user_image(unsigned process,
  const uint8_t *text,uint64_t text_size,const uint8_t *data,uint64_t data_size);
extern void *aiueos_address_space_user_data_backing(unsigned process);
static uint64_t loader_evidence;

static int range(uint64_t offset,uint64_t length,uint64_t size) {
  return offset<=size && length<=size-offset;
}
int aiueos_load_object_store_kotoba_process(unsigned process,const uint8_t app_id[16],uint64_t *entry,
                                         uint64_t **result) {
  const uint8_t *image=0; uint64_t size=0;
  if (!aiueos_kotoba_app_object(app_id,&image,&size)) return 0;
  if (size<sizeof(struct elf_header)) return 0;
  const struct elf_header *header=(const struct elf_header *)image;
  if (header->ident[0]!=0x7f || header->ident[1]!='E' || header->ident[2]!='L' ||
      header->ident[3]!='F' || header->ident[4]!=2 || header->ident[5]!=1 ||
      header->type!=2 || header->machine!=0x3e || header->version!=1 ||
      header->ehsize!=sizeof(*header) || header->phentsize!=sizeof(struct program_header) ||
      header->phnum!=2 || header->entry!=ELF_TEXT_VA ||
      !range(header->phoff,2*sizeof(struct program_header),size)) return 0;
  const struct program_header *segments=(const struct program_header *)(image+header->phoff);
  const struct program_header *text=0,*data=0;
  for (unsigned index=0;index<2;index++) {
    const struct program_header *segment=&segments[index];
    if (segment->type!=PT_LOAD || segment->filesz>segment->memsz ||
        segment->memsz>PAGE_SIZE || segment->align!=PAGE_SIZE ||
        !range(segment->offset,segment->filesz,size)) return 0;
    if (segment->vaddr==ELF_TEXT_VA && segment->flags==5 && !text) text=segment;
    else if (segment->vaddr==ELF_DATA_VA && segment->flags==6 && !data) data=segment;
    else return 0;
  }
  if (!text || !data || !text->filesz || data->filesz!=USER_CONTEXT_SIZE ||
      !aiueos_address_space_map_user_image(process,image+text->offset,text->filesz,
        image+data->offset,data->filesz)) return 0;
  const uint8_t *context=image+data->offset;
  uint64_t callback=*(const uint64_t *)(const void *)(context+48);
  uint64_t runtime_handle=*(const uint64_t *)(const void *)(context+80);
  if (*(const uint64_t *)(const void *)context!=0 ||
      *(const uint64_t *)(const void *)(context+8)!=256 || context[16]!=12 ||
      callback!=USER_RUNTIME_CALLBACK || runtime_handle!=0) return 0;
  for (unsigned i=17;i<48;i++) if (context[i]) return 0;
  for (unsigned i=56;i<80;i++) if (context[i]) return 0;
  *entry=header->entry;
  *result=aiueos_address_space_user_data_backing(process);
  loader_evidence=*result!=0;
  return (int)loader_evidence;
}
int aiueos_kotoba_process_loader_evidence_ready(void) { return (int)loader_evidence; }
