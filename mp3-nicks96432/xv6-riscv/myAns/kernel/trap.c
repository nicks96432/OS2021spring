#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void)
{
	initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void)
{
	w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void)
{
	int which_dev = 0;

	if ((r_sstatus() & SSTATUS_SPP) != 0)
		panic("usertrap: not from user mode");

	// send interrupts and exceptions to kerneltrap(),
	// since we're now in the kernel.
	w_stvec((uint64)kernelvec);

	struct proc *p = myproc();

	// save user program counter.
	p->trapframe->epc = r_sepc();

	if (r_scause() == 8)
	{
		// system call

		if (p->killed)
			exit(-1);

		// sepc points to the ecall instruction,
		// but we want to return to the next instruction.
		p->trapframe->epc += 4;

		// an interrupt will change sstatus &c registers,
		// so don't enable until done with those registers.
		intr_on();

		syscall();
	}
	else if ((which_dev = devintr()) != 0)
	{
		// ok
	}
	else
	{
		printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
		printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
		p->killed = 1;
	}

	if (p->killed)
		exit(-1);

	// give up the CPU if this is a timer interrupt.
	if (which_dev == 2)
	{
		if (p->thrdstop_interval != -1)
			if (++p->thrdstop_ticks == p->thrdstop_interval)
			{
				p->thrdstop_interval = -1;
				p->thrdstop_ticks = 0;
				// struct thrd_context_data thrdstop_context[MAX_THRD_NUM]; You store the current context
				// in this array, struct thrd_context_data is a bunch of uint64 to store registers.
				// You can find definition of struct thrd_context_data and MAX_THRD_NUM in /kernel/proc.h
				struct thrd_context_data *thrd_context = &p->thrdstop_context[p->thrdstop_context_id];
				// current program context
				struct trapframe *frame = p->trapframe;
				// Because this syscall will have program switch to thrdstop_handler,
				// you should store the current program context.
				thrd_context->s_regs[0] = frame->s0;
				thrd_context->s_regs[1] = frame->s1;
				thrd_context->s_regs[2] = frame->s2;
				thrd_context->s_regs[3] = frame->s3;
				thrd_context->s_regs[4] = frame->s4;
				thrd_context->s_regs[5] = frame->s5;
				thrd_context->s_regs[6] = frame->s6;
				thrd_context->s_regs[7] = frame->s7;
				thrd_context->s_regs[8] = frame->s8;
				thrd_context->s_regs[9] = frame->s9;
				thrd_context->s_regs[10] = frame->s10;
				thrd_context->s_regs[11] = frame->s11;
				thrd_context->a_regs[0] = frame->a0;
				thrd_context->a_regs[1] = frame->a1;
				thrd_context->a_regs[2] = frame->a2;
				thrd_context->a_regs[3] = frame->a3;
				thrd_context->a_regs[4] = frame->a4;
				thrd_context->a_regs[5] = frame->a5;
				thrd_context->a_regs[6] = frame->a6;
				thrd_context->a_regs[7] = frame->a7;
				thrd_context->t_regs[0] = frame->t0;
				thrd_context->t_regs[1] = frame->t1;
				thrd_context->t_regs[2] = frame->t2;
				thrd_context->t_regs[3] = frame->t3;
				thrd_context->t_regs[4] = frame->t4;
				thrd_context->t_regs[5] = frame->t5;
				thrd_context->t_regs[6] = frame->t6;
				thrd_context->sp = frame->sp;
				thrd_context->ra = frame->ra;
				thrd_context->gp = frame->gp;
				thrd_context->tp = frame->tp;
				thrd_context->epc = frame->epc;
				frame->epc = p->thrdstop_handler_pointer;
			}
		yield();
	}
	// printf("aaaqq\n");
	usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
	struct proc *p = myproc();

	// we're about to switch the destination of traps from
	// kerneltrap() to usertrap(), so turn off interrupts until
	// we're back in user space, where usertrap() is correct.
	intr_off();

	// send syscalls, interrupts, and exceptions to trampoline.S
	w_stvec(TRAMPOLINE + (uservec - trampoline));

	// set up trapframe values that uservec will need when
	// the process next re-enters the kernel.
	p->trapframe->kernel_satp = r_satp();		  // kernel page table
	p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
	p->trapframe->kernel_trap = (uint64)usertrap;
	p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

	// set up the registers that trampoline.S's sret will use
	// to get to user space.

	// set S Previous Privilege mode to User.
	unsigned long x = r_sstatus();
	x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
	x |= SSTATUS_SPIE; // enable interrupts in user mode
	w_sstatus(x);

	// set S Exception Program Counter to the saved user pc.
	w_sepc(p->trapframe->epc);

	// tell trampoline.S the user page table to switch to.
	uint64 satp = MAKE_SATP(p->pagetable);

	// jump to trampoline.S at the top of memory, which
	// switches to the user page table, restores user registers,
	// and switches to user mode with sret.
	uint64 fn = TRAMPOLINE + (userret - trampoline);
	((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap()
{
	int which_dev = 0;
	uint64 sepc = r_sepc();
	uint64 sstatus = r_sstatus();
	uint64 scause = r_scause();

	if ((sstatus & SSTATUS_SPP) == 0)
		panic("kerneltrap: not from supervisor mode");
	if (intr_get() != 0)
		panic("kerneltrap: interrupts enabled");

	if ((which_dev = devintr()) == 0)
	{
		printf("scause %p\n", scause);
		printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
		panic("kerneltrap");
	}

	// give up the CPU if this is a timer interrupt.
	if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
	{
		struct proc *thisProc = myproc();
		if (thisProc->thrdstop_interval != -1)
			if (++thisProc->thrdstop_ticks == thisProc->thrdstop_interval)
			{
				thisProc->thrdstop_interval = -1;
				thisProc->thrdstop_ticks = 0;
				// struct thrd_context_data thrdstop_context[MAX_THRD_NUM]; You store the current context
				// in this array, struct thrd_context_data is a bunch of uint64 to store registers.
				// You can find definition of struct thrd_context_data and MAX_THRD_NUM in /kernel/proc.h
				struct thrd_context_data *thrd_context = &thisProc->thrdstop_context[thisProc->thrdstop_context_id];
				// current program context
				struct trapframe *frame = thisProc->trapframe;
				// Because this syscall will have program switch to thrdstop_handler,
				// you should store the current program context.
				thrd_context->s_regs[0] = frame->s0;
				thrd_context->s_regs[1] = frame->s1;
				thrd_context->s_regs[2] = frame->s2;
				thrd_context->s_regs[3] = frame->s3;
				thrd_context->s_regs[4] = frame->s4;
				thrd_context->s_regs[5] = frame->s5;
				thrd_context->s_regs[6] = frame->s6;
				thrd_context->s_regs[7] = frame->s7;
				thrd_context->s_regs[8] = frame->s8;
				thrd_context->s_regs[9] = frame->s9;
				thrd_context->s_regs[10] = frame->s10;
				thrd_context->s_regs[11] = frame->s11;
				thrd_context->a_regs[0] = frame->a0;
				thrd_context->a_regs[1] = frame->a1;
				thrd_context->a_regs[2] = frame->a2;
				thrd_context->a_regs[3] = frame->a3;
				thrd_context->a_regs[4] = frame->a4;
				thrd_context->a_regs[5] = frame->a5;
				thrd_context->a_regs[6] = frame->a6;
				thrd_context->a_regs[7] = frame->a7;
				thrd_context->t_regs[0] = frame->t0;
				thrd_context->t_regs[1] = frame->t1;
				thrd_context->t_regs[2] = frame->t2;
				thrd_context->t_regs[3] = frame->t3;
				thrd_context->t_regs[4] = frame->t4;
				thrd_context->t_regs[5] = frame->t5;
				thrd_context->t_regs[6] = frame->t6;
				thrd_context->sp = frame->sp;
				thrd_context->ra = frame->ra;
				thrd_context->gp = frame->gp;
				thrd_context->tp = frame->tp;
				thrd_context->epc = frame->epc;
				frame->epc = thisProc->thrdstop_handler_pointer;
			}
		yield();
	}

	// the yield() may have caused some traps to occur,
	// so restore trap registers for use by kernelvec.S's sepc instruction.
	w_sepc(sepc);
	w_sstatus(sstatus);
}

void clockintr()
{
	acquire(&tickslock);
	ticks++;
	wakeup(&ticks);
	release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr()
{
	uint64 scause = r_scause();

	if ((scause & 0x8000000000000000L) &&
		(scause & 0xff) == 9)
	{
		// this is a supervisor external interrupt, via PLIC.

		// irq indicates which device interrupted.
		int irq = plic_claim();

		if (irq == UART0_IRQ)
		{
			uartintr();
		}
		else if (irq == VIRTIO0_IRQ)
		{
			virtio_disk_intr();
		}
		else if (irq)
		{
			printf("unexpected interrupt irq=%d\n", irq);
		}

		// the PLIC allows each device to raise at most one
		// interrupt at a time; tell the PLIC the device is
		// now allowed to interrupt again.
		if (irq)
			plic_complete(irq);

		return 1;
	}
	else if (scause == 0x8000000000000001L)
	{
		// software interrupt from a machine-mode timer interrupt,
		// forwarded by timervec in kernelvec.S.

		if (cpuid() == 0)
		{
			clockintr();
		}

		// acknowledge the software interrupt by clearing
		// the SSIP bit in sip.
		w_sip(r_sip() & ~2);

		return 2;
	}
	else
	{
		return 0;
	}
}
