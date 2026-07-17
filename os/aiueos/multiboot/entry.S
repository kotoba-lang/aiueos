/* Multiboot (v1) entry: QEMU's built-in Multiboot loader enters here in
 * 32-bit protected mode with paging off, eax = 0x2BADB002, ebx = info pointer.
 * This trampoline establishes long mode over an identity-mapped first GiB and
 * hands control to the 64-bit C landing. Each major step emits a byte on the
 * 0xE9 debug port so any hang localizes immediately. */

.section .text.mbentry,"ax",@progbits
.balign 8
.global aiueos_multiboot_header
aiueos_multiboot_header:
  .long 0x1BADB002                      /* magic */
  .long 0x00000003                      /* flags: page-align modules + mem info */
  .long -(0x1BADB002 + 0x00000003)      /* checksum */

/* Multiboot2 header, so GRUB's `multiboot2` command loads this same image.
   8-byte aligned, within the first 32 KiB of the file. GRUB enters at the ELF
   entry (the trampoline below) in 32-bit protected mode with eax = the MB2
   magic 0x36D76289 and ebx = the MB2 information structure. */
.balign 8
aiueos_multiboot2_header:
  .long 0xE85250D6                      /* MB2 magic */
  .long 0                               /* architecture: i386 (32-bit) */
  .long aiueos_multiboot2_header_end - aiueos_multiboot2_header
  .long -(0xE85250D6 + 0 + (aiueos_multiboot2_header_end - aiueos_multiboot2_header))
  /* end tag */
  .short 0
  .short 0
  .long 8
aiueos_multiboot2_header_end:

.code32
.global aiueos_multiboot_start
.type aiueos_multiboot_start,@function
aiueos_multiboot_start:
  cli
  cld
  movl %eax, aiueos_mb_magic
  movl %ebx, aiueos_mb_info
  movl $aiueos_mb_stack_top, %esp

  movb $0x31, %al                       /* '1' entered 32-bit */
  outb %al, $0xe9

  /* PML4[0] -> PDPT (4 x 1 GiB identity pages, first 4 GiB) so the LAPIC and
     IOAPIC MMIO windows (~0xFEE00000 / 0xFEC00000, just below 4 GiB) are
     mapped along with RAM. 1 GiB pages need PDPE1GB, which QEMU -cpu max has. */
  movl $aiueos_mb_pdpt, %eax
  orl  $0x3, %eax
  movl %eax, aiueos_mb_pml4
  movl $aiueos_mb_pdpt, %edi
  movl $0x83, %eax                      /* present + write + 1 GiB page, base 0 */
  movl $0, %ecx
1:
  movl %eax, (%edi)
  movl $0, 4(%edi)
  addl $0x40000000, %eax
  addl $8, %edi
  incl %ecx
  cmpl $4, %ecx
  jne 1b

  movb $0x32, %al                       /* '2' page tables built */
  outb %al, $0xe9

  movl $aiueos_mb_pml4, %eax
  movl %eax, %cr3
  movl %cr4, %eax
  orl  $0x20, %eax                       /* CR4.PAE */
  movl %eax, %cr4
  movl $0xC0000080, %ecx                 /* EFER */
  rdmsr
  orl  $0x100, %eax                      /* EFER.LME */
  wrmsr
  movl %cr0, %eax
  orl  $0x80000001, %eax                 /* CR0.PG | CR0.PE */
  movl %eax, %cr0

  movb $0x33, %al                       /* '3' long mode enabled */
  outb %al, $0xe9

  lgdt aiueos_mb_gdt_pointer
  ljmp $0x08, $2f

.code64
2:
  movw $0x10, %ax
  movw %ax, %ds
  movw %ax, %es
  movw %ax, %ss
  xorw %ax, %ax
  movw %ax, %fs
  movw %ax, %gs
  movq $aiueos_mb_stack_top, %rsp
  andq $-16, %rsp

  /* Enable SSE: the freestanding C is compiled at -O2, which may vectorize
     byte loops (an SSE instruction with SSE disabled would #UD, and with no
     IDT that triple-faults). Clear CR0.EM, set CR0.MP, set CR4.OSFXSR and
     CR4.OSXMMEXCPT. */
  movq %cr0, %rax
  andq $0xFFFFFFFFFFFFFFFB, %rax        /* clear EM (bit 2) */
  orq  $0x2, %rax                       /* set MP (bit 1) */
  movq %rax, %cr0
  movq %cr4, %rax
  orq  $0x600, %rax                     /* set OSFXSR (bit 9) | OSXMMEXCPT (bit 10) */
  movq %rax, %cr4

  movb $0x34, %al                       /* '4' 64-bit reached */
  outb %al, $0xe9

  xorq %rdi, %rdi
  movl aiueos_mb_magic(%rip), %edi
  xorq %rsi, %rsi
  movl aiueos_mb_info(%rip), %esi
  call aiueos_multiboot_main
3:
  cli
  hlt
  jmp 3b

/* Minimal APIC-timer ISR for the Multiboot path: bump the tick counter the
   shared apic.c increments elsewhere, acknowledge EOI through the LAPIC MMIO
   base apic.c published, and return. */
.global aiueos_mb_isr_timer
.type aiueos_mb_isr_timer,@function
aiueos_mb_isr_timer:
  push %rax
  incq aiueos_apic_timer_ticks(%rip)
  mov aiueos_apic_mmio_base(%rip), %rax
  movl $0, 0xb0(%rax)
  pop %rax
  iretq

/* Any unexpected vector before the timer test is a fault: emit a marker on the
   debug port and terminate deterministically (0x6d -> isa-debug-exit 219). */
.global aiueos_mb_isr_default
.type aiueos_mb_isr_default,@function
aiueos_mb_isr_default:
  movb $0x58, %al                       /* 'X' unexpected vector */
  outb %al, $0xe9
  movl $0x6d, %eax
  outl %eax, $0xf4
  cli
4:
  hlt
  jmp 4b

.global aiueos_mb_load_idt
.type aiueos_mb_load_idt,@function
aiueos_mb_load_idt:
  lidt (%rdi)
  ret

.section .data.mbgdt,"aw",@progbits
.balign 8
aiueos_mb_gdt:
  .quad 0x0000000000000000
  .quad 0x00af9a000000ffff              /* 64-bit code */
  .quad 0x00cf92000000ffff              /* data */
aiueos_mb_gdt_end:
aiueos_mb_gdt_pointer:
  .word aiueos_mb_gdt_end - aiueos_mb_gdt - 1
  .quad aiueos_mb_gdt

.section .bss.mb,"aw",@nobits
.balign 4096
aiueos_mb_pml4: .skip 4096
aiueos_mb_pdpt: .skip 4096
aiueos_mb_pd:   .skip 4096
aiueos_mb_magic: .skip 8
aiueos_mb_info:  .skip 8
.balign 16
aiueos_mb_stack_bottom: .skip 16384
aiueos_mb_stack_top:

.section .note.GNU-stack,"",@progbits
