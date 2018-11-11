#include "x86.h"
#include "device.h"
#define SYS_EXIT 1
#define SYS_FORK 2
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_SLEEP 7
#define SEM_INIT 8
#define SEM_POST 9
#define SEM_WAIT 10
#define SEM_DESTROY 11

#define WIDTH 80
#define HEIGHT 24
#define USER_SPACE (0x6000000);

extern struct processTable pcb[MAX_PCB_NUM];
extern SegDesc gdt[NR_SEGMENTS];
extern int pcb_runnable;
extern int pcb_blocked;
extern struct processTable idlePCB;
extern TSS tss;
struct Semaphore semTable[10];
int sem_table_index = 0;

int fork_mem = 0x300000;
int pidNow = 3001;

int pos = 0;

void syscallHandle(struct TrapFrame *tf);

void GProtectFaultHandle(struct TrapFrame *tf);

void TimerHandle(struct TrapFrame *tf);

void putchar(char c);

void irqHandle(struct TrapFrame *tf) {
	/*
	 * 中断处理程序
	 */
	/* Reassign segment register */
	asm volatile("movw $0x10, %ax");
	asm volatile("movw %ax, %ds");
	switch(tf->irq) {
		case -1:
			break;
		case 0xd:
			GProtectFaultHandle(tf);
			break;
		case 0x80:
			syscallHandle(tf);
			break;
		case 0x20:
			TimerHandle(tf);
			break;
		default:assert(0);
	}
}

void Updatepcb(struct TrapFrame *tf)
{
	int eax = pcb[0].tf.eax;
	pcb[0].tf = *tf;
	pcb[0].tf.eax = eax;
}

void switchToProcess()
{
	if(pcb_runnable > 1)
	{
		pcb[0] = pcb[1];
		pcb[0].state = STATE_RUNNING;
		for(int i = 2; i < pcb_runnable; i++)
			pcb[i-1] = pcb[i];
		pcb_runnable --;
		tss.ss0 = KSEL(SEG_KDATA);
		tss.esp0 = (uint32_t)&pcb[0].tf.edi-4;

		if(pcb[0].pid != 3000)
			pcb[0].tf.ds = USEL(SEG_CHILD_UDATA);
		else
			pcb[0].tf.ds = USEL(SEG_UDATA);

		asm volatile("movl %%eax, %%esp"::"a"(&pcb[0].tf.edi));
		asm volatile("popal");
		asm volatile("pop %gs");
		asm volatile("pop %fs");
		asm volatile("pop %es");
		asm volatile("pop %ds");
                asm volatile("addl $4, %esp");
                asm volatile("addl $4, %esp");
        	asm volatile("iret");
	}
	else {
		tss.ss0 = KSEL(SEG_KDATA);
		tss.esp0 = (uint32_t)&pcb[0].tf.edi-4;
		pcb[0] = idlePCB;
		asm volatile("sti");
		((void(*)(void))pcb[0].tf.eip)();
	}
}

int sys_exit() {
	switchToProcess();
	return 0;
}
int sys_fork() {
	//malloc a block of memory  and copy father process's memory and stack 
	//get a PCB for child process	
	
	uint32_t user_space = (0x6000000) + pcb[0].tf.esp - (128 << 20);
	pcb[pcb_runnable] = pcb[0];
	pcb[pcb_runnable].state = STATE_RUNNABLE;
	pcb[pcb_runnable].memory = fork_mem;
	pcb[pcb_runnable].tf.cs = USEL(SEG_CHILD_UCODE);
	pcb[pcb_runnable].tf.ds = USEL(SEG_CHILD_UDATA);
	pcb[pcb_runnable].tf.ss = USEL(SEG_CHILD_UDATA);
	pcb[pcb_runnable].tf.esp = user_space - 0x100000;
	pcb[pcb_runnable].tf.ebp = user_space - 0x100000;
//	pcb[pcb_runnable].tf.eip = pcb[pcb_runnable].memory - pcb[0].memory + pcb[0].tf.eip - 0x100000;
	pcb[pcb_runnable].tf.eax = 0;
	pcb[pcb_runnable].pid = pidNow;
	pcb[0].tf.eax = pidNow;
	pcb[0].tf.ds = USEL(SEG_UDATA);

	if(fork_mem > 0x1000000)
		return -1;
	int i = 0;
	for(int paddr = fork_mem; paddr < fork_mem + 0x2000; paddr++, i++)
		*(char *)paddr = *(char *)(pcb[0].memory + i);

/*	i = 0x1600;
	for(int paddr = fork_mem + 0x1600; paddr < fork_mem + 0x1600 + 0x300; paddr++)
		*(char *)paddr = *(char *)(pcb[0].memory + i);
*/
	i = 0;
	for(int paddr2 = 0x6000000; paddr2 >= user_space; paddr2--, i++)
		*(char *)paddr2 = *(char *)((128 << 20) - i);
 
	pcb_runnable ++;
	pidNow ++;
	return pcb[0].tf.eax;
}

int sys_sleep(int time) {
	pcb[0].sleepTime = time;
	pcb[pcb_blocked] = pcb[0];
	pcb[pcb_blocked].state = STATE_BLOCKED;
	pcb_blocked ++;
	switchToProcess();
	return 0;
}

size_t sys_write(int fd, void *buf, size_t len)
{
	assert(fd <= 2);
	char* s = (char *)buf;
	int i = 0;
	while(s[i] != '\0'){
		putchar(s[i]);
		i++;
	}	
	return len;
}
	
int sys_sem_init(int v)
{
	semTable[sem_table_index].value = v;
	semTable[sem_table_index++].list_len = 0;
	return sem_table_index-1;
}

int sys_sem_post(sem_t *sem)
{
	int index = (int)sem;
	if(index < 0)
		return -1;
//	index = 0;
	
	semTable[index].value ++;
	if(semTable[index].value <= 0)
	{
		int len = semTable[index].list_len;
		if(len > 0)
		{
			int i = 10;
			for(; i < pcb_blocked; i++)
				if(pcb[i].pid == semTable[index].list[len-1])
					break;
			if(i == pcb_blocked)
				return -1;
			semTable[index].list_len--;
			pcb[pcb_runnable] = pcb[i];
			pcb[pcb_runnable++].state = STATE_RUNNABLE;
			for(int j = i+1; j < pcb_blocked; j++)
				pcb[j-1] = pcb[j];
			pcb_blocked --;
			if(pcb[0].pid == 1000)
				switchToProcess();
		}
	}
	return 0;
}

int sys_sem_wait(sem_t *sem)
{
	int index = (int)sem;
	if(index < 0)
		return -1;

//	if(index == 0)
//		putchar('A');
//	else {
//		putchar('P');
//	}
//	index = 0;
	semTable[index].value --;
	if(semTable[index].value < 0)
	{
		//block the process now
		//mov pcb to semTable.list
		int index = (int)sem;	
		if(index < 0)
			return -1;
		int len = semTable[index].list_len;
		semTable[index].list_len ++;
		semTable[index].list[len] = pcb[0].pid;
		pcb[pcb_blocked] = pcb[0];
		pcb[pcb_blocked++].state = STATE_BLOCKED;
		switchToProcess();
	}
	return 0;
}

int sys_sem_destroy(sem_t* sem)
{
	int index = (int)sem;
	if(index < 0)
		return -1;
	for(int j = index +1; j < sem_table_index; j++)
		semTable[j-1] = semTable[j];
	sem_table_index--;
	return 0;
}
void syscallHandle(struct TrapFrame *tf) {
	/* 实现系统调用*/
	switch(tf->eax) {
		case SYS_EXIT:
			Updatepcb(tf);
			tf->eax = sys_exit(); break;
		case SYS_FORK: 
			Updatepcb(tf);
			tf->eax = sys_fork();
			break;
		case SYS_READ: 
			assert(0); break;
		case SYS_WRITE:
			if(pcb[0].pid != 3000)
				tf->ecx += 0x100000; 
			tf->eax = sys_write(tf->ebx, (void *)(tf->ecx), tf->edx); break;	
		case SYS_OPEN: 
			assert(0); break;
		case SYS_CLOSE: 
			assert(0); break;
		case SYS_SLEEP:
			Updatepcb(tf);
			tf->eax = sys_sleep(tf->edx); break;
		case SEM_INIT:
			Updatepcb(tf);
			tf->eax = sys_sem_init(tf->edx); break;
		case SEM_POST:
			Updatepcb(tf);
			tf->eax = sys_sem_post((sem_t *)tf->edx); break;
		case SEM_WAIT:
			Updatepcb(tf);
			tf->eax = sys_sem_wait((sem_t *)tf->edx); break;
		case SEM_DESTROY:
			Updatepcb(tf);
			tf->eax = sys_sem_destroy((sem_t *)tf->edx); break;
		default: break;
	}
}

void TimerHandle(struct TrapFrame *tf) {

	//blocked process's sleeptime--
	int i = 10;
	while(i < pcb_blocked) {
		pcb[i].sleepTime --;
		if(pcb[i].sleepTime == 0)
		{
			pcb[pcb_runnable] = pcb[i];
			pcb[pcb_runnable++].state = STATE_RUNNABLE;
			for(int j = i+1; j < pcb_blocked; j++)
				pcb[j-1] = pcb[j];
			pcb_blocked --;
			if(pcb[0].pid == 1000)
				switchToProcess();
		}
		else
			i++;
	}

	//running process's timeCount --
	pcb[0].timeCount --;
	if(pcb[0].timeCount == 0)
	{
		pcb[pcb_runnable] = pcb[0];
		pcb[pcb_runnable++].state = STATE_RUNNABLE;
		switchToProcess();
		assert(0);
	}
}
void GProtectFaultHandle(struct TrapFrame *tf){
	assert(0);
	return;
}

void putchar(char c){
	short *Gs = (short *)0xB8000;
	if(c == '\n'){
		int nextpos = (pos/WIDTH + 1)*WIDTH;
		for(;pos < nextpos; pos++)
			*(Gs + pos) = 0;
		pos = nextpos;
	}
	else {
		*(Gs + pos) = 0xc00 + c;
		pos++;
	}
}

