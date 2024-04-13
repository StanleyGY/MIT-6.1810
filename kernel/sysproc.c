#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
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
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  #ifdef LAB_TRAPS
  backtrace();
  #endif
  return 0;
}

#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  uint64 vaddr;
  uint64 uAddr;
  int numPages;
  unsigned int abits = 0;
  struct proc *p = myproc();

  argaddr(0, &vaddr);
  argint(1, &numPages);
  argaddr(2, &uAddr);

  if (numPages > 32) {
    return -1;
  }

  for (int i = 0; i < numPages; i++) {
    pte_t *pte = walk(p->pagetable, vaddr + i * PGSIZE, 0);
    if (*pte & PTE_A) {
      // Page was accessed since the last call
      abits |= (1 << i);
      // Clear access bit
      *pte &= ~PTE_A;
    }
  }

  // Copy the kernel results to user addr
  if (copyout(p->pagetable, uAddr, (char *)&abits, sizeof(unsigned int)) < 0) {
    return -1;
  }
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
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

uint64
sys_trace(void)
{
  int mask;

  argint(0, &mask);
  myproc()->tmask = mask;
  return 0;
}

uint64
sys_sysinfo(void)
{
  uint64 userinfo;
  struct sysinfo kinfo;
  struct proc *p = myproc();

  argaddr(0, &userinfo);  // user pointer for sysinfo struct

  kinfo.freemem = mem_freebytes();
  kinfo.nproc = proc_countactive();

  if(copyout(p->pagetable, userinfo, (char *)&kinfo, sizeof(struct sysinfo)) < 0) {
    return -1;
  }
  return 0;
}
