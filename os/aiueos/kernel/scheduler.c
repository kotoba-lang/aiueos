#include <stdint.h>

#define AIUEOS_TASK_SLOT_COUNT 9U
#define AIUEOS_USER_TASK_CAPACITY 8U
#define AIUEOS_SERVICE_CAPACITY 8U
#define AIUEOS_TASK_STACK_BYTES 4096U
#define AIUEOS_KERNEL_CODE_SELECTOR 0x08U
#define AIUEOS_INTERRUPT_FLAG (1ULL << 9)
#define AIUEOS_IPC_CAPABILITY_TYPE 2U
#define AIUEOS_IPC_SEND_RIGHT 1U

/* A saved stack pointer is the complete kernel-task context. */
struct aiueos_interrupt_context {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
  uint64_t rip, cs, rflags, rsp, ss;
};

extern int aiueos_address_spaces_initialize(void);
extern uint64_t aiueos_address_space_kernel_cr3(void);
extern uint64_t aiueos_address_space_cr3(unsigned process);
extern uint64_t aiueos_address_space_current_cr3(void);
extern void aiueos_address_space_switch(uint64_t cr3);
extern uint64_t aiueos_address_space_private_va(unsigned process);
extern uint64_t kotoba_aiueos_service_lifecycle(uint64_t generation,
  uint64_t restarts, uint64_t event, uint64_t budget);
extern uint64_t kotoba_aiueos_capability_plan(uint64_t slot,
  uint64_t generation, uint64_t type, uint64_t state_rights, uint64_t request);
extern void aiueos_process_set_kernel_stack(uint64_t top);
extern volatile uint16_t aiueos_current_user_domain;
extern void *aiueos_allocate_physical_page(void);
extern int aiueos_free_physical_page(void *page);

struct aiueos_task {
  uint64_t *saved_stack;
  uint8_t *kernel_stack;
  uint64_t switches, cr3;
  uint16_t generation, service;
  uint8_t active;
};
static struct aiueos_task tasks[AIUEOS_TASK_SLOT_COUNT];
static uint64_t current_task;
static int scheduler_user_mode;
volatile uint64_t aiueos_user_scheduler_switches;
static volatile uint8_t user_exit_requested[2];
static uint64_t user_tasks_reaped;
static uint64_t user_kernel_stacks_zeroed;
static volatile uint64_t service_runs[AIUEOS_SERVICE_CAPACITY];
volatile uint64_t aiueos_scheduler_context_switches;
volatile uint64_t aiueos_scheduler_address_space_failures;
struct aiueos_service_slot {
  uint64_t id, generation, heartbeats, restarts, restart_requested;
  volatile uint64_t *runs;
  uint16_t task_slot, process_slot;
  uint8_t marker, active;
};
static struct aiueos_service_slot services[AIUEOS_SERVICE_CAPACITY];
struct aiueos_service_mailbox {
  volatile uint64_t sequence, sender_id, sender_generation, recipient_id;
  volatile uint64_t payload, full;
};
static struct aiueos_service_mailbox service_mailbox;
static uint64_t service_ipc_send_handle;
static volatile uint64_t service_ipc_received_sequence;
static volatile uint64_t service_ipc_received_payload;
static volatile uint64_t service_ipc_foreign_rejections;
static uint64_t dynamic_task_evidence;
static uint64_t kotoba_lifecycle_evidence;
static uint64_t persistent_restore_evidence;

static int allocate_task_slot(uint64_t cr3) {
  for (unsigned slot=1;slot<AIUEOS_TASK_SLOT_COUNT;slot++) {
    if (tasks[slot].active || tasks[slot].kernel_stack) continue;
    uint8_t *stack=aiueos_allocate_physical_page();
    if (!stack) return -1;
    tasks[slot].kernel_stack=stack;
    tasks[slot].saved_stack=0;
    tasks[slot].switches=0;
    tasks[slot].cr3=cr3;
    tasks[slot].service=0xffffU;
    tasks[slot].generation++;
    if (!tasks[slot].generation) tasks[slot].generation=1;
    tasks[slot].active=1;
    return (int)slot;
  }
  return -1;
}
static int release_task_slot(unsigned slot) {
  if (!slot || slot>=AIUEOS_TASK_SLOT_COUNT || tasks[slot].active ||
      !tasks[slot].kernel_stack) return 0;
  if (!aiueos_free_physical_page(tasks[slot].kernel_stack)) return 0;
  tasks[slot].kernel_stack=0;
  tasks[slot].saved_stack=0;
  tasks[slot].switches=0;
  tasks[slot].cr3=0;
  tasks[slot].service=0xffffU;
  return 1;
}

static uint64_t ipc_capability_plan(uint16_t requester) {
  uint64_t state = AIUEOS_IPC_SEND_RIGHT | 65536U | ((uint64_t)1 << 17);
  uint64_t request = AIUEOS_IPC_SEND_RIGHT |
    ((uint64_t)AIUEOS_IPC_CAPABILITY_TYPE << 16) |
    ((uint64_t)requester << 32);
  return kotoba_aiueos_capability_plan(4,1,AIUEOS_IPC_CAPABILITY_TYPE,state,request);
}

static void service_ipc_step(unsigned process) {
  if (process == 0 && services[0].generation == 2 &&
      services[0].heartbeats == 1 && !service_mailbox.full) {
    if (ipc_capability_plan(1) != service_ipc_send_handle) return;
    service_mailbox.sender_id = services[0].id;
    service_mailbox.sender_generation = services[0].generation;
    service_mailbox.recipient_id = services[1].id;
    service_mailbox.payload = 0x4b4f544f42414950ULL;
    service_mailbox.sequence = 1;
    __asm__ volatile("" ::: "memory");
    service_mailbox.full = 1;
  } else if (process == 1 && service_mailbox.full) {
    __asm__ volatile("" ::: "memory");
    if (service_mailbox.recipient_id != services[1].id ||
        service_mailbox.sender_id != services[0].id ||
        service_mailbox.sender_generation != services[0].generation) return;
    service_ipc_received_payload = service_mailbox.payload;
    service_ipc_received_sequence = service_mailbox.sequence;
    service_mailbox.full = 0;
  }
}

static inline void debug_byte(uint8_t value) {
  __asm__ volatile("outb %0, $0xe9" : : "a"(value));
}
static void task_loop(volatile uint64_t *runs, uint8_t marker, unsigned process) {
  volatile uint64_t *private_word =
    (volatile uint64_t *)(uintptr_t)aiueos_address_space_private_va(process);
  for (;;) {
    if (aiueos_address_space_current_cr3() != aiueos_address_space_cr3(process))
      ++aiueos_scheduler_address_space_failures;
    ++*private_word;
    *runs = *private_word;
    services[process].heartbeats++;
    service_ipc_step(process);
    /* Deterministic fault injection proves that the scheduler replaces the
     * task context instead of merely changing lifecycle metadata. */
    if (process == 0 && services[process].generation == 1 &&
        services[process].heartbeats == 2)
      services[process].restart_requested = 1;
    if (*runs == 1) debug_byte(marker);
    __asm__ volatile("hlt");
  }
}
__attribute__((noreturn)) static void service_task_entry(uint64_t service) {
  if (service>=AIUEOS_SERVICE_CAPACITY || !services[service].active)
    for (;;) __asm__ volatile("hlt");
  if (!services[service].runs) for (;;) __asm__ volatile("hlt");
  task_loop(services[service].runs,services[service].marker,
    services[service].process_slot);
  __builtin_unreachable();
}
static uint64_t *initial_context(uint8_t *stack, void (*entry)(uint64_t), uint64_t argument) {
  /* iret enters a C function directly, so model the stack position normally
   * produced by call (RSP % 16 == 8 at function entry). */
  uintptr_t top = (((uintptr_t)stack + AIUEOS_TASK_STACK_BYTES) & ~(uintptr_t)15) - 8;
  struct aiueos_interrupt_context *context =
      (struct aiueos_interrupt_context *)(top - sizeof(*context));
  for (uint64_t *word = (uint64_t *)context;
       word != (uint64_t *)(context + 1); ++word) *word = 0;
  context->rip = (uint64_t)(uintptr_t)entry;
  context->rdi = argument;
  context->cs = AIUEOS_KERNEL_CODE_SELECTOR;
  context->rflags = AIUEOS_INTERRUPT_FLAG | 2U;
  context->rsp = top;
  context->ss = 0x10U;
  return (uint64_t *)context;
}
static uint64_t *initial_user_context(uint8_t *stack, void (*entry)(void),
                                      uint64_t user_stack) {
  uintptr_t top = ((uintptr_t)stack + AIUEOS_TASK_STACK_BYTES) & ~(uintptr_t)15;
  struct aiueos_interrupt_context *context =
    (struct aiueos_interrupt_context *)(top - sizeof(*context));
  for (uint64_t *word=(uint64_t *)context; word!=(uint64_t *)(context+1); ++word) *word=0;
  context->rip=(uint64_t)(uintptr_t)entry; context->cs=0x1bU;
  context->rflags=AIUEOS_INTERRUPT_FLAG|2U; context->rsp=user_stack; context->ss=0x23U;
  return (uint64_t *)context;
}
static int apply_service_event(unsigned service, uint64_t event) {
  if (service>=AIUEOS_SERVICE_CAPACITY) return 0;
  struct aiueos_service_slot *descriptor=&services[service];
  uint64_t plan=kotoba_aiueos_service_lifecycle(
    descriptor->generation,descriptor->restarts,event,3);
  uint64_t action=plan>>32;
  if (!plan && event) return 0;
  if (action==1) {
    if (descriptor->active) return 0;
    uint64_t cr3=descriptor->process_slot<8 ?
      aiueos_address_space_cr3(descriptor->process_slot) :
      aiueos_address_space_kernel_cr3();
    int slot=allocate_task_slot(cr3);
    if (slot<1) return 0;
    descriptor->generation=plan&65535U;
    descriptor->restarts=(plan>>16)&65535U;
    descriptor->task_slot=(uint16_t)slot;
    tasks[slot].service=(uint16_t)service;
    descriptor->heartbeats=descriptor->restart_requested=0;
    descriptor->active=1;
    tasks[slot].saved_stack=initial_context(tasks[slot].kernel_stack,
      service_task_entry,service);
    return 1;
  }
  if (action==2) {
    if (!descriptor->active || descriptor->task_slot<1) return 0;
    descriptor->generation=plan&65535U;
    descriptor->restarts=(plan>>16)&65535U;
    descriptor->heartbeats=descriptor->restart_requested=0;
    tasks[descriptor->task_slot].saved_stack=initial_context(
      tasks[descriptor->task_slot].kernel_stack,service_task_entry,service);
    return 1;
  }
  if (action==3) {
    unsigned slot=descriptor->task_slot;
    if (!descriptor->active || slot<1 || slot==current_task) return 0;
    tasks[slot].active=0;
    if (!release_task_slot(slot)) return 0;
    descriptor->active=0; descriptor->task_slot=0;
    return 1;
  }
  return event==0 && descriptor->active;
}
void aiueos_scheduler_initialize(void) {
  if (!aiueos_address_spaces_initialize()) {
    aiueos_scheduler_address_space_failures = 1;
    return;
  }
  for (uint32_t i=0;i<AIUEOS_TASK_SLOT_COUNT;i++) tasks[i]=(struct aiueos_task){0,0,0,0,0,0xffffU,0};
  tasks[0].cr3=aiueos_address_space_kernel_cr3(); tasks[0].active=1;
  for (unsigned service=0;service<AIUEOS_SERVICE_CAPACITY;service++)
    services[service]=(struct aiueos_service_slot){0,0,0,0,0,0,0,0,0,0};
  services[0].id=1; services[0].runs=&service_runs[0];
  services[0].process_slot=0; services[0].marker='A';
  services[1].id=2; services[1].runs=&service_runs[1];
  services[1].process_slot=1; services[1].marker='B';
  if (!apply_service_event(0,2) || !apply_service_event(1,2) ||
      services[0].task_slot!=1 || services[1].task_slot!=2) {
    aiueos_scheduler_address_space_failures=1; return;
  }
  services[2].id=3; services[2].process_slot=0xffffU; services[2].marker='C';
  if (!apply_service_event(2,2) || services[2].task_slot!=3 ||
      !apply_service_event(2,3) || services[2].active) {
    aiueos_scheduler_address_space_failures=1; return;
  }
  kotoba_lifecycle_evidence=1;
  current_task = 0;
  scheduler_user_mode = 0;
  aiueos_user_scheduler_switches = 0;
  for (unsigned service=0;service<AIUEOS_SERVICE_CAPACITY;service++) service_runs[service]=0;
  aiueos_scheduler_context_switches = 0;
  aiueos_scheduler_address_space_failures = 0;
  service_mailbox = (struct aiueos_service_mailbox){0,0,0,0,0,0};
  service_ipc_received_sequence = service_ipc_received_payload = 0;
  service_ipc_send_handle = ipc_capability_plan(1);
  service_ipc_foreign_rejections = ipc_capability_plan(2) == 0 ? 1 : 0;
}
void aiueos_scheduler_start_user_processes(void (*entry0)(void), void (*entry1)(void),
    uint64_t user_stack0, uint64_t user_stack1) {
  if (!apply_service_event(0,3) || !apply_service_event(1,3)) {
    aiueos_scheduler_address_space_failures=1; return;
  }
  int slot0=allocate_task_slot(aiueos_address_space_cr3(0));
  int slot1=allocate_task_slot(aiueos_address_space_cr3(1));
  if (slot0!=1 || slot1!=2) { aiueos_scheduler_address_space_failures=1; return; }
  tasks[0].saved_stack=0; tasks[0].switches=0;
  tasks[1].saved_stack=initial_user_context(tasks[1].kernel_stack,entry0,user_stack0);
  tasks[2].saved_stack=initial_user_context(tasks[2].kernel_stack,entry1,user_stack1);
  current_task=0; scheduler_user_mode=1; aiueos_user_scheduler_switches=0;
  user_exit_requested[0]=user_exit_requested[1]=0;
  user_tasks_reaped=0; user_kernel_stacks_zeroed=0;
  aiueos_current_user_domain=0;
}
int aiueos_service_runtime_evidence_ready(void) {
  return kotoba_lifecycle_evidence && services[0].id == 1 && services[1].id == 2 &&
    services[0].generation == 2 && services[0].restarts == 1 &&
    services[0].restart_requested == 0 && services[1].generation == 1 &&
    services[0].heartbeats >= 2 && services[1].heartbeats >= 2 &&
    services[0].active && services[1].active &&
    services[2].generation==1 && !services[2].active;
}
int aiueos_service_ipc_evidence_ready(void) {
  return service_ipc_send_handle != 0 && service_ipc_foreign_rejections == 1 &&
    service_ipc_received_sequence == 1 &&
    service_ipc_received_payload == 0x4b4f544f42414950ULL &&
    service_mailbox.full == 0;
}
uint64_t aiueos_service_registry_state(unsigned service) {
  if (service >= 2 || services[service].id > 255 ||
      services[service].generation > 255 || services[service].restarts > 255) return 0;
  return services[service].id | (services[service].generation << 16) |
    (services[service].restarts << 32);
}
int aiueos_scheduler_restore_service_registry(uint64_t state0, uint64_t state1) {
  uint64_t states[2]={state0,state1};
  if (current_task!=0 || scheduler_user_mode) return 0;
  for (unsigned service=0;service<2;service++) {
    uint64_t id=states[service]&255U;
    uint64_t generation=(states[service]>>16)&255U;
    uint64_t restarts=(states[service]>>32)&255U;
    if (id!=service+1 || !generation || restarts>3) return 0;
  }
  if (!apply_service_event(0,3) || !apply_service_event(1,3)) return 0;
  for (unsigned service=0;service<2;service++) {
    services[service].generation=(states[service]>>16)&255U;
    services[service].restarts=(states[service]>>32)&255U;
  }
  if (!apply_service_event(0,2) || !apply_service_event(1,2)) return 0;
  persistent_restore_evidence=services[0].task_slot==1 &&
    services[1].task_slot==2 && services[0].generation==((state0>>16)&255U) &&
    services[1].generation==((state1>>16)&255U);
  return (int)persistent_restore_evidence;
}
int aiueos_scheduler_persistent_restore_evidence_ready(void) {
  return (int)persistent_restore_evidence;
}
uint64_t *aiueos_scheduler_on_timer(uint64_t *interrupted_stack) {
  tasks[current_task].saved_stack = interrupted_stack;
  tasks[current_task].switches++;
  if (scheduler_user_mode && current_task>0 && user_exit_requested[current_task-1]) {
    tasks[current_task].active=0; user_tasks_reaped++;
  }
  if (!scheduler_user_mode && current_task > 0) {
    unsigned service=tasks[current_task].service;
    if (service<AIUEOS_SERVICE_CAPACITY && services[service].restart_requested) {
      if (!apply_service_event(service,1))
        aiueos_scheduler_address_space_failures++;
    }
  }
  do { current_task = (current_task + 1U) % AIUEOS_TASK_SLOT_COUNT; }
  while (!tasks[current_task].active);
  aiueos_scheduler_context_switches++;
  if (scheduler_user_mode) {
    aiueos_current_user_domain = current_task ? (uint16_t)(current_task + 1) : 0;
    if (current_task) {
      aiueos_process_set_kernel_stack((uint64_t)(uintptr_t)
        (tasks[current_task].kernel_stack + AIUEOS_TASK_STACK_BYTES));
      aiueos_user_scheduler_switches++;
    }
  }
  /* Interrupt code is mapped supervisor-only in every root.  Switch after
   * saving the outgoing frame, immediately before iret resumes the task. */
  aiueos_address_space_switch(tasks[current_task].cr3);
  return tasks[current_task].saved_stack;
}
void aiueos_scheduler_request_user_exit(uint16_t domain) {
  if (domain>=2 && domain<=3) user_exit_requested[domain-2]=1;
}
int aiueos_scheduler_users_reaped(void) { return user_tasks_reaped==2; }
int aiueos_scheduler_finalize_user_stacks(void) {
  if (user_tasks_reaped!=2 || current_task!=0) return 0;
  if (!release_task_slot(1) || !release_task_slot(2)) return 0;
  user_kernel_stacks_zeroed=2; scheduler_user_mode=0;
  return 1;
}
unsigned aiueos_scheduler_task_capacity(void) { return AIUEOS_USER_TASK_CAPACITY; }
int aiueos_scheduler_task_slot_self_test(void) {
  uint16_t generation_before=tasks[1].generation;
  for (unsigned expected=1;expected<AIUEOS_TASK_SLOT_COUNT;expected++)
    if (allocate_task_slot(aiueos_address_space_kernel_cr3())!=(int)expected) return 0;
  if (allocate_task_slot(aiueos_address_space_kernel_cr3())!=-1) return 0;
  for (unsigned slot=1;slot<AIUEOS_TASK_SLOT_COUNT;slot++) tasks[slot].active=0;
  for (unsigned slot=1;slot<AIUEOS_TASK_SLOT_COUNT;slot++)
    if (!release_task_slot(slot)) return 0;
  if (allocate_task_slot(aiueos_address_space_kernel_cr3())!=1 ||
      tasks[1].generation<=generation_before) return 0;
  for (unsigned i=0;i<AIUEOS_TASK_STACK_BYTES;i++)
    if (tasks[1].kernel_stack[i]) return 0;
  tasks[1].active=0;
  if (!release_task_slot(1)) return 0;
  dynamic_task_evidence=1;
  return 1;
}
int aiueos_scheduler_reap_evidence_ready(void) {
  return user_tasks_reaped==2 && user_kernel_stacks_zeroed==2 &&
    !tasks[1].active && !tasks[2].active && !tasks[1].kernel_stack &&
    !tasks[2].kernel_stack && dynamic_task_evidence && current_task==0;
}
int aiueos_user_scheduler_evidence_ready(void) {
  return scheduler_user_mode && aiueos_user_scheduler_switches >= 4 &&
    tasks[0].switches >= 2 && tasks[1].switches >= 2 && tasks[2].switches >= 2 &&
    aiueos_address_space_current_cr3() == tasks[0].cr3;
}
int aiueos_scheduler_evidence_ready(void) {
  return service_runs[0] >= 2 && service_runs[1] >= 2 &&
         aiueos_scheduler_context_switches >= 6 && tasks[0].switches >= 2 &&
         tasks[1].switches >= 2 && tasks[2].switches >= 2 &&
         aiueos_scheduler_address_space_failures == 0 &&
         aiueos_address_space_current_cr3() == tasks[0].cr3 &&
         tasks[0].cr3 != tasks[1].cr3 && tasks[1].cr3 != tasks[2].cr3 &&
         tasks[0].cr3 != tasks[2].cr3;
}
