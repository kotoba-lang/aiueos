#include <stdint.h>

#define ABI 0
#define LOG 1
#define EXIT 2
#define TRANSFER 3
#define CLAIM 4
#define ABI_V1 0x00010000ULL
#define BAD_HANDLE ((uint64_t)-9)
#define BAD_POINTER ((uint64_t)-14)
#define TOO_BIG ((uint64_t)-7)

struct __attribute__((packed)) tss64 {
  uint32_t reserved0; uint64_t rsp0, rsp1, rsp2; uint64_t reserved1;
  uint64_t ist[7]; uint64_t reserved2; uint16_t reserved3, iomap;
};
struct user_result {
  uint64_t handle, foreign_handle, domain;
  uint64_t abi, valid, too_big, stale, foreign_owner, wrong_type, no_rights;
  uint64_t bad_pointer, transfer, transfer_escalation, claimed;
  uint64_t transferred_valid, completed, scheduled_runs;
  uint64_t foreign_page;
  char message[8];
};

extern uint64_t aiueos_gdt_tss[2];
extern uint8_t aiueos_kernel_stack_top[];
extern void aiueos_syscall_entry(void);
extern uint64_t aiueos_sysret_count, aiueos_sysret_validation_count;
extern void aiueos_enter_user(void (*entry)(void), void *stack);
extern int aiueos_user_mapping_verify(void);
extern uint64_t aiueos_syscall_from_user;
extern volatile uint16_t aiueos_current_user_domain;
extern int aiueos_address_spaces_initialize(void);
extern uint64_t aiueos_address_space_enter(unsigned process);
extern void aiueos_address_space_leave(void);
extern uint64_t aiueos_address_space_private_va(unsigned process);
extern void *aiueos_address_space_private_backing(unsigned process);
extern uint64_t aiueos_capability_log_handle(uint16_t owner);
extern uint64_t aiueos_capability_ensure_log_handle(uint16_t owner);
extern uint64_t aiueos_capability_ensure_runtime_handle(uint16_t owner);
extern int aiueos_scheduler_begin_user_runtime(void);
extern int aiueos_scheduler_create_user_task(unsigned address_space,uint16_t domain,
  void (*entry)(uint64_t),uint64_t argument,uint64_t user_stack);
extern int aiueos_user_scheduler_evidence_ready(void);
extern void aiueos_scheduler_request_user_exit(uint16_t domain);
extern int aiueos_scheduler_users_reaped(void);
extern int aiueos_scheduler_finalize_user_stacks(void);
extern int aiueos_scheduler_reap_evidence_ready(void);
extern unsigned aiueos_scheduler_task_capacity(void);
extern int aiueos_scheduler_task_slot_self_test(void);
extern uint64_t aiueos_capability_revoke_owner(uint16_t owner);
extern int aiueos_address_space_reclaim(unsigned process);
extern int aiueos_address_space_reuse(unsigned process);
extern uint64_t aiueos_physical_allocator_reuse_count(void);
extern unsigned aiueos_address_space_capacity(void);
extern int aiueos_address_space_slot_self_test(void);
extern int aiueos_address_space_claim(void);
extern int aiueos_address_space_user_entry_valid(unsigned process,uint64_t entry);
extern int aiueos_load_object_store_kotoba_process(unsigned process,const uint8_t app_id[16],uint64_t *entry,uint64_t **result);
extern int aiueos_kotoba_process_loader_evidence_ready(void);
extern uint8_t aiueos_user_text_start[],aiueos_user_text_end[];
extern void aiueos_probe_cross_process(const void *address);
extern volatile uint64_t aiueos_page_fault_stage, aiueos_page_fault_error;
static struct tss64 tss;
uint64_t aiueos_syscall_kernel_stack_top;
static uint64_t syscall_transport_evidence;
__attribute__((section(".user.data"), aligned(4096), used))
static uint8_t user_mapping_anchor[4096];
static struct user_result *kernel_results[2];
#define PROCESS_CAPACITY 8U
struct process_descriptor {
  uint64_t entry, argument, user_stack;
  uint16_t generation, domain, address_space, task_slot;
  uint8_t active;
  struct user_result *result;
};
static struct process_descriptor processes[PROCESS_CAPACITY];
static uint64_t process_lifecycle_evidence;
static int process_results_valid;
static int catalog_lookup_rejection_evidence;
static const uint8_t hello_app_id[16]="app/hello";
static const uint8_t worker_app_id[16]="app/worker";
static const uint8_t missing_app_id[16]="app/missing";
int aiueos_process_result(void);
void aiueos_process_set_kernel_stack(uint64_t top) {
  tss.rsp0=top; aiueos_syscall_kernel_stack_top=top;
}

static uint64_t read_msr(uint32_t msr) {
  uint32_t lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(msr));
  return ((uint64_t)hi<<32)|lo;
}
static void write_msr(uint32_t msr,uint64_t value) {
  __asm__ volatile("wrmsr"::"c"(msr),"a"((uint32_t)value),"d"((uint32_t)(value>>32)));
}
static int syscall_transport_initialize(void) {
  uint32_t eax=0x80000000U,ebx,ecx,edx;
  __asm__ volatile("cpuid":"+a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx));
  if (eax<0x80000001U) return 0;
  eax=0x80000001U;
  __asm__ volatile("cpuid":"+a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx));
  if (!(edx&(1U<<11))) return 0;
  uint64_t star=(0x10ULL<<48)|(0x08ULL<<32);
  write_msr(0xc0000080U,read_msr(0xc0000080U)|1U);
  write_msr(0xc0000081U,star);
  write_msr(0xc0000082U,(uint64_t)(uintptr_t)aiueos_syscall_entry);
  write_msr(0xc0000084U,0x47700U);
  aiueos_sysret_count=aiueos_sysret_validation_count=0;
  syscall_transport_evidence=(read_msr(0xc0000080U)&1U) &&
    read_msr(0xc0000081U)==star &&
    read_msr(0xc0000082U)==(uint64_t)(uintptr_t)aiueos_syscall_entry &&
    read_msr(0xc0000084U)==0x47700U;
  return (int)syscall_transport_evidence;
}
int aiueos_syscall_transport_evidence_ready(void) {
  return syscall_transport_evidence && aiueos_sysret_count>=18 &&
    aiueos_sysret_validation_count==aiueos_sysret_count;
}

static inline uint64_t call(uint64_t n, uint64_t h, const void *p, uint64_t l) {
  register uint64_t a __asm__("rax")=n;
  __asm__ volatile("syscall" : "+a"(a) : "D"(h), "S"(p), "d"(l) :
                   "rcx","r11","r8","r9","r10","memory");
  return a;
}

__attribute__((section(".user.text"), noreturn))
static void user_run(struct user_result *r, const void *foreign_page) {
  r->abi = call(ABI,0,0,0);
  r->valid = call(LOG,r->handle,r->message,5);
  r->too_big = call(LOG,r->handle,r,257);
  r->stale = call(LOG,r->handle ^ 0x10000ULL,r->message,1);
  r->foreign_owner = call(LOG,r->foreign_handle,r->message,1);
  r->wrong_type = call(LOG,r->handle ^ 0x100000000ULL,r->message,1);
  r->no_rights = call(LOG,r->handle ^ 0x1000000000000ULL,r->message,1);
  r->bad_pointer = call(LOG,r->handle,foreign_page,1);
  if (r->domain==2) {
    r->transfer_escalation=call(TRANSFER,r->handle,(void *)3,2);
    r->transfer=call(TRANSFER,r->handle,(void *)3,1);
  } else {
    do { r->claimed=call(CLAIM,0,0,0); }
    while (r->claimed==BAD_HANDLE);
    r->transferred_valid=call(LOG,r->claimed,r->message,5);
  }
  r->completed = 1;
  for (;;) { r->scheduled_runs++; __asm__ volatile("pause"); }
}
__attribute__((section(".user.text"), noreturn))
static void user_entry(uint64_t argument) {
  struct user_result *result=(struct user_result *)(uintptr_t)argument;
  user_run(result,(const void *)(uintptr_t)result->foreign_page);
}

int aiueos_process_address_space_for_domain(uint16_t domain) {
  for (unsigned slot=0;slot<PROCESS_CAPACITY;slot++)
    if (processes[slot].active && processes[slot].domain==domain)
      return processes[slot].address_space;
  return -1;
}
static int process_create_in_space(void (*entry)(uint64_t),uint16_t domain,int supplied_space) {
  uint64_t address=(uint64_t)(uintptr_t)entry;
  if (domain<2 || aiueos_process_address_space_for_domain(domain)>=0) return -1;
  unsigned descriptor;
  for (descriptor=0;descriptor<PROCESS_CAPACITY;descriptor++)
    if (!processes[descriptor].active) break;
  if (descriptor==PROCESS_CAPACITY) return -1;
  int address_space=supplied_space>=0 ? supplied_space : aiueos_address_space_claim();
  if (address_space<0) return -1;
  int linked_entry=address>=(uint64_t)(uintptr_t)aiueos_user_text_start &&
    address<(uint64_t)(uintptr_t)aiueos_user_text_end;
  if (!linked_entry && !aiueos_address_space_user_entry_valid((unsigned)address_space,address)) {
    aiueos_address_space_reclaim((unsigned)address_space); return -1;
  }
  struct user_result *result=aiueos_address_space_private_backing((unsigned)address_space);
  uint64_t private_va=aiueos_address_space_private_va((unsigned)address_space);
  uint64_t handle=aiueos_capability_ensure_log_handle(domain);
  if (!result || !private_va || !handle) { aiueos_address_space_reclaim((unsigned)address_space); return -1; }
  result->handle=handle; result->domain=domain;
  result->message[0]='r'; result->message[1]='i'; result->message[2]='n';
  result->message[3]='g'; result->message[4]=(char)('1'+descriptor); result->message[5]=0;
  struct process_descriptor *p=&processes[descriptor];
  p->generation++; if (!p->generation) p->generation=1;
  p->entry=address; p->argument=private_va; p->user_stack=private_va+4096;
  p->domain=domain; p->address_space=(uint16_t)address_space; p->result=result; p->active=1;
  int task=aiueos_scheduler_create_user_task((unsigned)address_space,domain,entry,
    p->argument,p->user_stack);
  if (task<1) { p->active=0; aiueos_address_space_reclaim((unsigned)address_space); return -1; }
  p->task_slot=(uint16_t)task;
  return (int)descriptor;
}
static int process_create(void (*entry)(uint64_t),uint16_t domain) {
  return process_create_in_space(entry,domain,-1);
}
static int process_create_kotoba_elf(const uint8_t app_id[16],uint16_t domain,uint64_t **result) {
  int address_space=aiueos_address_space_claim();
  uint64_t entry=0;
  if (address_space<0 || !aiueos_load_object_store_kotoba_process(
      (unsigned)address_space,app_id,&entry,result)) {
    if (address_space>=0) aiueos_address_space_reclaim((unsigned)address_space);
    return -1;
  }
  uint64_t runtime_handle=aiueos_capability_ensure_runtime_handle(domain);
  if (!runtime_handle || !*result) {
    aiueos_address_space_reclaim((unsigned)address_space); return -1;
  }
  (*result)[10]=runtime_handle;
  return process_create_in_space((void (*)(uint64_t))(uintptr_t)entry,domain,address_space);
}

int aiueos_process_initialize(void) {
  int mappings=aiueos_user_mapping_verify();
  /* Scheduler initialization already created both roots. Rebuilding them here
   * would clear live private pages after service execution. */
  if (mappings != 7) return 0x10 | mappings;
  tss.rsp0=(uint64_t)(uintptr_t)aiueos_kernel_stack_top;
  aiueos_syscall_kernel_stack_top=tss.rsp0;
  tss.iomap=sizeof(tss);
  if (!syscall_transport_initialize()) return 0x20;
  uint64_t b=(uint64_t)(uintptr_t)&tss, limit=sizeof(tss)-1;
  aiueos_gdt_tss[0]=(limit & 0xffff) | ((b & 0xffffff)<<16) |
    (0x89ULL<<40) | ((limit & 0xf0000)<<32) | ((b & 0xff000000)<<32);
  aiueos_gdt_tss[1]=b>>32;
  for (unsigned p=0;p<PROCESS_CAPACITY;p++) processes[p]=(struct process_descriptor){0};
  process_lifecycle_evidence=0;
  process_results_valid=0;
  return 1;
}
void aiueos_process_enter(void) {
  uint64_t allocator_reuse_before;
  uint64_t recreated_entry=0,*recreated_result=0,*worker_result=0;
  if (!aiueos_scheduler_begin_user_runtime()) return;
  uint64_t *kotoba_result=0;
  int first=process_create(user_entry,2),second=process_create(user_entry,3);
  int kotoba=process_create_kotoba_elf(hello_app_id,4,&kotoba_result);
  int worker=process_create_kotoba_elf(worker_app_id,5,&worker_result);
  uint64_t *missing_result=0;
  catalog_lookup_rejection_evidence=process_create_kotoba_elf(missing_app_id,6,&missing_result)<0;
  if (first<0 || second<0 || kotoba<0 || worker<0 || !kotoba_result || !worker_result ||
      !catalog_lookup_rejection_evidence) return;
  kernel_results[0]=processes[first].result; kernel_results[1]=processes[second].result;
  kernel_results[0]->foreign_handle=kernel_results[1]->handle;
  kernel_results[1]->foreign_handle=kernel_results[0]->handle;
  kernel_results[0]->foreign_page=processes[second].argument;
  kernel_results[1]->foreign_page=processes[first].argument;
  __asm__ volatile("sti");
  while (!aiueos_process_result() || !aiueos_user_scheduler_evidence_ready() ||
         *kotoba_result!=42 || *worker_result!=42)
    __asm__ volatile("hlt");
  aiueos_scheduler_request_user_exit(2); aiueos_scheduler_request_user_exit(3);
  aiueos_scheduler_request_user_exit(4);
  aiueos_scheduler_request_user_exit(5);
  while (!aiueos_scheduler_users_reaped()) __asm__ volatile("hlt");
  __asm__ volatile("cli");
  if (aiueos_scheduler_finalize_user_stacks() &&
      aiueos_scheduler_task_capacity()==8 &&
      aiueos_scheduler_task_slot_self_test() &&
      aiueos_capability_revoke_owner(2)>=2 &&
      aiueos_capability_revoke_owner(3)>=1 &&
      aiueos_capability_revoke_owner(4)>=1 &&
      aiueos_capability_revoke_owner(5)>=1 &&
      !aiueos_capability_log_handle(2) && !aiueos_capability_log_handle(3) &&
      !aiueos_capability_log_handle(4) && !aiueos_capability_log_handle(5) &&
      aiueos_kotoba_process_loader_evidence_ready())
    process_lifecycle_evidence|=1;
  allocator_reuse_before=aiueos_physical_allocator_reuse_count();
  unsigned space0=processes[first].address_space,space1=processes[second].address_space;
  unsigned space2=processes[kotoba].address_space;
  unsigned space3=processes[worker].address_space;
  processes[first].active=processes[second].active=processes[kotoba].active=processes[worker].active=0;
  if (aiueos_address_space_reclaim(space0) && aiueos_address_space_reclaim(space1) &&
      aiueos_address_space_reclaim(space2) &&
      aiueos_address_space_reclaim(space3) &&
      aiueos_address_space_reuse(space0) && aiueos_address_space_reuse(space1) &&
      aiueos_address_space_reuse(space2) && aiueos_load_object_store_kotoba_process(
        space2,hello_app_id,&recreated_entry,&recreated_result) && recreated_entry==0x1e1000ULL &&
      recreated_result && *recreated_result==0 && aiueos_address_space_reclaim(space2) &&
      aiueos_address_space_reuse(space3) && aiueos_load_object_store_kotoba_process(
        space3,worker_app_id,&recreated_entry,&recreated_result) && recreated_entry==0x1e1000ULL &&
      recreated_result && *recreated_result==0 && aiueos_address_space_reclaim(space3) &&
      aiueos_physical_allocator_reuse_count()-allocator_reuse_before>=24)
    process_lifecycle_evidence|=2;
  if (aiueos_address_space_capacity()==8 && aiueos_address_space_slot_self_test())
    process_lifecycle_evidence|=4;
}
int aiueos_process_lifecycle_evidence_ready(void) {
  return process_lifecycle_evidence==7 && aiueos_scheduler_reap_evidence_ready();
}
int aiueos_catalog_lookup_rejection_evidence_ready(void) { return catalog_lookup_rejection_evidence; }
int aiueos_process_result(void) {
  if (process_results_valid) return 1;
  for (unsigned p=0; p<2; p++) {
    struct user_result *r=kernel_results[p];
    if (!r || !r->completed || r->abi!=ABI_V1 || r->valid!=5 ||
        r->too_big!=TOO_BIG || r->stale!=BAD_HANDLE ||
        r->foreign_owner!=BAD_HANDLE || r->wrong_type!=BAD_HANDLE ||
        r->no_rights!=BAD_HANDLE || r->bad_pointer!=BAD_POINTER ||
        r->scheduled_runs<2) return 0;
  }
  if (!kernel_results[0]->transfer ||
      kernel_results[0]->transfer==BAD_HANDLE ||
      kernel_results[0]->transfer_escalation!=BAD_HANDLE ||
      kernel_results[1]->claimed!=kernel_results[0]->transfer ||
      kernel_results[1]->transferred_valid!=5) return 0;
  process_results_valid=aiueos_syscall_from_user==3;
  return process_results_valid;
}

int aiueos_address_space_self_test(void) {
  if (!aiueos_address_spaces_initialize()) return 0;
  uint64_t first=aiueos_address_space_private_va(0), second=aiueos_address_space_private_va(1);
  uint64_t cr3_first=aiueos_address_space_enter(0);
  *(volatile uint64_t *)(uintptr_t)first=0x1111111111111111ULL;
  aiueos_page_fault_stage=3; aiueos_probe_cross_process((const void *)(uintptr_t)second);
  if (aiueos_page_fault_stage!=0x103 || (aiueos_page_fault_error&1)) { aiueos_address_space_leave(); return 0; }
  uint64_t cr3_second=aiueos_address_space_enter(1);
  if (*(volatile uint64_t *)(uintptr_t)second!=0) { aiueos_address_space_leave(); return 0; }
  *(volatile uint64_t *)(uintptr_t)second=0x2222222222222222ULL;
  aiueos_page_fault_stage=3; aiueos_probe_cross_process((const void *)(uintptr_t)first);
  int isolated=aiueos_page_fault_stage==0x103 && !(aiueos_page_fault_error&1) && cr3_first!=cr3_second;
  if (isolated) { aiueos_address_space_enter(0); isolated=*(volatile uint64_t *)(uintptr_t)first==0x1111111111111111ULL; }
  aiueos_address_space_leave(); return isolated;
}
