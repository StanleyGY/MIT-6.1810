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

#ifdef LAB_LOCK
// Kmem size per core
#define KMEM_SIZE_PER_CORE PGROUNDDOWN((PHYSTOP - PGROUNDUP((uint64)end)) / NCPU)
#define KMEM_ID(pa)        (pa - PGROUNDUP((uint64)end)) / KMEM_SIZE_PER_CORE

struct {
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];
#else
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
#endif

#ifdef LAB_COW
// Count how many user processes' page table are referencing this physical page
int mem_refcount[MEMREF_PGNUM];
#endif

void
kinit()
{
  #ifdef LAB_LOCK
  // A kmem per core
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmems[i].lock, "kmem");

    uint64 pstart = PGROUNDUP((uint64)end) + KMEM_SIZE_PER_CORE * i;
    uint64 pend = PGROUNDUP((uint64)end) + KMEM_SIZE_PER_CORE * (i + 1);
    freerange((void *)pstart, (void *)pend);
  }
  #else
  // A single kmem
  initlock(&kmem.lock, "kmem");
  #ifdef LAB_COW
  memset(mem_refcount, 0, sizeof(int) * MEMREF_PGNUM);
  #endif
  freerange(end, (void*)PHYSTOP);
  #endif
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

  #ifdef LAB_COW
  if (mem_dropref((uint64)pa) > 0)
    return;
  #endif

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  #ifdef LAB_LOCK

  // Find which core's kmem this page belongs to
  int id = KMEM_ID((uint64)pa);

  acquire(&kmems[id].lock);
  r->next = kmems[id].freelist;
  kmems[id].freelist = r;
  release(&kmems[id].lock);

  #else

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);

  #endif
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  #ifdef LAB_LOCK

  // If no pages are available for current CPU, steal from a different CPU
  int hartid = cpuid();
  int id = hartid;

  for (int i = 0; i < NCPU; i++) {
    id = (hartid + i) % NCPU;
    acquire(&kmems[id].lock);
    if (kmems[id].freelist) {
      break;
    }
    release(&kmems[id].lock);
  }

  r = kmems[id].freelist;
  if (r) {
    kmems[id].freelist = r->next;
    release(&kmems[id].lock);
  }

  #else

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    #ifdef LAB_COW
    mem_addref((uint64)r);
    #endif
  }
  release(&kmem.lock);

  #endif

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}

#ifdef LAB_SYSCALL
uint64
mem_freebytes(void) {
  uint64 bytes = 0;
  struct run *r;

  r = kmem.freelist;
  while (r) {
    r = r->next;
    bytes += PGSIZE;
  }

  return bytes;
}
#endif

#ifdef LAB_COW
void
mem_addref(uint64 pa) {
  mem_refcount[MEMREF_INDEX(pa)] ++;
}

int
mem_dropref(uint64 pa) {
  if (mem_refcount[MEMREF_INDEX(pa)] == 0)
    return 0;
  return --mem_refcount[MEMREF_INDEX(pa)];
}
#endif
