#include <stdint.h>

#define AIUEOS_PLC_INPUT_CAP 16U
#define AIUEOS_PLC_STAGE_CAP 17U
#define AIUEOS_PLC_COMMIT_CAP 18U
#define AIUEOS_PLC_WATCHDOG_CAP 19U
#define AIUEOS_PLC_IO_COUNT 2U
#define AIUEOS_PLC_CYCLE_TICKS 10U
#define AIUEOS_PLC_DEADLINE_TICKS 8U
#define AIUEOS_PLC_BUDGET_TICKS 1U

enum aiueos_plc_fault {
  AIUEOS_PLC_FAULT_NONE = 0,
  AIUEOS_PLC_FAULT_RELEASE = 1,
  AIUEOS_PLC_FAULT_CAPABILITY = 2,
  AIUEOS_PLC_FAULT_STAGE = 3,
  AIUEOS_PLC_FAULT_WATCHDOG = 4,
  AIUEOS_PLC_FAULT_BUDGET = 5,
  AIUEOS_PLC_FAULT_DEADLINE = 6,
  AIUEOS_PLC_FAULT_PROGRAM = 7
};

struct aiueos_plc_runtime {
  int32_t physical_input[AIUEOS_PLC_IO_COUNT];
  int32_t input_image[AIUEOS_PLC_IO_COUNT];
  int32_t shadow_output[AIUEOS_PLC_IO_COUNT];
  int32_t physical_output[AIUEOS_PLC_IO_COUNT];
  int32_t safe_output[AIUEOS_PLC_IO_COUNT];
  uint64_t next_release, release, deadline, budget_end;
  uint32_t staged;
  uint8_t active, watchdog, fault_latched, fault;
};

static struct aiueos_plc_runtime plc;
extern volatile uint64_t aiueos_apic_timer_ticks;

static void plc_safe(enum aiueos_plc_fault fault) {
  for (unsigned i=0;i<AIUEOS_PLC_IO_COUNT;i++)
    plc.physical_output[i]=plc.safe_output[i];
  plc.active=0;
  if (!plc.fault_latched) plc.fault=(uint8_t)fault;
  plc.fault_latched=1;
}

static void plc_reset(void) {
  plc=(struct aiueos_plc_runtime){0};
  plc.next_release=AIUEOS_PLC_CYCLE_TICKS;
}

static int plc_release(uint64_t now) {
  if (plc.fault_latched || plc.active || now!=plc.next_release) {
    plc_safe(AIUEOS_PLC_FAULT_RELEASE); return 0;
  }
  for (unsigned i=0;i<AIUEOS_PLC_IO_COUNT;i++) {
    plc.input_image[i]=plc.physical_input[i];
    plc.shadow_output[i]=plc.safe_output[i];
  }
  plc.release=now;
  plc.deadline=now+AIUEOS_PLC_DEADLINE_TICKS;
  plc.budget_end=now+AIUEOS_PLC_BUDGET_TICKS;
  plc.next_release+=AIUEOS_PLC_CYCLE_TICKS;
  plc.staged=0; plc.watchdog=0; plc.active=1;
  return 1;
}

static uint64_t plc_call(uint64_t capability,uint64_t argument,uint64_t now) {
  if (!plc.active) { plc_safe(AIUEOS_PLC_FAULT_CAPABILITY); return 0; }
  if (capability==AIUEOS_PLC_INPUT_CAP) {
    unsigned index=(unsigned)argument;
    if (index>=AIUEOS_PLC_IO_COUNT) { plc_safe(AIUEOS_PLC_FAULT_CAPABILITY); return 0; }
    return (uint64_t)(int64_t)plc.input_image[index];
  }
  if (capability==AIUEOS_PLC_STAGE_CAP) {
    unsigned index=(unsigned)(argument>>32);
    if (index>=AIUEOS_PLC_IO_COUNT || (plc.staged&(1U<<index))) {
      plc_safe(AIUEOS_PLC_FAULT_STAGE); return 0;
    }
    plc.shadow_output[index]=(int32_t)argument;
    plc.staged|=1U<<index;
    return 1;
  }
  if (capability==AIUEOS_PLC_WATCHDOG_CAP) {
    if (argument!=1 || plc.watchdog) { plc_safe(AIUEOS_PLC_FAULT_WATCHDOG); return 0; }
    plc.watchdog=1; return 1;
  }
  if (capability==AIUEOS_PLC_COMMIT_CAP) {
    if (argument!=AIUEOS_PLC_IO_COUNT || plc.staged!=3U) {
      plc_safe(AIUEOS_PLC_FAULT_STAGE); return 0;
    }
    if (!plc.watchdog) { plc_safe(AIUEOS_PLC_FAULT_WATCHDOG); return 0; }
    if (now>plc.deadline) { plc_safe(AIUEOS_PLC_FAULT_DEADLINE); return 0; }
    if (now>plc.budget_end) { plc_safe(AIUEOS_PLC_FAULT_BUDGET); return 0; }
    for (unsigned i=0;i<AIUEOS_PLC_IO_COUNT;i++)
      plc.physical_output[i]=plc.shadow_output[i];
    plc.active=0; return 1;
  }
  plc_safe(AIUEOS_PLC_FAULT_CAPABILITY); return 0;
}

/* Exact semantics of plc/examples/motor.st. The calls deliberately use the
 * same 16..19 ABI as a generated x86_64-aiueos-user-v1 program. This is the
 * native provider gate; loading that signed ELF as a periodic task is a
 * separate admission/loader step and is not claimed here. */
static int plc_motor_scan(uint64_t now) {
  int enable=plc_call(16,0,now)!=0;
  int32_t sensor=(int32_t)plc_call(16,1,now);
  int32_t command=(int32_t)((uint32_t)sensor+1U);
  int motor=enable && command<100;
  if (plc_call(17,(uint32_t)motor,now)!=1) return 0;
  if (plc_call(17,(1ULL<<32)|(uint32_t)command,now)!=1) return 0;
  if (plc_call(19,1,now)!=1) return 0;
  return plc_call(18,2,now)==1;
}

static int plc_failure_vectors(void) {
  plc_reset(); plc.physical_output[0]=plc.physical_output[1]=7;
  if (!plc_release(10) || plc_call(17,1,10)!=1) return 0;
  if (plc_call(18,2,10)!=0 || plc.fault!=AIUEOS_PLC_FAULT_STAGE ||
      plc.physical_output[0]!=0 || plc.physical_output[1]!=0) return 0;

  plc_reset(); if (!plc_release(10)) return 0;
  if (plc_call(17,1,10)!=1 || plc_call(17,1ULL<<32,10)!=1 ||
      plc_call(18,2,10)!=0 || plc.fault!=AIUEOS_PLC_FAULT_WATCHDOG) return 0;

  plc_reset(); if (!plc_release(10)) return 0;
  if (plc_call(17,1,10)!=1 || plc_call(17,1ULL<<32,10)!=1 ||
      plc_call(19,1,10)!=1 || plc_call(18,2,12)!=0 ||
      plc.fault!=AIUEOS_PLC_FAULT_BUDGET) return 0;

  plc_reset(); if (!plc_release(10)) return 0;
  plc.budget_end=30;
  if (plc_call(17,1,10)!=1 || plc_call(17,1ULL<<32,10)!=1 ||
      plc_call(19,1,10)!=1 || plc_call(18,2,19)!=0 ||
      plc.fault!=AIUEOS_PLC_FAULT_DEADLINE) return 0;

  plc_reset(); if (!plc_release(10)) return 0;
  plc_safe(AIUEOS_PLC_FAULT_PROGRAM);
  return plc.fault_latched && plc.fault==AIUEOS_PLC_FAULT_PROGRAM &&
    plc.physical_output[0]==0 && plc.physical_output[1]==0;
}

int aiueos_plc_rt_smoke(void) {
  plc_reset();
  while (aiueos_apic_timer_ticks<plc.next_release) __asm__ volatile("sti; hlt; cli");
  uint64_t release=plc.next_release;
  plc.physical_input[0]=1; plc.physical_input[1]=41;
  if (!plc_release(release)) return 0;
  /* Mutating the physical source after release proves the input image is
   * immutable during this scan. */
  plc.physical_input[0]=0; plc.physical_input[1]=99;
  if (!plc_motor_scan(release) || plc.physical_output[0]!=1 ||
      plc.physical_output[1]!=42 || plc.fault_latched) return 0;
  return plc.next_release==20 && plc_failure_vectors();
}
