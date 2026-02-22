.equ STACK_SIZE, 0X22000
.equ ADR_HINT, 0x600000000000
.global relocate_stack
.extern _start

relocate_stack:
    mov $9, %rax # __NR_MMAP
    movabs $ADR_HINT, %rdi
    mov $STACK_SIZE, %rsi
    mov $3, %rdx # PROT_READ | PROT_WRITE
    mov $34, %r10 # MAP_ANONYMOUS | MAP_PRIVATE
    mov $-1, %r8
    xor %r9, %r9
    syscall
    
    mov %rsp, %rbx
    mov (%rsp), %rcx # argc
    leaq 8(%rbx,%rcx,8), %rcx 
    add $8, %rcx

_find_envp_end:
    cmpq $0, (%rcx)
    je _find_envp_exit
    add $8, %rcx
    jmp _find_envp_end

_find_envp_exit:
    add $8, %rcx

_find_auxv_end:
    cmpq $0, (%rcx)
    je _find_auxv_exit
    add $16, %rcx
    jmp _find_auxv_end

_find_auxv_exit:
    add $16, %rcx

    sub %rbx, %rcx # stack size
    lea STACK_SIZE(%rax), %r12
    sub %rcx, %r12
    and $~15, %r12 # stack adr must be aligned to 16 byte boundary (ABI)

    mov %r12, %rdi
    mov %rbx, %rsi
    cld
    rep movsb
    
    mov %r12, %rsp
    
    jmp _start
    