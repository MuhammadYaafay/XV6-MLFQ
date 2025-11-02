#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;
extern uint ticks;
int nextpid = 1;
struct spinlock pid_lock; // to prevent same PID for two processes

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;  //prevent race condition checking for dead child



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NEW MLFQ HELPER FUNCTIONS
// Get time slice for a given priority level
// Higher priority = shorter time slice for better responsiveness
int get_timeslice(int priority) {
  switch(priority) {
    case MLFQ_HIGH:   return TIMESLICE_HIGH;    // 4 ticks - shortest time slice
    case MLFQ_MEDIUM: return TIMESLICE_MEDIUM;  // 8 ticks - medium time slice
    case MLFQ_LOW:    return TIMESLICE_LOW;     // 16 ticks - longest time slice
    default:          return TIMESLICE_LOW;     // Default to lowest priority
  }
}

// Initialize MLFQ fields for a new process
// All new processes start at highest priority for best responsiveness
void init_mlfq_proc(struct proc *p) {
  p->priority = MLFQ_HIGH;                       // Start at highest priority
  p->timeslice = get_timeslice(MLFQ_HIGH);      // Set time slice for high priority
  p->timeslice_used = 0;                        // No time used yet
  p->runtime_ticks = 0;                         // No CPU time consumed
  p->scheduled_num = 0;                         // Not scheduled yet
  p->yield_io = 0;                              // Not yielded for I/O
}

// Anti-starvation mechanism: boost all processes to highest priority
// This prevents lower-priority processes from being starved indefinitely
void boost_all_priorities(void) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == RUNNABLE || p->state == RUNNING) {
      p->priority = MLFQ_HIGH;                  // Reset to highest priority
      p->timeslice = get_timeslice(MLFQ_HIGH);  // Reset time slice
      p->timeslice_used = 0;                    // Reset usage counter
    }
    release(&p->lock);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)  //kernel stack for all processes
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc(); //one page for process's kstack
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc)); // virtual address for this process's kstack
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W); //map kstack in kernel page table
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid() //which CPU core is executing this code
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off(); //disable interupt
  struct cpu *c = mycpu();
  struct proc *p = c->proc; //Get the process that's currently running on this CPU.The CPU struct has a field proc that points to the active process
  pop_off(); //enable interupt
  return p; 
}

int
allocpid()
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
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  //for getprocinfo
  p->first_scheduled=0;
  p->created=ticks;
  p->runtime_ticks = 0;
  p->scheduled_num=0;
  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){  //The trapframe stores user registers when the process traps into the kernel (system call, interrupt, exception)
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));  //Context saves kernel registers when switching between processes
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  init_mlfq_proc(p);

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
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
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable; //A trampoline page for switching modes.A trapframe page for saving CPU registers.

}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
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
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
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

  p->exited = ticks;

  int is_name_sh = (p->name[0] == 's' && p->name[1] == 'h' && p->name[2] == '\0'); //cuz it kept showing the exit of shell when it forked to run in bg

  if (!is_name_sh) {
    printf("| Exited at          |  %d\n", p->exited);
    printf("| Turnaround Time    | %d\n", p->exited - p->created);
    printf("=============================================\n\n");
  }



  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
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
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}




// Round Robin comment out

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
// void
// scheduler(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();

//   c->proc = 0;
//   for(;;){
//     // The most recent process to run may have had interrupts
//     // turned off; enable them to avoid a deadlock if all
//     // processes are waiting. Then turn them back off
//     // to avoid a possible race between an interrupt
//     // and wfi.
    
//     intr_on();

//     int found = 0;
//     for(p = proc; p < &proc[NPROC]; p++) {
//       acquire(&p->lock);
//       if(p->state == RUNNABLE) {

//         p->scheduled_num++;     //times a process has been scheduled
//         if(p->first_scheduled==0){
//           p->first_scheduled=ticks;
//         }

//         p->state = RUNNING;
//         c->proc = p;
//         swtch(&c->context, &p->context);

//         // Process is done running for now.
//         // It should have changed its p->state before coming back.
//         c->proc = 0;
//         found = 1;
//       }
//       release(&p->lock);
//     }
//     if(found == 0) {
//       // nothing to run; stop running on this core until an interrupt.
//       asm volatile("wfi");
//     }
//   }
// }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// MLFQ SCHEDULER IMPLEMENTATION
// Implements 3 priority levels with different time slices
void
scheduler(void)
{
  struct proc *p;                    // Pointer to current process being considered
  struct cpu *c = mycpu();           // Get current CPU structure
  
  c->proc = 0;                       // Initialize CPU's current process to null
  for(;;){                           // Infinite scheduling loop
    // Enable interrupts to avoid deadlock when all processes are waiting
    intr_on();                       // Turn on interrupts
    intr_off();                      // Turn off interrupts immediately

    int found = 0;                   // Flag to track if we found a runnable process
    
    // Search for runnable processes starting from highest priority (0) to lowest (2)
    for(int priority = MLFQ_HIGH; priority <= MLFQ_LOW; priority++) {
      
      // Scan all processes in the process table for this priority level
      for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);           // Acquire process lock for thread safety
        
        // Check if process is runnable and at current priority level
        if(p->state == RUNNABLE && p->priority == priority) {
          
          // Found a runnable process at this priority level
          p->state = RUNNING;        // Change process state to running
          c->proc = p;               // Assign process to current CPU
          p->scheduled_num++;        // Increment scheduling counter for statistics
          
          // Set first_scheduled time if this is the first time
          if (p->first_scheduled == 0) {
              p->first_scheduled = ticks;
          }
          
          p->timeslice_used = 0;     // Reset time slice usage counter
          p->yield_io = 0;           // Reset I/O yield flag
          
          // Context switch to the selected process
          swtch(&c->context, &p->context);

          // Process has returned control to scheduler
          // Process should have changed its state before coming back
          c->proc = 0;               // Clear CPU's current process
          found = 1;                 // Mark that we found and ran a process
          
          // Check why process returned: time slice expired or I/O yield
          if(!p->yield_io && p->timeslice_used >= p->timeslice) {
            // Time slice expired - demote process to lower priority
            if(p->priority < MLFQ_LOW) {
              p->priority++;         // Move to lower priority level (higher number)
              p->timeslice = get_timeslice(p->priority);  // Update time slice for new priority
            }
          }
          // If process yielded for I/O (yielded_io == 1), keep same priority
          
          // Reset time slice usage for next scheduling
          p->timeslice_used = 0;
        }
        release(&p->lock);           // Release process lock
      }
      
      // If we found a process at this priority level, don't check lower priorities
      if(found) break;               // Exit priority loop, start fresh in main loop
    }
    
    // If no runnable processes found at any priority level, wait for interrupt
    if(found == 0) {
      asm volatile("wfi");           // Wait for interrupt instruction
    }
  }
}



// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}


// Round Robin comment out
// Give up the CPU for one scheduling round
// void
// yield(void)
// {
//   struct proc *p = myproc();
//   acquire(&p->lock);
//   p->state = RUNNABLE;
//   sched();
//   release(&p->lock);
// }


// NEW YIELD FUNCTION FOR MLFQ 
// Modified yield function to handle I/O yielding in MLFQ
// User-initiated yield for I/O operations
void
yield(void)
{
  struct proc *p = myproc();         // Get current process
  acquire(&p->lock);                 // Acquire process lock
  
  p->yield_io = 1;                   // Mark that process yielded for I/O (not time slice expiration)
  p->state = RUNNABLE;               // Change process state to runnable
  sched();                           // Switch to scheduler
  
  release(&p->lock);                 // Release process lock
}

// Kernel-initiated yield for time slice expiration
void
yield_timeslice(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  
  p->yield_io = 0;           // Demote priority (timeslice expired)
  p->state = RUNNABLE;
  sched();
  
  release(&p->lock);
}

// NEW MLFQ TIMER TICK HANDLER 
// Timer interrupt handler - called every clock tick
// This function should be called from trap.c when a timer interrupt occurs

// Global starvation prevention counter
static int starvation_counter = 0;
#define STARVATION_THRESHOLD 1000  // Boost all processes every 1000 ticks

void
mlfq_tick(void)
{
  struct proc *p = myproc();         // Get current running process
  
  // Increment global starvation counter
  starvation_counter++;
  
  // Check for starvation prevention - boost all processes periodically
  if(starvation_counter >= STARVATION_THRESHOLD) {
    boost_all_priorities();          // Reset all processes to HIGH priority
    starvation_counter = 0;          // Reset counter
  }
  
  if(p != 0 && p->state == RUNNING) { // Check if there's a running process
    acquire(&p->lock);               // Acquire process lock for thread safety
    
    p->runtime_ticks++;              // Increment total CPU ticks consumed by process
    p->timeslice_used++;             // Increment time slice usage for current scheduling round
    
    // Check if time slice is exhausted for current priority level
    if(p->timeslice_used >= p->timeslice) {
      p->yield_io = 0;               // Mark as time slice expiration (not I/O yield)
      release(&p->lock);             // Release process lock
      yield_timeslice();                       // Force process to yield CPU
      return;
    }
    
    release(&p->lock);               // Release process lock
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
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

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
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

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
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
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

uint64
ps(void)
{
  struct proc *p;
  printf("PID\tState\t\tName\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    char *state;
    switch(p->state){
    case SLEEPING:
      state = "SLEEPING";
      break;
    case RUNNING:
      state = "RUNNING";
      break;
    case RUNNABLE:
      state = "RUNNABLE";
      break;
    default:
      state = "OTHER";
      break;
    }
    printf("%d\t%s\t\t%s\n", p->pid, state, p->name);
  }
  return 0;
}
