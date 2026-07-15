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
  char message[8];
};

extern uint64_t aiueos_gdt_tss[2];
extern uint8_t aiueos_kernel_stack_top[];
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
extern void aiueos_scheduler_start_user_processes(void (*entry0)(void),void (*entry1)(void),
  uint64_t user_stack0,uint64_t user_stack1);
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
extern void aiueos_probe_cross_process(const void *address);
extern volatile uint64_t aiueos_page_fault_stage, aiueos_page_fault_error;
static struct tss64 tss;
static uint8_t syscall_stack[16384] __attribute__((aligned(4096)));
__attribute__((section(".user.data"), aligned(4096), used))
static uint8_t user_mapping_anchor[4096];
static struct user_result *kernel_results[2];
static uint64_t process_lifecycle_evidence;
static int process_results_valid;
int aiueos_process_result(void);
void aiueos_process_set_kernel_stack(uint64_t top) { tss.rsp0=top; }

static inline uint64_t call(uint64_t n, uint64_t h, const void *p, uint64_t l) {
  register uint64_t a __asm__("rax")=n;
  __asm__ volatile("int $0x80" : "+a"(a) : "D"(h), "S"(p), "d"(l) : "memory");
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
static void user_entry0(void) { user_run((void *)0x1f4000ULL,(void *)0x1f5000ULL); }
__attribute__((section(".user.text"), noreturn))
static void user_entry1(void) { user_run((void *)0x1f5000ULL,(void *)0x1f4000ULL); }

int aiueos_process_initialize(void) {
  int mappings=aiueos_user_mapping_verify();
  /* Scheduler initialization already created both roots. Rebuilding them here
   * would clear live private pages after service execution. */
  if (mappings != 7) return 0x10 | mappings;
  tss.rsp0=(uint64_t)(uintptr_t)(syscall_stack + sizeof(syscall_stack)); tss.iomap=sizeof(tss);
  uint64_t b=(uint64_t)(uintptr_t)&tss, limit=sizeof(tss)-1;
  aiueos_gdt_tss[0]=(limit & 0xffff) | ((b & 0xffffff)<<16) |
    (0x89ULL<<40) | ((limit & 0xf0000)<<32) | ((b & 0xff000000)<<32);
  aiueos_gdt_tss[1]=b>>32;
  for (unsigned p=0; p<2; p++) {
    kernel_results[p] = aiueos_address_space_private_backing(p);
    if (!kernel_results[p]) return 0;
    kernel_results[p]->handle = aiueos_capability_log_handle((uint16_t)(p+2));
    if (!kernel_results[p]->handle) return 0;
    kernel_results[p]->message[0]='r'; kernel_results[p]->message[1]='i';
    kernel_results[p]->message[2]='n'; kernel_results[p]->message[3]='g';
    kernel_results[p]->message[4]=(char)('3'+p); kernel_results[p]->message[5]=0;
    kernel_results[p]->domain=p+2;
  }
  kernel_results[0]->foreign_handle=kernel_results[1]->handle;
  kernel_results[1]->foreign_handle=kernel_results[0]->handle;
  process_lifecycle_evidence=0;
  process_results_valid=0;
  return 1;
}
void aiueos_process_enter(void) {
  uint64_t allocator_reuse_before;
  aiueos_scheduler_start_user_processes(user_entry0,user_entry1,
    aiueos_address_space_private_va(0)+4096,aiueos_address_space_private_va(1)+4096);
  __asm__ volatile("sti");
  while (!aiueos_process_result() || !aiueos_user_scheduler_evidence_ready())
    __asm__ volatile("hlt");
  aiueos_scheduler_request_user_exit(2); aiueos_scheduler_request_user_exit(3);
  while (!aiueos_scheduler_users_reaped()) __asm__ volatile("hlt");
  __asm__ volatile("cli");
  if (aiueos_scheduler_finalize_user_stacks() &&
      aiueos_scheduler_task_capacity()==8 &&
      aiueos_scheduler_task_slot_self_test() &&
      aiueos_capability_revoke_owner(2)>=2 &&
      aiueos_capability_revoke_owner(3)>=1 &&
      !aiueos_capability_log_handle(2) && !aiueos_capability_log_handle(3))
    process_lifecycle_evidence|=1;
  allocator_reuse_before=aiueos_physical_allocator_reuse_count();
  if (aiueos_address_space_reclaim(0) && aiueos_address_space_reclaim(1) &&
      aiueos_address_space_reuse(0) && aiueos_address_space_reuse(1) &&
      aiueos_physical_allocator_reuse_count()-allocator_reuse_before>=10)
    process_lifecycle_evidence|=2;
  if (aiueos_address_space_capacity()==8 && aiueos_address_space_slot_self_test())
    process_lifecycle_evidence|=4;
}
int aiueos_process_lifecycle_evidence_ready(void) {
  return process_lifecycle_evidence==7 && aiueos_scheduler_reap_evidence_ready();
}
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
