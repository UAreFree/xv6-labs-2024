// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct spinlock refcntlock;
int refcnt[(PHYSTOP - KERNBASE) / PGSIZE];

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refcntlock, "refcnt");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 页面引用计数 <= 0 时释放
  acquire(&refcntlock);
  if (--refcnt[GETPAINDEX((uint64)pa)] <= 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&refcntlock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    refcnt[GETPAINDEX((uint64)r)] = 1; // 新分配的物理页面引用计数设为1
  }

  return (void*)r;
}

void *
  cowalloc(void* pa) {
  acquire(&refcntlock);

  // 当前物理页引用计数为1 说明是COW之后未做更改的进程的物理页
  // 直接使用原物理页 此函数之后清除其PTE_COW
  if (refcnt[GETPAINDEX((uint64)pa)] <= 1) {
    release(&refcntlock);
    return pa;
  }

  // 当前物理页引用计数为2 最初的COW
  // kalloc一片新的物理页 复制过去
  uint64 mem;
  if((mem = (uint64)kalloc()) == 0) {
    release(&refcntlock);
    return 0; // 内存不够
  }
  memmove((void*)mem, (void*)pa, PGSIZE);
  // 旧页引用计数减1
  refcnt[GETPAINDEX((uint64)pa)]--;

  release(&refcntlock);
  return (void *)mem;
}