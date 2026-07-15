.section .rodata.kotoba_user_image,"a",@progbits
.global aiueos_kotoba_user_elf_start
.global aiueos_kotoba_user_elf_end
aiueos_kotoba_user_elf_start:
.incbin AIUEOS_USER_ELF
aiueos_kotoba_user_elf_end:
.section .note.GNU-stack,"",@progbits
