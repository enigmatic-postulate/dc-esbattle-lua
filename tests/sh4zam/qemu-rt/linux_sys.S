/* TEST HARNESS ONLY - not part of the game, never built by the makefile.
   Lets an sh-elf (newlib, -m4-single) program run under qemu-sh4 linux-user: Linux syscalls for
   newlib, and an entry point that keeps the stack qemu sets up. long _linux_syscall(n, a, b, c) */
	.text
	.align 2
	.global	__linux_syscall
__linux_syscall:
	mov	r4, r3
	mov	r5, r4
	mov	r6, r5
	mov	r7, r6
	trapa	#0x13
	rts
	nop

/* Entry point: keep the stack qemu set up (qemu zero-fills .bss), put the FPU in
   KOS's startup mode (libgcc __set_fpscr, as KOS's startup.S and newlib's crt0 do), run main, exit. */
	.global	start
start:
	mov.l	@r15, r8		! argc (r8, r9 are callee-saved: they survive the calls below)
	mov	r15, r9
	add	#4, r9			! argv
	mov	#4, r4
	mov.l	set_fpscr_k, r1
	jsr	@r1
	shll16	r4			! __set_fpscr(0x40000): DN=1, round to nearest (KOS startup.S)
	lds	r3, fpscr
	mov.l	init_k, r0
	jsr	@r0
	nop
	mov	r8, r4
	mov	r9, r5
	mov.l	main_k, r0
	jsr	@r0
	nop
	mov.l	exit_k, r1
	jsr	@r1
	mov	r0, r4
	.align 2
set_fpscr_k:	.long	___set_fpscr
init_k:		.long	___libc_init_array
main_k:		.long	_main
exit_k:		.long	_exit
