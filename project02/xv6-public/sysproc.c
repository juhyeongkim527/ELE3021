#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// system call of Project 01
// return grand-parent process id(gpid)
int 
sys_getgpid(void)
{
  // current process
  struct proc* curproc = myproc();
  
  // parent process 
  struct proc* parentproc = curproc->parent;
  
  // grand-parent process
  struct proc* gparentproc = parentproc->parent;

  // return gpid
  return gparentproc->pid;
}

// system call of Project 02
void
sys_yield(void)
{
  yield();
  return;
}

// return level of current process
int
sys_getlev(void)
{
  return getlev();
}

int
sys_setpriority(void)
{
  int pid;
  int priority;

  // if argument pid or priority does not exist, return -1
  if(argint(0, &pid) < 0 || argint(1, &priority) < 0){
    return -1;
  }
  return setpriority(pid, priority);
}

int
sys_setmonopoly(void)
{
  int pid;
  int password;
  // if argument pid or password does not exist, return -1
  if(argint(0, &pid) < 0 || argint(1, &password) < 0){
    return -1;
  }
  return setmonopoly(pid, password);
}

void
sys_monopolize(void)
{
  monopolize();
  return;
}

void
sys_unmonopolize(void)
{
  unmonopolize();
  return;
}