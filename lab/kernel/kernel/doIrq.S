.code32

.global irqEmpty
irqEmpty:
	pushl $0 // push dummy error code
	pushl $-1 // push interrupt vector into kernel
	jmp asmDoIrq

.global irqGProtectFault
irqGProtectFault:
	pushl $0xd
	jmp asmDoIrq

.global irqSyscall
irqSyscall:
	pushl $0 // push dummy error code
	pushl $0x80 // push interrupt vector into kernel stack
	jmp asmDoIrq

.global irqTimer
irqTimer:
	pushl $0 // push error code
	pushl $0x20 //push interrupt vector into kernel stack
	jmp asmDoIrq

.global asmDoIrq
asmDoIrq:
	push %ds
	push %es
	push %fs
	push %gs
	pushal // push process state into kernel stack
	pushl %esp
	call irqHandle
	addl $4, %esp
	popal
	pop %gs
	pop %fs
	pop %es
	pop %ds
	addl $4, %esp //interrupt vector is on top of kernel stack
	addl $4, %esp //error code is on top of kernel stack
	iret
