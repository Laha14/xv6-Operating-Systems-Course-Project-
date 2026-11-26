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

// for task 3
struct {
  struct spinlock lock;
  int cnt[PHYSTOP / PGSIZE];
} refs;

#define PA2IDX(pa) (((uint64)(pa)) / PGSIZE)

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refs.lock, "refs");          // for task 3
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  // for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  //   kfree(p);
  // for task 3
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    refs.cnt[PA2IDX(p)] = 1;             //so kfree drops it to 0
    kfree(p);
  }
}

// for task 3
// increment the reference count of a page
void
kincref(void *pa)
{
  acquire(&refs.lock);
  refs.cnt[PA2IDX(pa)]++;
  release(&refs.lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// void
// kfree(void *pa)
// {
//   struct run *r;

//   if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
//     panic("kfree");

//   // Fill with junk to catch dangling refs.
//   memset(pa, 1, PGSIZE);

//   r = (struct run *)pa;

//   acquire(&kmem.lock);
//   r->next = kmem.freelist;
//   kmem.freelist = r;
//   release(&kmem.lock);
// }

// for task 3
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // drop one reference; stop if others remain
  acquire(&refs.lock);
  if (--refs.cnt[PA2IDX(pa)] > 0) {
    release(&refs.lock);
    return;
  }
  release(&refs.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
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
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  // if (r)
  //   memset((char *)r, 5, PGSIZE); // fill with junk
  // for task 3
  if (r) {
    memset((char *)r, 5, PGSIZE); // fill with junk
    // a freshly allocated page has exactly one owner
    acquire(&refs.lock);
    refs.cnt[PA2IDX(r)] = 1;
    release(&refs.lock);
  }

  return (void *)r;
}
