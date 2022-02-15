#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
	int n;
	if (argint(0, &n) < 0)
		return -1;
	exit(n);
	return 0; // not reached
}

uint64
sys_getpid(void)
{
	return myproc()->pid;
}

uint64
sys_fork(void)
{
	return fork();
}

uint64
sys_wait(void)
{
	uint64 p;
	if (argaddr(0, &p) < 0)
		return -1;
	return wait(p);
}

uint64
sys_sbrk(void)
{
	int addr;
	int n;

	if (argint(0, &n) < 0)
		return -1;
	addr = myproc()->sz;
	if (growproc(n) < 0)
		return -1;
	return addr;
}

uint64
sys_sleep(void)
{
	int n;
	uint ticks0;

	if (argint(0, &n) < 0)
		return -1;
	acquire(&tickslock);
	ticks0 = ticks;
	while (ticks - ticks0 < n)
	{
		if (myproc()->killed)
		{
			release(&tickslock);
			return -1;
		}
		sleep(&ticks, &tickslock);
	}
	release(&tickslock);
	return 0;
}

uint64
sys_kill(void)
{
	int pid;

	if (argint(0, &pid) < 0)
		return -1;
	return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
	uint xticks;

	acquire(&tickslock);
	xticks = ticks;
	release(&tickslock);
	return xticks;
}

// for mp3
// 1. If a program calls thrdstop(), after ticks ticks that this program consumes,
//    switch to execute the thrdstop_handler. After you switch to thrdstop_handler
//    for the first time, you shouldn’t switch to it for the second time.
//    The effect of this syscall just for one time.
// 2. Store the current program context according to thrdstop_context_id.
uint64
sys_thrdstop(void)
{
	int interval, thrdstop_context_id;
	uint64 handler;
	if (argint(0, &interval) < 0)
		return -1;
	if (argint(1, &thrdstop_context_id) < 0)
		return -1;
	if (argaddr(2, &handler) < 0)
		return -1;

	struct proc *thisProc = myproc();
	// int thrdstop_ticks; It should record how many ticks have passed since the program.
	thisProc->thrdstop_ticks = 0;
	// int thrdstop_interval; It should store when we should switch to thrd_handler.
	// You can just store ticks, argument of thrdstop.
	thisProc->thrdstop_interval = interval;
	// uint64 thrdstop_handler_pointer; It should store where we need to switch.
	// You can just store thrdstop_handler, argument of thrdstop.
	thisProc->thrdstop_handler_pointer = handler;
	// The value of thrdstop_context_id is determined by thrdstop()
	// 1. If we call thrdstop(n, -1, handler), you should find an empty slot in
	//    thrdstop_context[MAX_THRD_NUM]. and store the empty slot index in
	//    thrdstop_context_id, then thrdstop() will return the empty slot index.
	//    Remember to set thrdstop_context_used[index] to 1.
	//    If you can’t find an empty slot, return -1;
	// 2. If we call thrdstop(n, i, handler), it means that we want to store the current
	//    context in thrdstop_context[i], set thrdstop_context_id as i, then thrdstop() will
	//    return i.
	if (thrdstop_context_id < 0)
	{
		for (thrdstop_context_id = 0; thrdstop_context_id < MAX_THRD_NUM; ++thrdstop_context_id)
			if (thisProc->thrdstop_context_used[thrdstop_context_id] == 0)
			{
				thisProc->thrdstop_context_used[thrdstop_context_id] = 1;
				thisProc->thrdstop_context_id = thrdstop_context_id;
				return thrdstop_context_id;
			}
		return -1;
	}
	thisProc->thrdstop_context_used[thrdstop_context_id] = 1;
	thisProc->thrdstop_context_id = thrdstop_context_id;
	return thrdstop_context_id;
}

// for mp3
// This function cancels the thrdstop(). It also save the current thread
// context into thrdstop_context[thrdstop_context_id], no need to store if
// thrdstop_context_id is -1.
// The return value is the time counted down by thrdstop().
// That is, return proc->thrdstop_ticks
uint64
sys_cancelthrdstop(void)
{
	int thrdstop_context_id;
	if (argint(0, &thrdstop_context_id) < 0)
		return -1;
	struct proc *thisProc = myproc();
	if (thrdstop_context_id >= 0)
	{
		thisProc->thrdstop_context_id = thrdstop_context_id;
		thisProc->thrdstop_context_used[thrdstop_context_id] = 1;
		struct thrd_context_data *thrd_context = &thisProc->thrdstop_context[thrdstop_context_id];
		struct trapframe *frame = thisProc->trapframe;
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
	}
	thisProc->thrdstop_interval = -1;
	return thisProc->thrdstop_ticks;
}

// for mp3
// 1. If is_exit is zero, reload the context stored in
//    thrdstop_context[thrdstop_context_id], continue to execute that context.
// 2. If is_exit isn’t zero, then thrdstop_context[thrdstop_context_id]
//    will become empty , and previous thrdstop() will be cancelled.
// 3. Return any value you want, we don’t use it.
uint64
sys_thrdresume(void)
{
	int thrdstop_context_id, is_exit;
	if (argint(0, &thrdstop_context_id) < 0)
		return -1;
	if (argint(1, &is_exit) < 0)
		return -1;
	struct proc *thisProc = myproc();
	if (thisProc == 0)
		return -1;
	if (is_exit == 0)
	{
		struct thrd_context_data *thrd_context = &thisProc->thrdstop_context[thrdstop_context_id];
		struct trapframe *frame = thisProc->trapframe;
		frame->s0 = thrd_context->s_regs[0];
		frame->s1 = thrd_context->s_regs[1];
		frame->s2 = thrd_context->s_regs[2];
		frame->s3 = thrd_context->s_regs[3];
		frame->s4 = thrd_context->s_regs[4];
		frame->s5 = thrd_context->s_regs[5];
		frame->s6 = thrd_context->s_regs[6];
		frame->s7 = thrd_context->s_regs[7];
		frame->s8 = thrd_context->s_regs[8];
		frame->s9 = thrd_context->s_regs[9];
		frame->s10 = thrd_context->s_regs[10];
		frame->s11 = thrd_context->s_regs[11];
		frame->a0 = thrd_context->a_regs[0];
		frame->a1 = thrd_context->a_regs[1];
		frame->a2 = thrd_context->a_regs[2];
		frame->a3 = thrd_context->a_regs[3];
		frame->a4 = thrd_context->a_regs[4];
		frame->a5 = thrd_context->a_regs[5];
		frame->a6 = thrd_context->a_regs[6];
		frame->a7 = thrd_context->a_regs[7];
		frame->t0 = thrd_context->t_regs[0];
		frame->t1 = thrd_context->t_regs[1];
		frame->t2 = thrd_context->t_regs[2];
		frame->t3 = thrd_context->t_regs[3];
		frame->t4 = thrd_context->t_regs[4];
		frame->t5 = thrd_context->t_regs[5];
		frame->t6 = thrd_context->t_regs[6];
		frame->sp = thrd_context->sp;
		frame->ra = thrd_context->ra;
		frame->gp = thrd_context->gp;
		frame->tp = thrd_context->tp;
		frame->epc = thrd_context->epc;
	}
	else
	{
		thisProc->thrdstop_context_used[thrdstop_context_id] = 0;
		thisProc->thrdstop_interval = -1;
	}
	return 0;
}
