#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1; // tid 부여를 위한 변수
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->is_thread = 0; // thread와 구분하기 위해 allocproc로 생성되는 process는 is_thread를 0으로 설정

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;

  // malloc() 호출시 thread 사이에 메모리 할당 공간이 겹치지 않고 계속 다음 메모리에 쌓일 수 있도록 하고, 
  // 같은 thread들 끼리는 할당된 메모리를 공유할 수 있도록 sz를 update 해줌
  acquire(&ptable.lock);
  for(struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == curproc->pid){ // process가 호출했을 수도 있기 때문에 curproc->master_thread->pid가 아닌 curproc->pid와 비교
      p->sz = curproc->sz; // thread마다 update된 sz를 공유
    }
  }
  release(&ptable.lock);

  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
// exit()를 수행하면 master_thread(=process)와, 같은 master_thread를 가지는 모든 thread들이 정리되어야 하므로,
// 호출한 process나 thread를 제외하고(curproc를 제외하고), 같은 pid를 가지는 나머지 process와 thread를 종료하는 함수 추가 
// (ptable을 사용하므로 사용하기 편하도록 proc.c에 해당 함수 정의)
void
exit(void)
{
  // cprintf("IN EXIT\n\n");
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  if(curproc->is_thread == 1){ // curproc가 thread인 경우 master_thread(=process)를 정리하면서 parent가 0이 되버리기 때문에,
    curproc->parent = curproc->master_thread->parent; // 여기서 master_thread의 parent를 미리 저장
    // curproc->is_thread = 0; // exit() 이후 같은 process나 thread의 exec()이 일어나는 경우는 없을 것이므로 안해줘도 된다고 생각하지만, 정확하지 않아서 일단 exec()에서 처럼 작성
  }
  // cprintf("tid : %d\n", curproc->tid);
  kill_all_threads_without_curproc(curproc); // curproc를 제외하고 master_thread(=process)와, 같은 master_thread를 가지는 모든 thread 정리
  // cprintf("parent pid is : %d\n\n", curproc->parent->pid); // 추가했던 부분

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int 
thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
{
  // ** allocproc를 변형해서 thread를 alloc 하는 부분 시작 **

  struct proc *nt; // new thread
  struct proc *curproc = myproc(); // parent process or thread
  char *sp;

  acquire(&ptable.lock);

  for(nt = ptable.proc; nt < &ptable.proc[NPROC]; nt++)
    if(nt->state == UNUSED)
      goto found;
  
  release(&ptable.lock);
  return -1; // UNUSED 프로세스를 찾지 못한 경우 -1 리턴 (실패)

found:
  nt->state = EMBRYO;
  nt->pid = curproc->pid; // thread는 page table을 공유하기 위해 create한 process의 pid를 저장
  nt->tid = nexttid++;    // 가장 먼저 만들어진 thread의 tid는 1으로 설정하고 pid처럼 tid를 1씩 증가하여 다음 thread에 할당
  *thread = nt->tid;      // thread_t *thread 변수에 할당한 tid 저장
  nt->is_thread = 1;      // thread를 할당했기 때문에 is_thread = 1으로 설정
  nt->parent = curproc;   // create한 process를 parent로 가짐

  if(curproc->is_thread == 0){    // create의 주체가 process인 경우
    nt->master_thread = curproc;  // create한 process를 master_thread로 가짐
  }
  else{ // create의 주체가 thread인 경우
    nt->master_thread = curproc->master_thread; // create한 thread의 master_thread를 master_thread로 가짐 (master_thread를 하나로 통일하기 위해)
  }

  release(&ptable.lock);

  // Allocate kernel stack.
  if((nt->kstack = kalloc()) == 0){
    nt->state = UNUSED;
    return -1; // kernel stack을 alloc하지 못한 경우 UNUSED로 돌리고 -1 리턴 (실패)
  }
  sp = nt->kstack + KSTACKSIZE; // kernel stack 할당

  // Leave room for trap frame.
  sp -= sizeof *nt->tf; // trapframe 크기의 공간 할당
  nt->tf = (struct trapframe*)sp; // thread의 trapframe을 가리키는 주소를 sp로 지정

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4; // trapret의 주소를 위한 공간 할당
  *(uint*)sp = (uint)trapret; // trapret의 주소 저장

  sp -= sizeof *nt->context; // context 크기의 공간 할당
  nt->context = (struct context*)sp; // thread의 context를 가리키는 주소를 sp로 지정
  memset(nt->context, 0, sizeof *nt->context); // context의 entry를 0으로 초기화
  nt->context->eip = (uint)forkret; // context의 eip(return address)에 forkret을 저장

  // ** 여기까지 allocproc()를 변형해서 thread를 alloc 하는 부분 끝 **
  // process fork와 달리 thread는 모든 page table을 복사해서 새로 만들 필요 없이 공유하면 되므로,
  // copyuvm() 대신 master_thread와 page table을 공유하면 되고 위에서 할당한 trapframe만 카피해오면됨
  nt->pgdir = nt->master_thread->pgdir; // copyuvm() 대신 master_thread의 page table을 공유
  *nt->tf = *nt->master_thread->tf;     // 위에서 할당한 trapframe에 master_thread의 trapframe을 저장

  // text, data, heap 영역은 page table을 통해 공유하고 stack만 thread 별로 가지면 되므로,
  // ** exec()를 변형해서 메모리에 stack을 할당해주는 부분 시작 **
  uint _sp;       // stack 위치 지정을 편하게 하기 위해서 stack pointer 역할을 하는 _sp 변수 선언
  uint ustack[2]; // fake return PC와 start_routine에 전달할 arg를 저장할 공간
  
  nt->master_thread->sz = PGROUNDUP(nt->master_thread->sz); // 새로운 stack을 기존 nt->master_thread->sz 위에 쌓음 (가장 가까운 페이지 단위에 맞춰서 올림 처리)
  
  if((nt->master_thread->sz = allocuvm(nt->master_thread->pgdir, nt->master_thread->sz, nt->master_thread->sz + 2*PGSIZE)) == 0){ 
    // 앞에서 새로 쌓은 nt->master_thread->sz에 새로운 stack frame을 할당
    nt->state = UNUSED; // 실패시 thread를 다시 UNUSED로 초기화해주고
    return -1; // -1 리턴
  }

  clearpteu(nt->master_thread->pgdir, (char*)(nt->master_thread->sz - 2*PGSIZE)); // 할당한 nt->master_thread->sz의 entry를 초기화
  _sp = nt->master_thread->sz; // stack pointer를 할당한 nt->master_thread->sz 로 이동

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = (uint)arg;   // arg 주소 (start_routine의 argument로 사용) 
  // 기존 exec와 달리 start_routine의 argument의 개수가 1개로 한정되어있으므로, argc로 argument 포인터를 순회하지 않아도 됨

  _sp -= 2 * sizeof(uint); // 위에서 설정한 ustack을 위한 공간 할당 (uint 자료형 2개 크기)
  
  if(copyout(nt->master_thread->pgdir, _sp, ustack, 2 * sizeof(uint)) < 0){ 
    // 앞에서 ustack을 위해 공간을 할당해준 _sp에 ustack을 실제 메모리로 copy하여 할당
    nt->state = UNUSED; // 실패시 thread를 다시 UNUSED로 초기화해주고
    return -1; // -1 리턴
  }

  nt->tf->eip = (uint)start_routine; // forkret 이후 trapret에서 iret을 통해 이동할 새로운 thread의 시작 함수를 start_routine으로 설정
  nt->tf->esp = _sp;  // 새로운 thread가 가질 stack pointer를 _sp로 설정 (앞에서 할당한 user stack을 가리키는 stack pointer)

  // 이후 또 다른 thread가 생성될 때 마다 기존의 메모리 위에 새로운 stack이 쌓이도록 하기 위해서 
  // 같은 pid를 가지는 모든 process들의 sz를 앞에서 update한 sz를 가리키도록(공유하도록) 해줌 
  acquire(&ptable.lock);
  for(struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == nt->pid){
      p->sz = nt->master_thread->sz; // new thread의 sz도 여기서 한꺼번에 update하기 위해 nt->sz가 아닌 nt->master_thread->sz 이용
    }
  }
  release(&ptable.lock);

  for(int i = 0; i < NOFILE; i++)
    if(nt->master_thread->ofile[i])
      nt->ofile[i] = filedup(nt->master_thread->ofile[i]); // master_thread의 파일 디스크립터를 복제하여 new thread에 저장
  nt->cwd = idup(nt->master_thread->cwd); // master_thread의 현재 작업 디렉토리를 복제하여 new thread에 저장

  safestrcpy(nt->name, nt->master_thread->name, sizeof(nt->master_thread->name)); // 디버깅을 위한 master_thread의 이름을 복사해서 저장

  acquire(&ptable.lock);

  nt->state = RUNNABLE; // 에러 없이 여기까지 왔다면 정상적으로 thread가 생성되었으므로 이 thread도 기존 process들처럼 스케줄링이 될 수 있도록 RUNNABLE 상태로 전환

  release(&ptable.lock);
  
  return 0;
}

// 기존 exit() 콜에서 struct proc에 추가한 retval만 업데이트 하는 코드만 추가하였습니다.
void
thread_exit(void *retval)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  
  // ** 추가 부분 **
  curproc->retval = retval; // ZOMBIE로 바꾸기 전 retval을 update 해줌

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// 기존 wait() 콜에서 struct proc에 추가한 thread 관련 변수들을 초기화하고 thread_exit에서 update한 retval을 리턴하는 코드만 추가하였습니다.
int 
thread_join(thread_t thread, void **retval)
{
  struct proc *p;
  int is_find; // process와 달리 tid로 찾는 경우이기 때문에 havekids대신 is_find로 변수 이름을 설정
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    is_find = 0;
    // Scan through table looking for exited children.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->tid != thread)
        continue;
      is_find = 1; // tid와 같은 thread를 찾은 경우
      if(p->state == ZOMBIE){
        // Found one.
        kfree(p->kstack);
        p->kstack = 0;
        // freevm(p->pgdir); thread는 각자 page table을 가지는게 아니라 복사하기 때문에 process와 달리 여기서 page table을 free해주면 안됨 
        // page table은 master_thread(=process)가 종료될 때, 기존에 process가 종료되는 루틴에 따라 wait() 콜에서 회수될 것임
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        // struct proc에 추가한 thread 관련 변수들 초기화 후 thread_exit에서 update한 retval을 받아와서 리턴
        p->is_thread = 0;
        p->tid = 0;
        p->master_thread = 0;
        *retval = p->retval;
        p->retval = 0;
        release(&ptable.lock);
        return 0;
      }
    }
    // process와 달리 tid로 찾는 경우이기 때문에 havekids대신 is_find로 변수 이름을 설정하였고, thread(=tid)를 찾지 못한 경우 is_find = 0 이므로 -1을 리턴
    if(!is_find || curproc->killed){
      release(&ptable.lock);
      return -1;
    }
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

// exec()와 exit()에서 master_thread(=process)와, 같은 master_thread를 가지는 나머지 thread를 모두 종료해야 하므로 추가로 정의한 함수
void 
kill_all_threads_without_curproc(struct proc *curproc)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == curproc->pid && p != curproc){ 
      // curproc의 경우 exec()에서 실행할 대상이기 때문에 여기서 찾은 p가 curproc인 경우는 제외해줘야함
      // 만약 바로 아래에서처럼 실행할 대상의 kernel stack을 free하면 trap 오류가 발생
      kfree(p->kstack);
      p->kstack = 0;
      // freevm(p->pgdir); // thread는 각자 page table을 가지는게 아니라 복사하기 때문에 process와 달리 page table을 free해주면 안됨 
      // page table은 master_thread(=process)가 exit()될 때 기존 wait() 콜에서 회수될거임
      p->pid = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->state = UNUSED;
      // struct proc에 추가한 thread 관련 변수들 초기화
      p->is_thread = 0;
      p->tid = 0;
      p->master_thread = 0;
      p->retval = 0;
    }
  }
  release(&ptable.lock);
}