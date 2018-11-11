#include "x86.h"
#include "device.h"

SegDesc gdt[NR_SEGMENTS];
TSS tss;

struct processTable pcb[MAX_PCB_NUM];
struct processTable idlePCB;
int pcb_runnable = 1;  //maxindex of runnable process
int pcb_blocked = 10;  //maxindex of blocked process

#define SECTSIZE 512
#define Runnable 1 //runnable pcb start place
#define Blocked 10 //blocked pcb start place

void idle_process() {
	while(1){
		waitForInterrupt();
	}
}

void waitDisk(void) {
	while((inByte(0x1F7) & 0xC0) != 0x40); 
}

void readSect(void *dst, int offset) {
	int i;
	waitDisk();
	
	outByte(0x1F2, 1);
	outByte(0x1F3, offset);
	outByte(0x1F4, offset >> 8);
	outByte(0x1F5, offset >> 16);
	outByte(0x1F6, (offset >> 24) | 0xE0);
	outByte(0x1F7, 0x20);

	waitDisk();
	for (i = 0; i < SECTSIZE / 4; i ++) {
		((int *)dst)[i] = inLong(0x1F0);
	}
}

void initPCB(uint32_t entry, uint32_t size) {
	pcb[0].state = STATE_RUNNING;
	pcb[0].sleepTime = 0;
	pcb[0].timeCount = 1000;
	pcb[0].tf.eip = entry;
	pcb[0].tf.cs = USEL(SEG_UCODE);
	pcb[0].tf.ss = USEL(SEG_UDATA);  
	pcb[0].tf.eflags = 0x16;
	pcb[0].tf.esp = (128 << 20);
	pcb[0].tf.ebp = (128 << 20);
	pcb[0].pid = 3000;
	pcb[0].memory = entry;
	pcb[0].size = size;
	pcb[0].tf.ds = USEL(SEG_UDATA);
	
	idlePCB.state = STATE_RUNNING;
	idlePCB.sleepTime = 0;
	idlePCB.timeCount = 10000;
	idlePCB.tf.eip = (uint32_t)idle_process;
	idlePCB.tf.eflags = 0x12;
	idlePCB.tf.cs = KSEL(SEG_KCODE);
	idlePCB.tf.ss = KSEL(SEG_KDATA);
	idlePCB.tf.ds = KSEL(SEG_KDATA);
	idlePCB.tf.esp = (64 << 20);
	idlePCB.tf.ebp = (64<<20);
	idlePCB.memory = idlePCB.tf.eip;
	idlePCB.pid = 1000;
}

void initSeg() {
	gdt[SEG_KCODE] = SEG(STA_X | STA_R, 0,       0xffffffff, DPL_KERN);
	gdt[SEG_KDATA] = SEG(STA_W,         0,       0xffffffff, DPL_KERN);
	gdt[SEG_UCODE] = SEG(STA_X | STA_R, 0,       0xffffffff, DPL_USER);
	gdt[SEG_UDATA] = SEG(STA_W,         0,       0xffffffff, DPL_USER);
	gdt[SEG_TSS] = SEG16(STS_T32A,      &tss, sizeof(TSS)-1, DPL_KERN);
	gdt[SEG_TSS].s = 0;
	gdt[SEG_VIDEO] = SEG(STA_W,         0xb8000,  0xffffffff,DPL_KERN);
	gdt[SEG_CHILD_UCODE] = SEG(STA_X | STA_R, 0x100000, 0xffffffff, DPL_USER);
	gdt[SEG_CHILD_UDATA] = SEG(STA_W, 0x100000, 0xffffffff, DPL_USER);
	setGdt(gdt, sizeof(gdt));

	/*
	 * 初始化TSS
	 */
	tss.ss0 = KSEL(SEG_KDATA);
	tss.esp0 = (uint32_t)&pcb[0].tf.edi;
	asm volatile("ltr %%ax":: "a" (KSEL(SEG_TSS)));

	/*设置正确的段寄存器*/
	asm volatile("mov %%ax, %%ds"::"a"(KSEL(SEG_KDATA)));
	asm volatile("mov %%ax, %%ss"::"a"(KSEL(SEG_KDATA)));
	asm volatile("mov %%ax, %%es"::"a"(KSEL(SEG_KDATA)));
	asm volatile("mov %%ax, %%gs"::"a"(KSEL(SEG_VIDEO)));

	lLdt(0);
	
}

void enterUserSpace(uint32_t entry) {
	/*
	 * Before enter user space 
	 * you should set the right segment registers here
	 * and use 'iret' to jump to ring3
	 */
	short *Gs = (short *)0xB8000;
	for(int i = 0; i < 9; i++)
		for(int j = 0; j < 80; j++)
			*(Gs+i*80+j) = 0;
/*	asm volatile("pushl %%eax"::"a"(USEL(SEG_UDATA)));
	asm volatile("pushl $(128<<20)");
	asm volatile("pushf");
	asm volatile("pushl %%eax"::"a"(USEL(SEG_UCODE)));
	asm volatile("pushl $0x200000");
*/
	asm volatile("sti");
	asm volatile("movl %%eax, %%esp"::"a"(&pcb[0].tf.edi));
	asm volatile("popal");
	asm volatile("popl %gs");
	asm volatile("popl %fs");
	asm volatile("popl %es");
	asm volatile("popl %ds");
	asm volatile("addl $8, %esp");
	asm volatile("iret");
}
	
void loadUMain(void) {

	/*加载用户程序至内存*/

	struct ELFHeader *elf = (struct ELFHeader *)0x8000;
	struct ProgramHeader *ph, *pr;
	readSect((void *)elf, 201);
	ph = (struct ProgramHeader *)(0x8000 + elf->phoff);
	pr = ph + elf->phnum;
	for(;ph < pr; ph++) {
		int start = ph->off/SECTSIZE + 201;
		int end = (ph->off + ph->filesz)/SECTSIZE + 201; 
		for(int j = start; j <= end; j++) 
			readSect((void *)(ph->paddr + (j-start)*SECTSIZE),j);
		for(int j = ph->filesz; j < ph->memsz; j++) {
			*(char *)(ph->paddr + j) = 0;
		}
	}

	uint32_t entry = elf->entry;
	initPCB(entry, 4);
	enterUserSpace(entry);
}

