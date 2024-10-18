#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#define NUM_Q 4

struct cpu cpus[NCPU];
struct queuemlfq queues[NUM_Q];
struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
struct queuemlfq* getq(){
  return queues;
}
int gethead(int num){
  return queues[num].head;
}
void createq(){
  for(int i=0;i<NUM_Q;i++){
    queues[i].head=-1;
    queues[i].rear=-1;
    queues[i].count=0;
  }
}
void push(int num,struct proc* p){
  // acquire(&(&queues[num])->lock);
  p->queuelevel=num;
  queues[num].head=(queues[num].head+1)%NPROC;
  if(queues[num].rear==-1){
    queues[num].rear=0;
  }
  queues[num].count++;
  p->inq=1;
  queues[num].arr[queues[num].head]=p;
  if(num!=0){
    p->timeleft=1<<(p->queuelevel+1);
  }
  else{
    p->timeleft=1;
  }
  // release(&(&queues[num])->lock);
}
void pop(int num,struct proc* p){
  // acquire(&(&queues[num])->lock);
  // int foundindex=-1;
  // for(int i=0;i<=queues[num].head;i++){
  //   if(queues[num].arr[i]==p){
  //     foundindex=i;
  //   }
  // }
  // if(foundindex==-1){
  //   release(&(&queues[num])->lock);
  //   return;
  // }
  queues[num].count--;
  p->inq=0;
  // for(int i=foundindex;i<queues[num].head;i++){
  //   queues[num].arr[i]=queues[num].arr[i+1];
  // }
  // queues[num].head--;
  if(queues[num].rear==queues[num].head){
    queues[num].rear=-1;
    queues[num].count=0;
    queues[num].head=-1;
  }
  else{
    queues[num].rear=(queues[num].rear+1)%NPROC;
  }
  // release(&(&queues[num])->lock);
}
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  if ((p->savedtf = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->rtime = 0;
  p->etime = 0;
  p->ctime = ticks;
  memset(p->sys_c,0,sizeof(p->sys_c)/sizeof(p->sys_c[0]));
  // p->sys_c=0;
  p->sys_mask=0;
  p->cputicks=0;
  p->tickcount=0;
  p->handlerflag=0;
  p->handler=0;
  p->ticket=1;
  p->inq=0;
  p->queuelevel=0;
  p->arrivaltime=ticks;
  p->timeleft=1;
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  if(p->savedtf)
    kfree((void *)p->savedtf);
  p->savedtf=0;
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  memset(p->sys_c,0,sizeof(p->sys_c)/sizeof(p->sys_c[0]));
  // p->sys_c=0;
  p->sys_mask=0;
  p->cputicks=0;
  p->inq=0;
  p->tickcount=0;
  p->queuelevel=0;
  // p->savedtf=0;
  p->handler=0;
  p->timeleft=0;
  p->handlerflag=0;
  p->ticket=0;
  p->arrivaltime=0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;
  release(&np->lock);
  for(int i=0;i<sizeof(np->sys_c)/sizeof(np->sys_c[0]);i++){
    np->sys_c[i]=0;
  }
  acquire(&wait_lock);
  np->parent = p;
  for(int i=0;i<sizeof(np->sys_c)/sizeof(np->sys_c[0]);i++){
    np->sys_c[i]=0;
  }
  np->sys_mask=0;
  np->arrivaltime=ticks;
  np->ticket=p->ticket;
  np->queuelevel=0;
  np->timeleft=1;
  np->inq=0;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);
  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();
  if(p->parent!=0&&p->parent->pid!=1){
    for(int i=0;i<sizeof(p->sys_c)/sizeof(p->sys_c[0]);i++){
      p->parent->sys_c[i]+=p->sys_c[i];
      // printf("%d ",p->sys_c[i]);
    }
    // printf("\n%d\n",p->pid);
    // printf("%d\n",p->sys_c[14]);
  }
  // printf("%d\n",p->pid);
  // p->inq=0;
  // memset(p->sys_c,0,33);

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }
    // #ifdef MLFQ
    //   if(p->inq==1){
    //     acquire(&(&queues[p->queuelevel])->lock);
    //     pop(p->queuelevel,p);
    //     release(&(&queues[p->queuelevel])->lock);
    //   }
    // #endif
    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
int seed=42949672;
int rand(int ticket) {
  seed = ( 101042* seed + 8755748) % ticket;
  return seed;
}
void scheduler(void)
{
  // struct proc *p;
  createq();
  struct cpu *c = mycpu();
  // printf("ke");
  c->proc = 0;
  for (;;)
  {
    // printf("k");
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    #ifdef LBS
    struct proc *p;
    struct proc *cur=0;
    int totalticket=0;
    for(p=proc;p<&proc[NPROC];p++){
      acquire(&p->lock);
      if(p->state==RUNNABLE){
        totalticket=totalticket+p->ticket;
      }
      release(&p->lock);
    }
    if(totalticket==0){
      continue;
    }
    int winner=rand(totalticket);
    int count=0;
    for(p=proc;p<&proc[NPROC];p++){
      acquire(&p->lock);
      if(p->state==RUNNABLE){
        count=count+p->ticket;        
      }
      if(p->state==RUNNABLE&&count>winner){
      // printf("%d %d\n",count,p->ticket);
        if(cur==0||(cur->ticket==p->ticket&&cur->arrivaltime>p->arrivaltime)||cur->ticket<p->ticket){
          cur=p;
        }     
      }
      release(&p->lock);
    }
    if(cur!=0){
      for(p=proc;p<&proc[NPROC];p++){
        acquire(&p->lock);
        if(p->state==RUNNABLE&&cur->ticket==p->ticket){
          if((cur->arrivaltime>p->arrivaltime)){
            cur=p;
          }     
        }
        release(&p->lock);
      }
    }
    if(cur!=0){
      acquire(&cur->lock);
      if(cur->state==RUNNABLE){
        cur->state=RUNNING;
        c->proc=cur;
        swtch(&c->context,&cur->context);
        c->proc=0;
      }
      release(&cur->lock);
    }
    #endif
    #ifdef MLFQ
    struct proc *p;
    // if(pboost_timer>=MLFQ_PBT){
    //   pboost_timer=0;
    //   for(int i=1;i<NUM_Q;i++){
    //     int temp=queues[i].count;
    //     int temp2=queues[i].rear;
    //     while(temp){
    //       acquire(&(&queues[i])->lock);
    //       struct proc* pt=queues[i].arr[temp2];
    //       acquire(&pt->lock);
    //       pop(pt->queuelevel,pt);
    //       pt->queuelevel=0;
    //       push(pt->queuelevel,pt);
    //       temp2=(temp2+1)%NPROC;
    //       temp--;
    //       release(&pt->lock);
    //       release(&(&queues[i])->lock);
    //     }
    //   }
    //   // for (p = proc; p < &proc[NPROC]; p++)
    //   // {
    //   //   acquire(&p->lock);
    //   //   if (p->inq == 1)
    //   //   {
    //   //     pop(p->queuelevel,p);
    //   //     p->queuelevel=0;
    //   //     push(p->queuelevel,p);
    //   //   }
    //   //   release(&p->lock);
    //   // }
    // }
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p!=0&&p->state==RUNNABLE&&p->inq==0)
      {
          acquire(&(&queues[p->queuelevel])->lock);
          push(p->queuelevel,p);
          release(&(&queues[p->queuelevel])->lock);
      }
      else if(p!=0&&p->state!=RUNNABLE&&p->inq==1){
        acquire(&(&queues[p->queuelevel])->lock);
        pop(p->queuelevel,p);
        release(&(&queues[p->queuelevel])->lock);
      }
      release(&p->lock);
    }
    p=0;
    for (int i = 0; i < NUM_Q; i++) {
    acquire(&queues[i].lock); // Acquire lock for the queue

    while (queues[i].count > 0) {
        p = queues[i].arr[queues[i].rear];

        // Acquire process lock
        acquire(&p->lock);

        // Remove process from the queue
        pop(p->queuelevel, p);

        // Check if the process is RUNNABLE
        if (p!=0&&p->state == RUNNABLE) {
            // Change state to RUNNING
            p->state = RUNNING;

            // Set the current process for the CPU
            c->proc = p;

            // Release the queue lock before switching context
            release(&queues[i].lock);

            // Perform the context switch
            swtch(&c->context, &p->context);

            // Reset the current process
            c->proc = 0;
            release(&p->lock);

            // Re-acquire the queue lock after switching back
            acquire(&queues[i].lock);
            continue;
        }

        // Release the process lock
        release(&p->lock);
    }

    // Release the queue lock
    release(&queues[i].lock);
}

    #endif
    #ifdef DEFAULT
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
    #endif
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  // printf("Yielding from process %d, queue level %d, time left %d\n", myproc()->pid, myproc()->queuelevel, myproc()->timeleft);
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      // #ifdef MLFQ
      // if(p->inq==1){
      //   acquire(&(&queues[p->queuelevel])->lock);
      //   pop(p->queuelevel,p);
      //   release(&(&queues[p->queuelevel])->lock);
      // }
      // #endif
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s %d %d %d heads: %d %d %d %d  rears: %d %d %d %d", p->pid, state, p->name,p->inq,p->queuelevel,p->timeleft,queues[0].head,queues[1].head,queues[2].head,queues[3].head,queues[0].rear,queues[1].rear,queues[2].rear,queues[3].rear);
    printf("\n");
  }
}

// waitx
int waitx(uint64 addr, uint *wtime, uint *rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (np = proc; np < &proc[NPROC]; np++)
    {
      if (np->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if (np->state == ZOMBIE)
        {
          // Found one.
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->etime - np->ctime - np->rtime;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                   sizeof(np->xstate)) < 0)
          {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || p->killed)
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

void update_time()
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNING)
    {
      p->timeleft--;
      p->rtime++;
    }
    // #ifdef MLFQ
    //   if (p->state == RUNNABLE || p->state == RUNNING)
    //   printf("%d %d %d\n", p->pid, ticks, p->queuelevel);
    // #endif
    release(&p->lock);
  }
}