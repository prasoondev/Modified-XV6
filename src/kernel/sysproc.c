#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
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
  if (growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (killed(myproc()))
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
sys_waitx(void)
{
  uint64 addr, addr1, addr2;
  uint wtime, rtime;
  argaddr(0, &addr);
  argaddr(1, &addr1); // user virtual memory
  argaddr(2, &addr2);
  int ret = waitx(addr, &wtime, &rtime);
  struct proc *p = myproc();
  if (copyout(p->pagetable, addr1, (char *)&wtime, sizeof(int)) < 0)
    return -1;
  if (copyout(p->pagetable, addr2, (char *)&rtime, sizeof(int)) < 0)
    return -1;
  return ret;
}

uint64
sys_getSysCount(void){
  struct proc *p=myproc();
  if(p->sys_mask!=0){
    int u_c;
    argint(0, &u_c);
    return p->sys_c[u_c];
  }
  int u_mask;
  argint(0, &u_mask);
  p->sys_mask=u_mask;
  memset(p->sys_c,0,sizeof(p->sys_c)/sizeof(p->sys_c[0]));
  return 0;
}

uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler_addr;
  struct proc *p = myproc();
  argint(0, &interval);
  argaddr(1, &handler_addr);
  if(p->cputicks<0){
    return -1;
  }
  p->cputicks = interval;
  p->tickcount = 0;
  p->handler = (void (*)())handler_addr;
  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  memmove(p->trapframe, p->savedtf, sizeof(struct trapframe));
  p->handlerflag=0;
  return p->trapframe->a0;
}

uint64
sys_settickets(void)
{
  struct proc *p = myproc();
  int u_ticket;
  argint(0,&u_ticket);
  p->ticket=u_ticket;
  return 0;
}
