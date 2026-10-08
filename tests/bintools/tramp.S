	.text
	.globl bt_call_checked
	.hidden bt_call_checked
# uint64 bt_call_checked(void *fn, void *a, void *b, void *c)
# Calls fn(a, b, c) with the callee-saved registers set to sentinels and
# returns a bitmask of the ones that changed (bit 5: rsp not restored).
bt_call_checked:
	push %rbp
	mov %rsp, %rbp
	push %rbx
	push %r12
	push %r13
	push %r14
	push %r15
	sub $8, %rsp
	mov %rdi, %rax
	mov %rsi, %rdi
	mov %rdx, %rsi
	mov %rcx, %rdx
	movabs $0x1111111111111111, %rbx
	movabs $0x2222222222222222, %r12
	movabs $0x3333333333333333, %r13
	movabs $0x4444444444444444, %r14
	movabs $0x5555555555555555, %r15
	call *%rax
	xor %eax, %eax
	movabs $0x1111111111111111, %rcx
	cmp %rcx, %rbx
	je 1f
	or $1, %eax
1:	movabs $0x2222222222222222, %rcx
	cmp %rcx, %r12
	je 2f
	or $2, %eax
2:	movabs $0x3333333333333333, %rcx
	cmp %rcx, %r13
	je 3f
	or $4, %eax
3:	movabs $0x4444444444444444, %rcx
	cmp %rcx, %r14
	je 4f
	or $8, %eax
4:	movabs $0x5555555555555555, %rcx
	cmp %rcx, %r15
	je 5f
	or $16, %eax
5:	lea -48(%rbp), %rcx
	cmp %rcx, %rsp
	je 6f
	or $32, %eax
6:	lea -40(%rbp), %rsp
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rbx
	pop %rbp
	ret
	.section .note.GNU-stack,"",@progbits
