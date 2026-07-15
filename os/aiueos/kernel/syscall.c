#include <stdint.h>

enum { AIUEOS_SYSCALL_ABI = 0, AIUEOS_SYSCALL_LOG_WRITE = 1 };
#define AIUEOS_SYSCALL_ABI_V1 0x00010000ULL
#define AIUEOS_CAP_LOG_WRITE 1ULL
#define AIUEOS_ERR_NO_SYSCALL ((uint64_t)-38)
#define AIUEOS_ERR_BAD_HANDLE ((uint64_t)-9)
#define AIUEOS_ERR_BAD_POINTER ((uint64_t)-14)

/* index 1, generation 1, type tag in the high 32 bits */
static const uint64_t log_handle = 0xa105ca7e00010001ULL;

extern uint64_t kotoba_aiueos_syscall_range_valid(
  uint64_t pointer, uint64_t length, uint64_t lower, uint64_t upper);

extern uint8_t aiueos_user_data_start[], aiueos_user_data_end[];
extern uint64_t aiueos_syscall_from_user;
static int readable_user_range(uint64_t pointer, uint64_t length) {
  return (int)kotoba_aiueos_syscall_range_valid(
    pointer, length, (uint64_t)(uintptr_t)aiueos_user_data_start,
    (uint64_t)(uintptr_t)aiueos_user_data_end);
}

uint64_t aiueos_syscall_dispatch(uint64_t number, uint64_t handle,
                                 uint64_t pointer, uint64_t length) {
  if (number == AIUEOS_SYSCALL_ABI) return AIUEOS_SYSCALL_ABI_V1;
  if (number == 2) return 2; /* assembly consumes this completion token */
  if (number != AIUEOS_SYSCALL_LOG_WRITE) return AIUEOS_ERR_NO_SYSCALL;
  if (handle != log_handle || (handle & 0xffffULL) != AIUEOS_CAP_LOG_WRITE)
    return AIUEOS_ERR_BAD_HANDLE;
  if (aiueos_syscall_from_user ? !readable_user_range(pointer,length) :
      !kotoba_aiueos_syscall_range_valid(pointer,length,0x1000,0x40000000ULL))
    return AIUEOS_ERR_BAD_POINTER;
  return length;
}

static uint64_t invoke(uint64_t number, uint64_t handle,
                       const void *pointer, uint64_t length) {
  register uint64_t rax __asm__("rax") = number;
  register uint64_t rdi __asm__("rdi") = handle;
  register const void *rsi __asm__("rsi") = pointer;
  register uint64_t rdx __asm__("rdx") = length;
  __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi), "S"(rsi), "d"(rdx)
                   : "rcx", "r8", "r9", "r10", "r11", "memory");
  return rax;
}

int aiueos_syscall_self_test(void) {
  static const char message[] = "capability-bound";
  if (invoke(AIUEOS_SYSCALL_ABI, 0, 0, 0) != AIUEOS_SYSCALL_ABI_V1) return 0;
  if (invoke(AIUEOS_SYSCALL_LOG_WRITE, log_handle, message,
             sizeof(message) - 1) != sizeof(message) - 1) return 0;
  if (invoke(AIUEOS_SYSCALL_LOG_WRITE, log_handle ^ 0x10000ULL, message, 1) !=
      AIUEOS_ERR_BAD_HANDLE) return 0;
  if (invoke(AIUEOS_SYSCALL_LOG_WRITE, log_handle,
             (const void *)0x0000800000000000ULL, 1) != AIUEOS_ERR_BAD_POINTER)
    return 0;
  if (invoke(AIUEOS_SYSCALL_LOG_WRITE, log_handle,
             (const void *)0x3fffffffULL, 2) != AIUEOS_ERR_BAD_POINTER)
    return 0;
  if (invoke(AIUEOS_SYSCALL_LOG_WRITE, log_handle,
             (const void *)0x3fffffffULL, 1) != 1) return 0;
  if (invoke(AIUEOS_SYSCALL_LOG_WRITE, log_handle, message, 0) !=
      AIUEOS_ERR_BAD_POINTER) return 0;
  return 1;
}
