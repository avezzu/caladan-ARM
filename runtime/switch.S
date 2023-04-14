/*
 * switch.S - assembly routines for switching trap frames
 */

/*
 * Trap Frame Format
 * WARNING: These values reflect the layout of struct thread_tf. Don't change
 * these values without also updating defs.h.
 */

.file "switch.S"
.text

/* arguments registers (can be clobbered) */
#define _X0	#0
#define _X1	#8
#define _X2	#16
#define _X3	#24
#define _X4	#32
#define _X5	#40

/* temporary registers (can be clobbered) */
#define _X6	#48
#define _X7	#56

/* callee-saved registers (can not be clobbered) */
#define _X19 #64
#define _X29 #72
#define _X12 #80
#define _X13 #88
#define _X14 #96
#define _X15 #104 
#define _X16 #112
#define _X17 #120
#define _X18 #128
#define _X30 #136

/* special-purpose registers */
#define _PC	#144	/* instruction pointer */
#define _SP	#152	/* stack pointer */

/**
 * __jmp_thread - executes a thread from the runtime
 * @tf: the trap frame to restore (%rdi)
 *
 * This low-level variant isn't intended to be called directly.
 * Re-enables preemption, parking the kthread if necessary.
 * Does not return.
 */

.align 16
.globl __jmp_thread
.type __jmp_thread, @function
__jmp_thread:
	/* restore ip and stack */
	
	ldr x3, [x0, _SP]
	mov sp, x3
	ldr x2, [x0, _PC]

	/* restore callee regs */
	ldr x19, [x0, _X19]
	ldr x29, [x0, _X29]
	ldr x12, [x0, _X12]
	ldr x13, [x0, _X13]
	ldr x14, [x0, _X14]
	ldr x15, [x0, _X15]

	ldr x16, [x0, _X16]
	ldr x17, [x0, _X17]
	ldr x18, [x0, _X18]
	ldr x30, [x0, _X30]

	/* set first argument (in case new thread) */

	ldr x0, [x0, _X0] /* ARG0 */

	/* re-enable preemption */
	cbz x1, label1

	/* jump into trap frame */
	br x2
	nop

label1:	/* cold-path, save RIP and park the kthread */

	str x1, [sp, #-8]!
	str x0, [sp, #-8]!
	str x15, [sp, #-8]!

	mov	x15, sp
	sub sp, sp, #16 /* ensure correct stack alignment */
	bl preempt
	mov	sp, x15 /* restore SP */

	ldr x15, [sp], #8
	ldr x0, [sp], #8
	ldr x1, [sp], #8
	
	br x1

/**
 * __jmp_thread_direct - directly switches from one thread to the next
 * @oldtf: the trap frame to save (%rdi)
 * @newtf: the trap frame to restore (%rsi)
 * @thread_running: a pointer to whether the thread is still running (%rdx)
 *
 * This low-level variant isn't intended to be called directly.
 * Re-enables preemption, parking the kthread if necessary.
 * Does return.
 */
.align 16
.globl __jmp_thread_direct
.type __jmp_thread_direct, @function
__jmp_thread_direct:

	/* save ip and stack */
	ldr x4, [sp]
	str x4, [x0, _PC]

	add x4, sp, #8
	str x4, [x0, _SP]

	/* save callee regs */
	str x19, [x0, _X19]
	str x29, [x0, _X29]
	str x12, [x0, _X12]
	str x13, [x0, _X13]
	str x14, [x0, _X14]
	str x15, [x0, _X15]

	str x16, [x0, _X16]
	str x17, [x0, _X17]
	str x18, [x0, _X18]
	str x30, [x0, _X30]

	/* restore ip and stack */
	ldr x7, [x1, _SP]
	mov sp, x7
	ldr x4, [x1, _PC]
	
	/* clear the stack busy flag */
	mov x7, #0
	str x7, [x2]

	/* restore callee regs */
	ldr x19, [x1, _X19]
	ldr x29, [x1, _X29]
	ldr x12, [x1, _X12]
	ldr x13, [x1, _X13]
	ldr x14, [x1, _X14]
	ldr x15, [x1, _X15]
	ldr x16, [x1, _X16]
	ldr x17, [x1, _X17]
	ldr x18, [x1, _X18]
	ldr x30, [x1, _X30]

	/* set first argument (in case new thread) */
	ldr x0, [x1, _X0] /* ARG0 */

	/* re-enable preemption */
	
	sub x3, x3, #1
	cbz x3, label2

	/* jump into trap frame */
	br x4
	nop

label2:	/* cold-path, save RIP and park the kthread */

	str x4, [sp, #-8]!
	str x0, [sp, #-8]!
	str x15, [sp, #-8]!

	mov	x15, sp
	sub sp, sp, #16 /* ensure correct stack alignment */
	bl preempt
	mov	sp, x15 /* restore SP */

	ldr x15, [sp], #8
	ldr x0, [sp], #8
	ldr x4, [sp], #8
	
	br x4

/**
 * __jmp_runtime - saves the current trap frame and jumps to a function in the
 *                 runtime
 * @tf: the struct thread_tf to save state (%rdi)
 * @fn: the function pointer to call (%rsi)
 * @stack: the start of the runtime stack (%rdx)
 *
 * This low-level variant isn't intended to be called directly.
 * Must be called with preemption disabled.
 * No return value.
 */
.align 16
.globl __jmp_runtime
.type __jmp_runtime, @function
__jmp_runtime:
	/* save callee regs */
	str x19, [x0, _X19]
	str x29, [x0, _X29]
	str x12, [x0, _X12]
	str x13, [x0, _X13]
	str x14, [x0, _X14]
	str x15, [x0, _X15]

	str x16, [x0, _X16]
	str x17, [x0, _X17]
	str x18, [x0, _X18]
	str x30, [x0, _X30]

	/* save ip and stack */
	ldr x4, [sp]
	str x4, [x0, _PC]

	add x4, sp, #8
	str x4, [x0, _SP]

	/* jump into runtime function */
	mov sp, x2

	/* jump into runtime code */
	br x1

/**
 * __jmp_runtime_nosave - jumps to a function in the runtime without saving the
 *			  current stack frame
 * @fn: the function pointer to call (%rdi)
 * @stack: the start of the runtime stack (%rsi)
 *
 * This low-level variant isn't intended to be called directly.
 * Must be called with preemption disabled.
 * No return value.
 */
.align 16
.globl __jmp_runtime_nosave
.type __jmp_runtime_nosave, @function
__jmp_runtime_nosave:

	/* jump into runtime function */
	mov sp, x1
	mov x1, x0
	br x1
