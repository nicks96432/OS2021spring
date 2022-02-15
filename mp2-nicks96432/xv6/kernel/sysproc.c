#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fcntl.h"
#include "proc.h"
#include "fs.h"
#include "file.h"

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

#define MMAP_FAIL 0xffffffffffffffff

uint64 sys_mmap(void)
{
  //get args
  uint64 addr;
  int prot, flags, fd, offset, length;
  if (argaddr(0, &addr) < 0 || argint(1, &length) || argint(2, &prot) ||
      argint(3, &flags) || argint(4, &fd) || argint(5, &offset))
    return MMAP_FAIL;

  // addr will always be zero in MP2.
  // offset will always be zero in MP2.
  if (addr != 0 || offset != 0)
    return MMAP_FAIL;

  struct proc *p = myproc();

  // check file
  struct file *f;
  if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0 || f->type != FD_INODE)
    return MMAP_FAIL;

  // check file permission
  if ((flags & MAP_SHARED) && !(f->writable) && (prot & PROT_WRITE))
    return MMAP_FAIL;

  // find first unused VMA and move p->current_maxvma
  struct VMA *vma = 0;
  for (int i = 0; i < MAXVMA; ++i)
  {
    if (!p->VMAs[i].vm_used)
    {
      vma = &p->VMAs[i];
      break;
    }
  }
  if (!vma)
    return MMAP_FAIL;

  // initialize VMA
  vma->vm_start = PGROUNDDOWN(p->current_maxvma - length);
  vma->vm_end = PGROUNDDOWN(p->current_maxvma);
  vma->vm_file = f;
  vma->vm_prot = prot;
  vma->vm_flags = flags;
  vma->vm_used = 1;
  vma->vm_file->ref++;
  p->current_maxvma = vma->vm_start;
  return vma->vm_start;
}
uint64 sys_munmap(void)
{
  // get args
  uint64 addr;
  int length;
  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0)
    return MMAP_FAIL;

  struct proc *p = myproc();

  // find VMAs that addr belongs to
  struct VMA *vma = 0;
  for (int i = 0; i < MAXVMA; ++i)
    if (p->VMAs[i].vm_start <= addr && addr < p->VMAs[i].vm_end)
    {
      vma = &p->VMAs[i];
      break;
    }
  if (vma == 0)
    return MMAP_FAIL;

  if (addr + length > vma->vm_end)
    return MMAP_FAIL;
  // assumption 1: munmap always unmaps the multiple of the size of a page.
  // assumption 2: munmap always unmaps the mmap-ed region from starting point.
  uint64 pa;
  for (uint64 va = vma->vm_start; va < addr + length; va += PGSIZE)
    if ((pa = walkaddr(p->pagetable, va)))
    {
      // write to file if MAP_SHARED flag is set
      if (vma->vm_flags == MAP_SHARED)
        filewrite(vma->vm_file, va, PGSIZE);
      // unmap virtual memory
      uvmunmap(p->pagetable, va, 1, 1);
      // TODO: decrease physical memory reference count
    }
  // update VMA start address
  vma->vm_start += length;
  // free VMA if it's empty
  if (vma->vm_start == vma->vm_end)
  {
    vma->vm_file->ref--;
    vma->vm_used = 0;
  }

  return 0;
}

#undef MMAP_FAIL