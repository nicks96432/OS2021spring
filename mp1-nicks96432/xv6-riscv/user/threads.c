#include "kernel/types.h"
#include "user/threads.h"
#include "user/setjmp.h"
#include "user/user.h"
#define NULL 0

static struct thread *current_thread = NULL;
static int id = 1;
static jmp_buf env_st;
static jmp_buf env_tmp;

struct thread *thread_create(void (*f)(void *), void *arg)
{
	struct thread *t = (struct thread *)malloc(sizeof(struct thread));
	unsigned long new_stack_p;
	unsigned long new_stack;
	new_stack = (unsigned long)malloc(sizeof(unsigned long) * 0x100);
	new_stack_p = new_stack + 0x100 * 8 - 0x2 * 8;
	t->fp = f;
	t->arg = arg;
	t->ID = id;
	t->buf_set = 0;
	t->stack = (void *)new_stack;
	t->stack_p = (void *)new_stack_p;
	id++;
	return t;
}
void thread_add_runqueue(struct thread *t)
{
	if (current_thread == NULL) // 程式的開始
	{
		// TODO
		t->next = t;
		t->previous = t;
		current_thread = t;
	}
	else
	{
		// TODO
		t->next = current_thread;
		t->previous = current_thread->previous;
		current_thread->previous->next = t;
		current_thread->previous = t;
	}
}
void thread_yield(void)
{
	// TODO
	if (setjmp(current_thread->env) == 0)
	{
		schedule();
		dispatch();
	}
}
void dispatch(void)
{
	// TODO
	if (!current_thread->buf_set) // 第一次到這裡
	{
		// 要讓thread的stack pointer變成heap裡面的記憶體，不然stack會一直被洗掉
		if (setjmp(env_tmp) == 0)
		{
			env_tmp->sp = (unsigned long)current_thread->stack_p;
			longjmp(env_tmp, 1);
		}
		current_thread->buf_set = 1;
		current_thread->fp(current_thread->arg);
		thread_exit();
	}
	else
		longjmp(current_thread->env, 1);
}
void schedule(void)
{
	current_thread = current_thread->next;
}
void thread_exit(void)
{
	if (current_thread->next != current_thread)
	{
		// TODO
		struct thread *tmp = current_thread;
		schedule();
		tmp->next->previous = tmp->previous;
		tmp->previous->next = tmp->next;
		free(tmp->stack);
		free(tmp);
		dispatch();
	}
	else // 要回到main的方法是回到thread_start_threading()，所以要在那個函數裡面加一個setjmp
	{
		// TODO
		// Hint: No more thread to execute
		free(current_thread->stack);
		free(current_thread);
		longjmp(env_st, 1);
	}
}
void thread_start_threading(void)
{
	// TODO
	if (setjmp(env_st) == 0) // 第一次執行
	{
		schedule();
		dispatch();
	}
}
