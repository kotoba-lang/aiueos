#include <stdint.h>
#include <stddef.h>

#define PT_LOAD 1U
#define ELF_TEXT_VA 0x1e1000ULL
#define ELF_DATA_VA 0x1e2000ULL
#define PAGE_SIZE 4096ULL

struct __attribute__((packed)) elf_header {
  uint8_t ident[16]; uint16_t type,machine; uint32_t version;
  uint64_t entry,phoff,shoff; uint32_t flags;
  uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx;
};
struct __attribute__((packed)) program_header {
  uint32_t type,flags; uint64_t offset,vaddr,paddr,filesz,memsz,align;
};
extern const uint8_t aiueos_kotoba_user_elf_start[],aiueos_kotoba_user_elf_end[];
extern int aiueos_address_space_map_user_image(unsigned process,
  const uint8_t *text,uint64_t text_size,const uint8_t *data,uint64_t data_size);
extern void *aiueos_address_space_user_data_backing(unsigned process);
static uint64_t loader_evidence;

static int range(uint64_t offset,uint64_t length,uint64_t size) {
  return offset<=size && length<=size-offset;
}
int aiueos_load_embedded_kotoba_process(unsigned process,uint64_t *entry,
                                         uint64_t **result) {
  const uint8_t *image=aiueos_kotoba_user_elf_start;
  uint64_t size=(uint64_t)(aiueos_kotoba_user_elf_end-image);
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
  if (!text || !data || !text->filesz || !data->filesz ||
      !aiueos_address_space_map_user_image(process,image+text->offset,text->filesz,
        image+data->offset,data->filesz)) return 0;
  *entry=header->entry;
  *result=aiueos_address_space_user_data_backing(process);
  loader_evidence=*result!=0;
  return (int)loader_evidence;
}
int aiueos_kotoba_process_loader_evidence_ready(void) { return (int)loader_evidence; }
