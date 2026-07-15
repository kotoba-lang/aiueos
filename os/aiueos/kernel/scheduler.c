#include <stdint.h>

#define AIUEOS_TASK_COUNT 3U
#define AIUEOS_TASK_STACK_BYTES 16384U
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

struct aiueos_task { uint64_t *saved_stack; uint64_t switches; uint64_t cr3; };
static uint8_t task_stacks[2][AIUEOS_TASK_STACK_BYTES]
    __attribute__((aligned(4096)));
static struct aiueos_task tasks[AIUEOS_TASK_COUNT];
static uint64_t current_task;
volatile uint64_t aiueos_scheduler_task_a_runs;
volatile uint64_t aiueos_scheduler_task_b_runs;
volatile uint64_t aiueos_scheduler_context_switches;
volatile uint64_t aiueos_scheduler_address_space_failures;
struct aiueos_service_slot {
  uint64_t id, generation, heartbeats, restarts, restart_requested;
};
static struct aiueos_service_slot services[2];
struct aiueos_service_mailbox {
  volatile uint64_t sequence, sender_id, sender_generation, recipient_id;
  volatile uint64_t payload, full;
};
static struct aiueos_service_mailbox service_mailbox;
static uint64_t service_ipc_send_handle;
static volatile uint64_t service_ipc_received_sequence;
static volatile uint64_t service_ipc_received_payload;
static volatile uint64_t service_ipc_foreign_rejections;

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
__attribute__((noreturn)) static void task_a(void) {
  task_loop(&aiueos_scheduler_task_a_runs, 'A', 0); __builtin_unreachable();
}
__attribute__((noreturn)) static void task_b(void) {
  task_loop(&aiueos_scheduler_task_b_runs, 'B', 1); __builtin_unreachable();
}
static uint64_t *initial_context(uint8_t *stack, void (*entry)(void)) {
  /* iret enters a C function directly, so model the stack position normally
   * produced by call (RSP % 16 == 8 at function entry). */
  uintptr_t top = (((uintptr_t)stack + AIUEOS_TASK_STACK_BYTES) & ~(uintptr_t)15) - 8;
  struct aiueos_interrupt_context *context =
      (struct aiueos_interrupt_context *)(top - sizeof(*context));
  for (uint64_t *word = (uint64_t *)context;
       word != (uint64_t *)(context + 1); ++word) *word = 0;
  context->rip = (uint64_t)(uintptr_t)entry;
  context->cs = AIUEOS_KERNEL_CODE_SELECTOR;
  context->rflags = AIUEOS_INTERRUPT_FLAG | 2U;
  context->rsp = top;
  context->ss = 0x10U;
  return (uint64_t *)context;
}
void aiueos_scheduler_initialize(void) {
  if (!aiueos_address_spaces_initialize()) {
    aiueos_scheduler_address_space_failures = 1;
    return;
  }
  tasks[0].saved_stack = 0;
  tasks[1].saved_stack = initial_context(task_stacks[0], task_a);
  tasks[2].saved_stack = initial_context(task_stacks[1], task_b);
  tasks[0].cr3 = aiueos_address_space_kernel_cr3();
  tasks[1].cr3 = aiueos_address_space_cr3(0);
  tasks[2].cr3 = aiueos_address_space_cr3(1);
  for (uint32_t i = 0; i < AIUEOS_TASK_COUNT; ++i) tasks[i].switches = 0;
  current_task = 0;
  aiueos_scheduler_task_a_runs = aiueos_scheduler_task_b_runs = 0;
  aiueos_scheduler_context_switches = 0;
  aiueos_scheduler_address_space_failures = 0;
  services[0] = (struct aiueos_service_slot){1, 1, 0, 0, 0};
  services[1] = (struct aiueos_service_slot){2, 1, 0, 0, 0};
  service_mailbox = (struct aiueos_service_mailbox){0,0,0,0,0,0};
  service_ipc_received_sequence = service_ipc_received_payload = 0;
  service_ipc_send_handle = ipc_capability_plan(1);
  service_ipc_foreign_rejections = ipc_capability_plan(2) == 0 ? 1 : 0;
}
int aiueos_service_runtime_evidence_ready(void) {
  return services[0].id == 1 && services[1].id == 2 &&
    services[0].generation == 2 && services[0].restarts == 1 &&
    services[0].restart_requested == 0 && services[1].generation == 1 &&
    services[0].heartbeats >= 2 && services[1].heartbeats >= 2;
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
uint64_t *aiueos_scheduler_on_timer(uint64_t *interrupted_stack) {
  tasks[current_task].saved_stack = interrupted_stack;
  tasks[current_task].switches++;
  if (current_task > 0) {
    unsigned service = (unsigned)(current_task - 1);
    if (services[service].restart_requested) {
      uint64_t plan = kotoba_aiueos_service_lifecycle(
        services[service].generation, services[service].restarts, 1, 3);
      if (plan) {
        services[service].generation = plan & 65535U;
        services[service].restarts = (plan >> 16) & 65535U;
        services[service].heartbeats = 0;
        services[service].restart_requested = 0;
        tasks[current_task].saved_stack = initial_context(
          task_stacks[service], service == 0 ? task_a : task_b);
      }
    }
  }
  current_task = (current_task + 1U) % AIUEOS_TASK_COUNT;
  aiueos_scheduler_context_switches++;
  /* Interrupt code is mapped supervisor-only in every root.  Switch after
   * saving the outgoing frame, immediately before iret resumes the task. */
  aiueos_address_space_switch(tasks[current_task].cr3);
  return tasks[current_task].saved_stack;
}
int aiueos_scheduler_evidence_ready(void) {
  return aiueos_scheduler_task_a_runs >= 2 && aiueos_scheduler_task_b_runs >= 2 &&
         aiueos_scheduler_context_switches >= 6 && tasks[0].switches >= 2 &&
         tasks[1].switches >= 2 && tasks[2].switches >= 2 &&
         aiueos_scheduler_address_space_failures == 0 &&
         aiueos_address_space_current_cr3() == tasks[0].cr3 &&
         tasks[0].cr3 != tasks[1].cr3 && tasks[1].cr3 != tasks[2].cr3 &&
         tasks[0].cr3 != tasks[2].cr3;
}
