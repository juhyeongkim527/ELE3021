// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int use_lock;
  uint refc_arr[PHYSTOP / PGSIZE];  // PHYSTOP : Physical memory 범위 , PGSIZE : Physical page 크기
} refc;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  initlock(&refc.lock, "refc"); // refc locking 추가
  kmem.use_lock = 0;
  refc.use_lock = 0; // refc locking 추가
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
  refc.use_lock = 1; // refc locking 추가
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE){
    refc.refc_arr[V2P(p) / PGSIZE] = 0; // p에 대해서만 refc_arr을 0으로 초기화, freerange 호출 시에는 use_lock이 0이므로 locking 해줄 필요 X
    kfree(p);
  }
}

//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  
  if(get_refc(V2P(v)) == 0 || get_refc(V2P(v)) == 1){
  if(get_refc(V2P(v)) == 1)
    decr_refc(V2P(v));
  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);
  r->next = kmem.freelist;
  kmem.freelist = r;
  }

  else if(get_refc(V2P(v)) > 1){
    decr_refc(V2P(v));
  }

  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  if(refc.use_lock)
    acquire(&refc.lock);

  r = kmem.freelist;

  if(r){
    kmem.freelist = r->next;
    refc.refc_arr[V2P(r) / PGSIZE] = 1; // 새로 page가 할당될 때 해당 page 참조 횟수를 1으로 설정
  }

  if(kmem.use_lock)
    release(&kmem.lock);
  if(refc.use_lock)
    release(&refc.lock);

  return (char*)r;
}

void 
incr_refc(uint pa)
{
  if(refc.use_lock)
    acquire(&refc.lock);
  
  refc.refc_arr[pa / PGSIZE]++;
  
  if(refc.use_lock)
    release(&refc.lock);
}

void 
decr_refc(uint pa)
{
  if(refc.use_lock)
    acquire(&refc.lock);
  
  refc.refc_arr[pa / PGSIZE]--;
  
  if(refc.use_lock)
    release(&refc.lock);
}

int 
get_refc(uint pa)
{
  if(refc.use_lock)
    acquire(&refc.lock);
  
  int refc_ = refc.refc_arr[pa / PGSIZE];
  
  if(refc.use_lock)
    release(&refc.lock);

  return refc_;
}

int 
countfp(void)
{
  int count = 0;
  if(kmem.use_lock)
    acquire(&kmem.lock);

  for(struct run *fp = kmem.freelist; fp != 0; fp = fp->next){
    count++;
  }
  
  if(kmem.use_lock)
    release(&kmem.lock);

  return count;
}
